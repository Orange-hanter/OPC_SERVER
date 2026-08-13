#pragma once

#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <string_view>

namespace opc::adapters {

/// Asio io_context + strand-per-endpoint (ADR-0002). Header stays Asio-free.
class AsioReactor {
public:
    explicit AsioReactor(std::size_t worker_threads);
    ~AsioReactor();

    AsioReactor(const AsioReactor&) = delete;
    AsioReactor& operator=(const AsioReactor&) = delete;

    void ensure_strand(std::string_view endpoint_id);

    /// Serialize `work` on the endpoint strand.
    void post(std::string_view endpoint_id, std::function<void()> work);

    /// First tick immediately, then again after each completion + `period`.
    void repeat_on_strand(std::string_view endpoint_id,
                          std::chrono::milliseconds period,
                          std::function<void()> work);

    /// Unstranded repeating timer (watchlist / historian flush).
    void repeat(std::chrono::milliseconds period, std::function<void()> work);

    /// Spawn worker threads. Safe to call once.
    void start();

    /// Cancel timers, stop io_context, join workers. Safe from any thread except a worker.
    void stop();

    /// Block until `stop()` (SIGINT/SIGTERM also request stop).
    void run_until_stop();

    [[nodiscard]] bool running() const;
    [[nodiscard]] std::size_t worker_count() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace opc::adapters
