#include "adapters/asio_reactor.hpp"

#include <asio.hpp>

#include <atomic>
#include <chrono>
#include <csignal>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace opc::adapters {
namespace {

using IoContext = asio::io_context;
using Strand = asio::strand<IoContext::executor_type>;

struct RepeatOp : std::enable_shared_from_this<RepeatOp> {
    RepeatOp(IoContext& ctx,
             Strand* strand,
             std::chrono::milliseconds period,
             std::function<void()> work,
             std::atomic<bool>* stopping)
        : timer(ctx),
          owned_strand(strand == nullptr
                           ? std::make_unique<Strand>(asio::make_strand(ctx.get_executor()))
                           : nullptr),
          strand(strand != nullptr ? strand : owned_strand.get()),
          period(period),
          work(std::move(work)),
          stopping(stopping) {}

    void start() { arm(std::chrono::milliseconds{0}); }

    void cancel() {
        // Timer cancel must run on the same strand as arm()/async_wait (TSan / Asio).
        asio::dispatch(*strand, [self = shared_from_this()] {
            self->timer.cancel();
        });
    }

    asio::steady_timer timer;
    std::unique_ptr<Strand> owned_strand;
    Strand* strand{nullptr};
    std::chrono::milliseconds period{0};
    std::function<void()> work;
    std::atomic<bool>* stopping{nullptr};

private:
    void arm(std::chrono::milliseconds delay) {
        timer.expires_after(delay);
        auto handler = [self = shared_from_this()](const asio::error_code& ec) {
            if (ec || self->stopping->load()) {
                return;
            }
            self->work();
            if (!self->stopping->load()) {
                self->arm(self->period);
            }
        };
        timer.async_wait(asio::bind_executor(*strand, std::move(handler)));
    }
};

}  // namespace

using WorkGuard = asio::executor_work_guard<IoContext::executor_type>;

struct StrandExecutor final : ports::IExecutor {
    StrandExecutor(Strand* strand, std::atomic<bool>* stopping)
        : strand(strand), stopping(stopping) {}

    void post(std::move_only_function<void()> work) override {
        if (!work || stopping == nullptr || stopping->load()) {
            return;
        }
        asio::post(*strand, [w = std::move(work)]() mutable { w(); });
    }

    Strand* strand{nullptr};
    std::atomic<bool>* stopping{nullptr};
};

struct AsioReactor::Impl {
    explicit Impl(std::size_t worker_threads)
        : worker_threads(worker_threads < 1 ? 1 : worker_threads),
          guard(asio::make_work_guard(ctx)) {}

    Strand& strand_for(std::string_view endpoint_id) {
        const std::string key{endpoint_id};
        std::lock_guard lock(mutex);
        auto& slot = strands[key];
        if (!slot) {
            slot = std::make_unique<Strand>(asio::make_strand(ctx.get_executor()));
        }
        return *slot;
    }

    void add_repeat(Strand* strand,
                    std::chrono::milliseconds period,
                    std::function<void()> work) {
        auto op = std::make_shared<RepeatOp>(ctx, strand, period, std::move(work), &stopping);
        {
            std::lock_guard lock(mutex);
            repeats.push_back(op);
        }
        op->start();
    }

    void request_stop() {
        if (stopping.exchange(true)) {
            return;
        }
        if (signals) {
            signals->cancel();
        }
        std::vector<std::shared_ptr<RepeatOp>> snapshot;
        {
            std::lock_guard lock(mutex);
            snapshot = repeats;
        }
        for (auto& op : snapshot) {
            if (op) {
                op->cancel();
            }
        }
        if (guard) {
            guard->reset();
        }
        ctx.stop();
        {
            std::lock_guard lock(mutex);
            repeats.clear();
        }
    }

    std::size_t worker_threads{2};
    IoContext ctx;
    std::optional<WorkGuard> guard;
    std::mutex mutex;
    std::unordered_map<std::string, std::unique_ptr<Strand>> strands;
    std::vector<std::shared_ptr<RepeatOp>> repeats;
    std::vector<std::thread> threads;
    std::unique_ptr<asio::signal_set> signals;
    std::atomic<bool> stopping{false};
    std::atomic<bool> running{false};
};

AsioReactor::AsioReactor(std::size_t worker_threads)
    : impl_(std::make_unique<Impl>(worker_threads)) {}

AsioReactor::~AsioReactor() {
    stop();
}

void AsioReactor::ensure_strand(std::string_view endpoint_id) {
    impl_->strand_for(endpoint_id);
}

void AsioReactor::post(std::string_view endpoint_id, std::function<void()> work) {
    if (!work || impl_->stopping.load()) {
        return;
    }
    auto& strand = impl_->strand_for(endpoint_id);
    asio::post(strand, std::move(work));
}

std::shared_ptr<ports::IExecutor> AsioReactor::executor_for(std::string_view endpoint_id) {
    auto& strand = impl_->strand_for(endpoint_id);
    return std::make_shared<StrandExecutor>(&strand, &impl_->stopping);
}

void AsioReactor::repeat_on_strand(std::string_view endpoint_id,
                                   std::chrono::milliseconds period,
                                   std::function<void()> work) {
    if (!work) {
        return;
    }
    if (period.count() < 1) {
        period = std::chrono::milliseconds{1};
    }
    impl_->add_repeat(&impl_->strand_for(endpoint_id), period, std::move(work));
}

void AsioReactor::repeat(std::chrono::milliseconds period, std::function<void()> work) {
    if (!work) {
        return;
    }
    if (period.count() < 1) {
        period = std::chrono::milliseconds{1};
    }
    impl_->add_repeat(nullptr, period, std::move(work));
}

void AsioReactor::start() {
    if (impl_->running.exchange(true)) {
        return;
    }
    impl_->stopping = false;
    if (!impl_->guard || !impl_->guard->owns_work()) {
        impl_->guard.emplace(asio::make_work_guard(impl_->ctx));
    }
    impl_->ctx.restart();
    impl_->threads.clear();
    impl_->threads.reserve(impl_->worker_threads);
    for (std::size_t i = 0; i < impl_->worker_threads; ++i) {
        impl_->threads.emplace_back([this] { impl_->ctx.run(); });
    }
}

void AsioReactor::stop() {
    impl_->request_stop();
    for (auto& thread : impl_->threads) {
        if (thread.joinable() && thread.get_id() != std::this_thread::get_id()) {
            thread.join();
        }
    }
    impl_->threads.clear();
    impl_->signals.reset();
    impl_->running = false;
}

void AsioReactor::run_until_stop() {
    impl_->signals = std::make_unique<asio::signal_set>(impl_->ctx, SIGINT, SIGTERM);
    impl_->signals->async_wait([this](const asio::error_code&, int) { impl_->request_stop(); });
    start();
    for (auto& thread : impl_->threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    impl_->threads.clear();
    impl_->running = false;
}

bool AsioReactor::running() const {
    return impl_->running.load();
}

std::size_t AsioReactor::worker_count() const {
    return impl_->worker_threads;
}

}  // namespace opc::adapters
