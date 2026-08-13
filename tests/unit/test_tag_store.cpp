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
    REQUIRE(got->quality == Quality::Good);

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
    REQUIRE(got->quality == Quality::Uncertain);
    REQUIRE(got->reason == QualityReason::Stale);
}
