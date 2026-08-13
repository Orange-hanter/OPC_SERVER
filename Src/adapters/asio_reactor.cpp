#include "adapters/asio_reactor.hpp"

#include <asio.hpp>

#include <atomic>
#include <chrono>
#include <csignal>
#include <functional>
#include <mutex>
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
          strand(strand),
          period(period),
          work(std::move(work)),
          stopping(stopping) {}

    void start() { arm(std::chrono::milliseconds{0}); }

    void cancel() {
        asio::error_code ignored;
        timer.cancel(ignored);
    }

    asio::steady_timer timer;
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
        if (strand != nullptr) {
            timer.async_wait(asio::bind_executor(*strand, std::move(handler)));
        } else {
            timer.async_wait(std::move(handler));
        }
    }
};

}  // namespace

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
        asio::error_code ignored;
        if (signals) {
            signals->cancel(ignored);
        }
        {
            std::lock_guard lock(mutex);
            for (auto& op : repeats) {
                if (op) {
                    op->cancel();
                }
            }
            repeats.clear();
        }
        guard.reset();
        ctx.stop();
    }

    std::size_t worker_threads{2};
    IoContext ctx;
    asio::executor_work_guard<IoContext::executor_type> guard;
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
    if (!impl_->guard.owns_work()) {
        impl_->guard = asio::make_work_guard(impl_->ctx);
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
