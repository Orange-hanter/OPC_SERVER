#include <catch2/catch_test_macros.hpp>

#include "core/tag_store.hpp"
#include "domain/tag_value_util.hpp"

#include <atomic>
#include <thread>
#include <vector>

using opc::domain::Quality;
using opc::domain::QualityReason;
using opc::domain::TagValue;

TEST_CASE("TagStore concurrent publish and get stay consistent", "[unit][core][tagstore][tsan]") {
    opc::core::TagStore store;
    std::atomic<int> notifications{0};
    store.subscribe([&](auto, const TagValue&) { notifications.fetch_add(1); });

    constexpr int kWriters = 4;
    constexpr int kIters = 200;
    std::vector<std::thread> threads;
    threads.reserve(static_cast<unsigned>(kWriters));
    for (int w = 0; w < kWriters; ++w) {
        threads.emplace_back([&store, w] {
            for (int i = 0; i < kIters; ++i) {
                TagValue value;
                value.value = static_cast<std::uint16_t>(i);
                value.quality = Quality::Good;
                value.server_ts = i;
                store.publish(static_cast<opc::domain::TagId>(w + 1), value);
                (void)store.get(static_cast<opc::domain::TagId>(w + 1));
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    REQUIRE(notifications.load() == kWriters * kIters);
    for (int w = 0; w < kWriters; ++w) {
        auto got = store.get(static_cast<opc::domain::TagId>(w + 1));
        REQUIRE(got);
        REQUIRE(got->quality == Quality::Good);
    }
}

TEST_CASE("with_quality preserves last engineering value", "[unit][domain]") {
    TagValue previous;
    previous.value = 12.5f;
    previous.quality = Quality::Good;
    previous.source_ts = 10;
    auto next = opc::domain::with_quality(previous, Quality::Bad, QualityReason::Timeout, 50);
    REQUIRE(std::get<float>(next.value) == 12.5f);
    REQUIRE(next.quality == Quality::Bad);
    REQUIRE(next.reason == QualityReason::Timeout);
    REQUIRE(next.source_ts == 10);
    REQUIRE(next.server_ts == 50);
}
