#include <catch2/catch_test_macros.hpp>

#include "adapters/manual_clock.hpp"
#include "adapters/testsupport/fake_modbus_transport.hpp"
#include "core/dispatcher.hpp"
#include "core/runtime_index.hpp"
#include "core/tag_store.hpp"
#include "ports/i_metrics.hpp"
#include "project/load.hpp"

using opc::adapters::ManualClock;
using opc::adapters::testsupport::FakeModbusTransport;
using opc::core::Dispatcher;
using opc::core::RuntimeIndex;
using opc::core::TagStore;
using opc::ports::NullMetrics;

namespace {

std::shared_ptr<const opc::project::Project> two_endpoint_project() {
    constexpr std::string_view kJson = R"({
      "schemaVersion": 1,
      "name": "iso",
      "endpoints": [
        {"id": "ep-a", "host": "127.0.0.1", "port": 502, "transport": "tcp"},
        {"id": "ep-b", "host": "127.0.0.1", "port": 503, "transport": "tcp"}
      ],
      "devices": [
        {"id": "da", "endpointId": "ep-a", "unitId": 1, "tags": [
          {"name": "A", "area": "holding", "address": 0, "type": "uint16",
           "byteOrder": "AB", "group": "ga", "writable": true},
          {"name": "CoilA", "area": "coil", "address": 0, "type": "bool", "group": "ga"}
        ]},
        {"id": "db", "endpointId": "ep-b", "unitId": 1, "tags": [
          {"name": "B", "area": "holding", "address": 0, "type": "uint16",
           "byteOrder": "AB", "group": "gb"}
        ]}
      ],
      "pollGroups": [
        {"id": "ga", "periodMs": 100, "priority": "fast", "deviceId": "da", "tagNames": ["A", "CoilA"]},
        {"id": "gb", "periodMs": 100, "priority": "fast", "deviceId": "db", "tagNames": ["B"]}
      ]
    })";
    auto loaded = opc::project::load_json_text(kJson, "iso.json");
    REQUIRE(loaded.ok);
    return std::make_shared<opc::project::Project>(std::move(loaded.project));
}

}  // namespace

TEST_CASE("Dispatcher isolates endpoints when one transport fails",
          "[component][core][dispatcher][fault]") {
    auto project = two_endpoint_project();
    RuntimeIndex index = RuntimeIndex::build(project);
    TagStore store;
    ManualClock clock{1'000};
    NullMetrics metrics;
    FakeModbusTransport ta;
    FakeModbusTransport tb;
    REQUIRE(ta.connect({.host = "127.0.0.1", .port = 502}));
    REQUIRE(tb.connect({.host = "127.0.0.1", .port = 503}));
    ta.set_holding(1, 0, 11);
    tb.set_holding(1, 0, 22);
    ta.set_coil(1, 0, true);

    Dispatcher dispatcher(Dispatcher::Dependencies{
        .index = index, .tag_store = &store, .clock = &clock, .metrics = &metrics});
    dispatcher.bind_transport("ep-a", &ta);
    dispatcher.bind_transport("ep-b", &tb);

    REQUIRE(dispatcher.poll_due("ep-a", 1'000).has_value());
    REQUIRE(dispatcher.poll_due("ep-b", 1'000).has_value());
    auto a = index.find_by_name("A");
    auto b = index.find_by_name("B");
    auto coil = index.find_by_name("CoilA");
    REQUIRE(store.get(a->id)->quality == opc::domain::Quality::Good);
    REQUIRE(std::get<std::uint16_t>(store.get(a->id)->value) == 11);
    REQUIRE(std::get<bool>(store.get(coil->id)->value));
    REQUIRE(std::get<std::uint16_t>(store.get(b->id)->value) == 22);

    ta.fail_next(opc::domain::Error{
        opc::domain::ErrorCode::Timeout, "injected timeout", "fake.modbus", true});
    REQUIRE_FALSE(dispatcher.poll_due("ep-a", 1'200).has_value());
    REQUIRE(store.get(a->id)->quality == opc::domain::Quality::Bad);
    REQUIRE(store.get(a->id)->reason == opc::domain::QualityReason::Timeout);
    REQUIRE(std::get<std::uint16_t>(store.get(a->id)->value) == 11);

    REQUIRE(dispatcher.poll_due("ep-b", 1'200).has_value());
    REQUIRE(store.get(b->id)->quality == opc::domain::Quality::Good);
}

TEST_CASE("Dispatcher maps Modbus exception to Bad quality and continues other tags",
          "[component][core][dispatcher][fault]") {
    auto project = two_endpoint_project();
    RuntimeIndex index = RuntimeIndex::build(project);
    TagStore store;
    ManualClock clock{1'000};
    NullMetrics metrics;
    FakeModbusTransport ta;
    REQUIRE(ta.connect({.host = "127.0.0.1", .port = 502}));
    ta.set_holding(1, 0, 5);
    ta.set_coil(1, 0, false);

    Dispatcher dispatcher(Dispatcher::Dependencies{
        .index = index, .tag_store = &store, .clock = &clock, .metrics = &metrics});
    dispatcher.bind_transport("ep-a", &ta);

    REQUIRE(dispatcher.poll_due("ep-a", 1'000).has_value());
    ta.fail_next(opc::domain::Error{
        opc::domain::ErrorCode::ModbusException, "ex", "fake.modbus", true, 2});
    REQUIRE_FALSE(dispatcher.poll_due("ep-a", 1'200).has_value());
    auto a = index.find_by_name("A");
    REQUIRE(store.get(a->id)->reason == opc::domain::QualityReason::ModbusException);
}

TEST_CASE("Dispatcher skips poll group until period elapses on ManualClock",
          "[component][core][dispatcher]") {
    auto project = two_endpoint_project();
    RuntimeIndex index = RuntimeIndex::build(project);
    TagStore store;
    ManualClock clock{1'000};
    NullMetrics metrics;
    FakeModbusTransport ta;
    REQUIRE(ta.connect({.host = "127.0.0.1", .port = 502}));
    ta.set_holding(1, 0, 1);

    Dispatcher dispatcher(Dispatcher::Dependencies{
        .index = index, .tag_store = &store, .clock = &clock, .metrics = &metrics});
    dispatcher.bind_transport("ep-a", &ta);
    REQUIRE(dispatcher.poll_due("ep-a", 1'000).has_value());
    ta.set_holding(1, 0, 99);
    REQUIRE(dispatcher.poll_due("ep-a", 1'050).has_value());
    auto a = index.find_by_name("A");
    REQUIRE(std::get<std::uint16_t>(store.get(a->id)->value) == 1);
    REQUIRE(dispatcher.poll_due("ep-a", 1'100).has_value());
    REQUIRE(std::get<std::uint16_t>(store.get(a->id)->value) == 99);
}

TEST_CASE("writes_first flushes before poll on the same tick",
          "[component][core][dispatcher]") {
    auto project = two_endpoint_project();
    RuntimeIndex index = RuntimeIndex::build(project);
    TagStore store;
    ManualClock clock{1'000};
    NullMetrics metrics;
    FakeModbusTransport ta;
    REQUIRE(ta.connect({.host = "127.0.0.1", .port = 502}));
    ta.set_holding(1, 0, 0);

    Dispatcher dispatcher(Dispatcher::Dependencies{
        .index = index, .tag_store = &store, .clock = &clock, .metrics = &metrics});
    dispatcher.bind_transport("ep-a", &ta);
    auto a = index.find_by_name("A");
    REQUIRE(dispatcher.enqueue_write(a->id, std::uint16_t{77}).has_value());
    REQUIRE(dispatcher.poll_due("ep-a", 2'000).has_value());
    REQUIRE(ta.holding_at(1, 0) == 77);
    REQUIRE(std::get<std::uint16_t>(store.get(a->id)->value) == 77);
}
