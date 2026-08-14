#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/tag_store.hpp"

using opc::domain::Quality;
using opc::domain::QualityReason;
using opc::domain::TagId;
using opc::domain::TagValue;

TEST_CASE("TagStore publish/get/subscribe", "[unit][core][tagstore]") {
    opc::core::TagStore store;
    int calls = 0;
    TagValue seen{};

    const auto sub = store.subscribe([&](TagId id, const TagValue& value) {
        REQUIRE(id == 7);
        seen = value;
        ++calls;
    });

    TagValue v;
    v.value = float{12.5f};
    v.quality = Quality::Good;
    v.server_ts = 1000;
    store.publish(7, v);

    REQUIRE(calls == 1);
    REQUIRE(std::get<float>(seen.value) == Catch::Approx(12.5f));

    const auto got = store.get(7);
    REQUIRE(got.has_value());
    const auto stored = got.value_or(TagValue{});
    REQUIRE(stored.quality == Quality::Good);

    store.unsubscribe(sub);
    store.publish(7, v);
    REQUIRE(calls == 1);
}

TEST_CASE("TagStore mark_stale_before", "[unit][core][tagstore]") {
    opc::core::TagStore store;
    TagValue v;
    v.value = std::uint16_t{1};
    v.quality = Quality::Good;
    v.server_ts = 10;
    store.publish(1, v);

    store.mark_stale_before(50, QualityReason::Stale);
    const auto got = store.get(1);
    REQUIRE(got.has_value());
    const auto stale = got.value_or(TagValue{});
    REQUIRE(stale.quality == Quality::Uncertain);
    REQUIRE(stale.reason == QualityReason::Stale);
}

TEST_CASE("TagStore stale cutoff is strict and notifies only on transition",
          "[unit][core][tagstore][contract]") {
    opc::core::TagStore store;
    int notifications = 0;
    TagValue notified;
    store.subscribe([&](TagId id, const TagValue& value) {
        if (id == 1) {
            ++notifications;
            notified = value;
        }
    });

    TagValue value;
    value.value = std::uint16_t{17};
    value.quality = Quality::Good;
    value.reason = QualityReason::None;
    value.server_ts = 100;
    store.publish(1, value);
    notifications = 0;

    store.mark_stale_before(100, QualityReason::Stale);
    REQUIRE(store.get(1));
    CHECK(store.get(1)->quality == Quality::Good);
    CHECK(notifications == 0);

    store.mark_stale_before(101, QualityReason::Stale);
    const auto stale = store.get(1);
    REQUIRE(stale);
    CHECK(stale->quality == Quality::Uncertain);
    CHECK(stale->reason == QualityReason::Stale);
    CHECK(std::get<std::uint16_t>(stale->value) == 17);
    CHECK(notifications == 1);
    CHECK(notified.quality == Quality::Uncertain);

    store.mark_stale_before(102, QualityReason::Timeout);
    CHECK(notifications == 1);
    CHECK(store.get(1)->reason == QualityReason::Stale);
}

TEST_CASE("TagStore callbacks may unsubscribe and publish reentrantly",
          "[unit][core][tagstore][hardening]") {
    opc::core::TagStore store;
    int first_calls = 0;
    int second_calls = 0;
    std::uint64_t first_id = 0;

    first_id = store.subscribe([&](TagId id, const TagValue&) {
        ++first_calls;
        store.unsubscribe(first_id);
        if (id == 1) {
            TagValue nested;
            nested.value = std::uint16_t{2};
            nested.quality = Quality::Good;
            store.publish(2, nested);
        }
    });
    store.subscribe([&](TagId, const TagValue&) { ++second_calls; });

    TagValue value;
    value.value = std::uint16_t{1};
    value.quality = Quality::Good;
    store.publish(1, value);

    CHECK(first_calls == 1);
    CHECK(second_calls == 2);
    REQUIRE(store.get(2));

    store.publish(3, value);
    CHECK(first_calls == 1);
    CHECK(second_calls == 3);
}
