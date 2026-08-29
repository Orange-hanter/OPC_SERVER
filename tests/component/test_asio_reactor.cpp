#include <catch2/catch_test_macros.hpp>

#include "adapters/asio_reactor.hpp"

#include <atomic>
#include <chrono>
#include <thread>

using opc::adapters::AsioReactor;
using namespace std::chrono_literals;

TEST_CASE("AsioReactor repeat fires until stop", "[adapters][asio]") {
    AsioReactor reactor(2);
    std::atomic<int> ticks{0};
    reactor.start();
    reactor.repeat(5ms, [&] { ticks.fetch_add(1); });

    const auto deadline = std::chrono::steady_clock::now() + 200ms;
    while (ticks.load() < 3 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(2ms);
    }
    REQUIRE(ticks.load() >= 3);

    reactor.stop();
    const int after_stop = ticks.load();
    std::this_thread::sleep_for(30ms);
    CHECK(ticks.load() == after_stop);
}

TEST_CASE("AsioReactor strand isolation: slow work does not stall the other strand",
          "[adapters][asio]") {
    AsioReactor reactor(2);
    std::atomic<int> slow_ticks{0};
    std::atomic<int> fast_ticks{0};

    reactor.start();
    reactor.repeat_on_strand("slow", 5ms, [&] {
        std::this_thread::sleep_for(120ms);
        slow_ticks.fetch_add(1);
    });
    reactor.repeat_on_strand("fast", 5ms, [&] { fast_ticks.fetch_add(1); });

    const auto deadline = std::chrono::steady_clock::now() + 80ms;
    while (fast_ticks.load() < 3 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(2ms);
    }
    reactor.stop();

    REQUIRE(fast_ticks.load() >= 3);
    CHECK(fast_ticks.load() > slow_ticks.load());
}
