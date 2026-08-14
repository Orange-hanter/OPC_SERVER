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

std::shared_ptr<const opc::project::Project> four_area_project() {
    constexpr std::string_view kJson = R"({
      "schemaVersion": 1,
      "name": "areas",
      "endpoints": [
        {"id": "ep1", "host": "127.0.0.1", "port": 502, "transport": "tcp"}
      ],
      "devices": [
        {"id": "d1", "endpointId": "ep1", "unitId": 1, "tags": [
          {"name": "Hold", "area": "holding", "address": 0, "type": "uint16",
           "byteOrder": "AB", "group": "g1"},
          {"name": "In", "area": "input", "address": 2, "type": "uint16",
           "byteOrder": "AB", "group": "g1"},
          {"name": "CoilW", "area": "coil", "address": 1, "type": "bool",
           "writable": true, "group": "g1"},
          {"name": "Disc", "area": "discrete", "address": 3, "type": "bool", "group": "g1"}
        ]}
      ],
      "pollGroups": [
        {"id": "g1", "periodMs": 100, "priority": "fast", "deviceId": "d1",
         "tagNames": ["Hold", "In", "CoilW", "Disc"]}
      ]
    })";
    auto loaded = opc::project::load_json_text(kJson, "areas.json");
    REQUIRE(loaded.ok);
    return std::make_shared<opc::project::Project>(std::move(loaded.project));
}

std::shared_ptr<const opc::project::Project> blocks_only_project() {
    constexpr std::string_view kJson = R"({
      "schemaVersion": 1,
      "name": "blocks",
      "endpoints": [
        {"id": "ep1", "host": "127.0.0.1", "port": 502, "transport": "tcp"}
      ],
      "devices": [
        {"id": "d1", "endpointId": "ep1", "unitId": 1, "tags": [
          {"name": "Blk", "area": "holding", "address": 5, "type": "uint16",
           "byteOrder": "AB", "group": "other"}
        ]}
      ],
      "pollGroups": [
        {"id": "g1", "periodMs": 50, "priority": "fast", "deviceId": "d1",
         "blocks": [{"area": "holding", "start": 5, "count": 1}]}
      ]
    })";
    auto loaded = opc::project::load_json_text(kJson, "blocks.json");
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

TEST_CASE("Dispatcher polls input, discrete and writable coil",
          "[component][core][dispatcher][modbus]") {
    auto project = four_area_project();
    RuntimeIndex index = RuntimeIndex::build(project);
    TagStore store;
    ManualClock clock{1'000};
    NullMetrics metrics;
    FakeModbusTransport transport;
    REQUIRE(transport.connect({.host = "127.0.0.1", .port = 502}));
    transport.set_holding(1, 0, 9);
    transport.set_input(1, 2, 44);
    transport.set_coil(1, 1, false);
    transport.set_discrete(1, 3, true);

    Dispatcher dispatcher(Dispatcher::Dependencies{
        .index = index, .tag_store = &store, .clock = &clock, .metrics = &metrics});
    dispatcher.bind_transport("ep1", &transport);

    REQUIRE(dispatcher.poll_due("ep1", 1'000).has_value());
    auto hold = index.find_by_name("Hold");
    auto in = index.find_by_name("In");
    auto coil = index.find_by_name("CoilW");
    auto disc = index.find_by_name("Disc");
    REQUIRE(std::get<std::uint16_t>(store.get(hold->id)->value) == 9);
    REQUIRE(std::get<std::uint16_t>(store.get(in->id)->value) == 44);
    REQUIRE_FALSE(std::get<bool>(store.get(coil->id)->value));
    REQUIRE(std::get<bool>(store.get(disc->id)->value));

    REQUIRE(dispatcher.enqueue_write(coil->id, true).has_value());
    REQUIRE(dispatcher.poll_due("ep1", 1'200).has_value());
    REQUIRE(transport.coil_at(1, 1));
    REQUIRE(std::get<bool>(store.get(coil->id)->value));
}

TEST_CASE("enqueue_write rejects unknown and non-writable tags",
          "[component][core][dispatcher][hardening]") {
    auto project = four_area_project();
    RuntimeIndex index = RuntimeIndex::build(project);
    TagStore store;
    ManualClock clock{1'000};
    NullMetrics metrics;
    Dispatcher dispatcher(Dispatcher::Dependencies{
        .index = index, .tag_store = &store, .clock = &clock, .metrics = &metrics});

    auto missing = dispatcher.enqueue_write(999, std::uint16_t{1});
    REQUIRE_FALSE(missing);
    CHECK(missing.error().code == opc::domain::ErrorCode::NotFound);

    auto hold = index.find_by_name("Hold");
    auto denied = dispatcher.enqueue_write(hold->id, std::uint16_t{1});
    REQUIRE_FALSE(denied);
    CHECK(denied.error().code == opc::domain::ErrorCode::Permission);
}

TEST_CASE("Dispatcher publishes DecodingError when registers cannot be decoded",
          "[component][core][dispatcher][fault]") {
    auto project = std::make_shared<opc::project::Project>();
    project->endpoints.push_back({.id = "ep1", .host = "127.0.0.1", .port = 502});
    opc::project::Device device;
    device.id = "d1";
    device.endpoint_id = "ep1";
    device.unit_id = 1;
    opc::project::Tag tag;
    tag.name = "BadOrder";
    tag.area = opc::project::Area::Holding;
    tag.type = opc::project::TagType::Float32;
    tag.byte_order = "XYZZ";
    tag.group = "g1";
    device.tags.push_back(tag);
    project->devices.push_back(device);
    opc::project::PollGroup group;
    group.id = "g1";
    group.period_ms = 50;
    group.device_id = "d1";
    group.tag_names = {"BadOrder"};
    project->poll_groups.push_back(group);

    RuntimeIndex index = RuntimeIndex::build(project);
    TagStore store;
    ManualClock clock{1'000};
    NullMetrics metrics;
    FakeModbusTransport transport;
    REQUIRE(transport.connect({.host = "127.0.0.1", .port = 502}));
    transport.set_holding(1, 0, 1);
    transport.set_holding(1, 1, 2);

    Dispatcher dispatcher(Dispatcher::Dependencies{
        .index = index, .tag_store = &store, .clock = &clock, .metrics = &metrics});
    dispatcher.bind_transport("ep1", &transport);
    REQUIRE_FALSE(dispatcher.poll_due("ep1", 1'000).has_value());
    auto binding = index.find_by_name("BadOrder");
    auto stored = store.get(binding->id);
    REQUIRE(stored);
    CHECK(stored->quality == opc::domain::Quality::Bad);
    CHECK(stored->reason == opc::domain::QualityReason::DecodingError);
}

TEST_CASE("failed flush publishes WriteRejected and keeps prior value",
          "[component][core][dispatcher][fault]") {
    auto project = four_area_project();
    RuntimeIndex index = RuntimeIndex::build(project);
    TagStore store;
    ManualClock clock{1'000};
    NullMetrics metrics;
    FakeModbusTransport transport;
    REQUIRE(transport.connect({.host = "127.0.0.1", .port = 502}));

    auto coil = index.find_by_name("CoilW");
    store.publish(coil->id,
                  opc::domain::TagValue{.value = false,
                                        .quality = opc::domain::Quality::Good,
                                        .reason = opc::domain::QualityReason::None,
                                        .source_ts = 10,
                                        .server_ts = 10});

    Dispatcher dispatcher(Dispatcher::Dependencies{
        .index = index, .tag_store = &store, .clock = &clock, .metrics = &metrics});
    dispatcher.bind_transport("ep1", &transport);
    REQUIRE(dispatcher.enqueue_write(coil->id, true).has_value());
    transport.fail_next(opc::domain::Error{
        opc::domain::ErrorCode::Timeout, "write timeout", "fake.modbus", true});
    REQUIRE_FALSE(dispatcher.flush_writes("ep1").has_value());
    auto stored = store.get(coil->id);
    REQUIRE(stored);
    CHECK(std::get<bool>(stored->value) == false);
    CHECK(stored->quality == opc::domain::Quality::Bad);
    CHECK(stored->reason == opc::domain::QualityReason::WriteRejected);
}

TEST_CASE("Dispatcher reconnects after injected connect failure",
          "[component][core][dispatcher][fault]") {
    auto project = four_area_project();
    RuntimeIndex index = RuntimeIndex::build(project);
    TagStore store;
    ManualClock clock{1'000};
    NullMetrics metrics;
    FakeModbusTransport transport;
    transport.fail_connect_once();

    Dispatcher dispatcher(Dispatcher::Dependencies{
        .index = index, .tag_store = &store, .clock = &clock, .metrics = &metrics});
    dispatcher.bind_transport("ep1", &transport);
    REQUIRE_FALSE(dispatcher.poll_due("ep1", 1'000).has_value());
    REQUIRE_FALSE(transport.is_connected());

    transport.set_holding(1, 0, 3);
    REQUIRE(dispatcher.poll_due("ep1", 1'200).has_value());
    auto hold = index.find_by_name("Hold");
    REQUIRE(std::get<std::uint16_t>(store.get(hold->id)->value) == 3);
}

TEST_CASE("Dispatcher polls tags selected by register blocks",
          "[component][core][dispatcher]") {
    auto project = blocks_only_project();
    RuntimeIndex index = RuntimeIndex::build(project);
    TagStore store;
    ManualClock clock{1'000};
    NullMetrics metrics;
    FakeModbusTransport transport;
    REQUIRE(transport.connect({.host = "127.0.0.1", .port = 502}));
    transport.set_holding(1, 5, 123);

    Dispatcher dispatcher(Dispatcher::Dependencies{
        .index = index, .tag_store = &store, .clock = &clock, .metrics = &metrics});
    dispatcher.bind_transport("ep1", &transport);
    REQUIRE(dispatcher.poll_due("ep1", 1'000).has_value());
    auto blk = index.find_by_name("Blk");
    REQUIRE(std::get<std::uint16_t>(store.get(blk->id)->value) == 123);
}
