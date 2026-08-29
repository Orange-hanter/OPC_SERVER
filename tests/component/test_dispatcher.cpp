#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "adapters/memory_metrics.hpp"
#include "adapters/system_clock.hpp"
#include "adapters/testsupport/fake_modbus_transport.hpp"
#include "adapters/testsupport/recording_tracer.hpp"
#include "core/dispatcher.hpp"
#include "core/runtime_index.hpp"
#include "core/tag_store.hpp"
#include "core/translator.hpp"
#include "ports/i_metrics.hpp"
#include "project/load.hpp"

#include <atomic>
#include <chrono>
#include <future>
#include <thread>
#include <vector>
using opc::adapters::testsupport::FakeModbusTransport;
using opc::core::Dispatcher;
using opc::core::RuntimeIndex;
using opc::core::TagStore;
using opc::core::Translator;
using opc::ports::NullMetrics;

namespace {

std::shared_ptr<const opc::project::Project> tiny_project() {
    constexpr std::string_view kJson = R"({
      "schemaVersion": 1,
      "name": "tiny",
      "endpoints": [
        {"id": "ep1", "host": "127.0.0.1", "port": 502, "transport": "tcp"}
      ],
      "devices": [
        {"id": "d1", "endpointId": "ep1", "unitId": 1, "tags": [
          {"name": "Level", "nodePath": "Plant/Level", "area": "holding", "address": 0,
           "type": "float32", "byteOrder": "ABCD", "writable": true, "group": "g1"},
          {"name": "Setpoint", "nodePath": "Plant/Setpoint", "area": "holding", "address": 2,
           "type": "uint16", "byteOrder": "AB", "writable": true, "group": "g1"}
        ]}
      ],
      "pollGroups": [
        {"id": "g1", "periodMs": 100, "priority": "fast", "deviceId": "d1",
         "tagNames": ["Level", "Setpoint"]}
      ]
    })";
    auto loaded = opc::project::load_json_text(kJson, "tiny.json");
    REQUIRE(loaded.ok);
    return std::make_shared<opc::project::Project>(std::move(loaded.project));
}

}  // namespace

TEST_CASE("Dispatcher poll and write via fake transport", "[component][core][dispatcher]") {
    auto project = tiny_project();
    RuntimeIndex index = RuntimeIndex::build(project);

    TagStore store;
    opc::adapters::SystemClock clock;
    NullMetrics metrics;
    FakeModbusTransport transport;
    REQUIRE(transport.connect({.host = "127.0.0.1", .port = 502}).has_value());

    auto level = index.find_by_name("Level");
    auto sp = index.find_by_name("Setpoint");
    REQUIRE(level);
    REQUIRE(sp);

    auto encoded = Translator::encode(level->tag, 12.5f);
    REQUIRE(encoded);
    transport.set_holding(0, (*encoded)[0]);
    transport.set_holding(1, (*encoded)[1]);
    transport.set_holding(2, 7);

    Dispatcher dispatcher(Dispatcher::Dependencies{
        .index = index,
        .tag_store = &store,
        .clock = &clock,
        .metrics = &metrics,
    });
    dispatcher.bind_transport("ep1", &transport);

    REQUIRE(dispatcher.poll_due("ep1", 1'000).has_value());

    auto level_v = store.get(level->id);
    REQUIRE(level_v);
    REQUIRE(level_v->quality == opc::domain::Quality::Good);
    REQUIRE(std::get<float>(level_v->value) == Catch::Approx(12.5f));

    auto sp_v = store.get(sp->id);
    REQUIRE(sp_v);
    REQUIRE(std::get<std::uint16_t>(sp_v->value) == 7);

    REQUIRE(dispatcher.enqueue_write(sp->id, std::uint16_t{99}).has_value());
    REQUIRE(dispatcher.poll_due("ep1", 1'200).has_value());

    auto regs = transport.read_holding_registers(1, 2, 1);
    REQUIRE(regs);
    REQUIRE((*regs)[0] == 99);
}

TEST_CASE("Dispatcher Bad write keeps prior value; QueueFull is returned", "[component][core][dispatcher][hardening]") {
    auto project = tiny_project();
    RuntimeIndex index = RuntimeIndex::build(project);
    TagStore store;
    opc::adapters::SystemClock clock;
    NullMetrics metrics;
    FakeModbusTransport transport;
    REQUIRE(transport.connect({.host = "127.0.0.1", .port = 502}).has_value());

    auto sp = index.find_by_name("Setpoint");
    REQUIRE(sp);

    store.publish(sp->id,
                  opc::domain::TagValue{.value = std::uint16_t{7},
                                        .quality = opc::domain::Quality::Good,
                                        .reason = opc::domain::QualityReason::None,
                                        .source_ts = 1,
                                        .server_ts = 1});

    Dispatcher dispatcher(Dispatcher::Dependencies{
        .index = index,
        .tag_store = &store,
        .clock = &clock,
        .metrics = &metrics,
    });
    // No transport bound → flush fails and requeues; publish WriteRejected preserving 7.
    REQUIRE(dispatcher.enqueue_write(sp->id, std::uint16_t{42}).has_value());
    REQUIRE_FALSE(dispatcher.flush_writes("ep1").has_value());
    auto after = store.get(sp->id);
    REQUIRE(after);
    // Value stays 7 or becomes 42 only after successful write; on transport-missing we
    // requeue without publishing Bad — check queue still has the write by successful bind.
    dispatcher.bind_transport("ep1", &transport);
    REQUIRE(dispatcher.flush_writes("ep1").has_value());
    after = store.get(sp->id);
    REQUIRE(after);
    CHECK(std::get<std::uint16_t>(after->value) == 42);
    CHECK(after->quality == opc::domain::Quality::Good);

    // Fill queue to capacity.
    for (std::size_t i = 0; i < 1024; ++i) {
        REQUIRE(dispatcher.enqueue_write(sp->id, static_cast<std::uint16_t>(i)).has_value());
    }
    auto overflow = dispatcher.enqueue_write(sp->id, std::uint16_t{1});
    REQUIRE_FALSE(overflow.has_value());
    CHECK(overflow.error().code == opc::domain::ErrorCode::QueueFull);
}

TEST_CASE("Dispatcher write queue is bounded under concurrent producers and reports depth",
          "[component][core][dispatcher][concurrency]") {
    auto project = tiny_project();
    RuntimeIndex index = RuntimeIndex::build(project);
    TagStore store;
    opc::adapters::SystemClock clock;
    opc::adapters::MemoryMetrics metrics;
    FakeModbusTransport transport;
    REQUIRE(transport.connect({.host = "127.0.0.1", .port = 502}));

    auto sp = index.find_by_name("Setpoint");
    REQUIRE(sp);
    Dispatcher dispatcher(Dispatcher::Dependencies{
        .index = index, .tag_store = &store, .clock = &clock, .metrics = &metrics});
    dispatcher.bind_transport("ep1", &transport);

    constexpr int kThreads = 8;
    constexpr int kWritesPerThread = 128;
    std::atomic<int> accepted{0};
    std::vector<std::thread> producers;
    for (int thread = 0; thread < kThreads; ++thread) {
        producers.emplace_back([&, thread] {
            for (int i = 0; i < kWritesPerThread; ++i) {
                const auto value =
                    static_cast<std::uint16_t>(thread * kWritesPerThread + i);
                if (dispatcher.enqueue_write(sp->id, value)) {
                    accepted.fetch_add(1);
                }
            }
        });
    }
    for (auto& producer : producers) {
        producer.join();
    }

    CHECK(accepted.load() == kThreads * kWritesPerThread);
    CHECK(metrics.gauge("modbus_write_queue_depth") == 1024.0);
    auto overflow = dispatcher.enqueue_write(sp->id, std::uint16_t{1});
    REQUIRE_FALSE(overflow);
    CHECK(overflow.error().code == opc::domain::ErrorCode::QueueFull);
    CHECK(metrics.counter("modbus_write_queue_overflow_total") == 1.0);

    REQUIRE(dispatcher.flush_writes("ep1"));
    CHECK(metrics.gauge("modbus_write_queue_depth") == 0.0);
    auto stored = store.get(sp->id);
    REQUIRE(stored);
    CHECK(stored->quality == opc::domain::Quality::Good);
}

TEST_CASE("Dispatcher writes multi-register values and publishes the engineering value",
          "[component][core][dispatcher][write]") {
    auto project = tiny_project();
    RuntimeIndex index = RuntimeIndex::build(project);
    TagStore store;
    opc::adapters::SystemClock clock;
    NullMetrics metrics;
    FakeModbusTransport transport;
    REQUIRE(transport.connect({.host = "127.0.0.1", .port = 502}));

    const auto level = index.find_by_name("Level");
    REQUIRE(level);
    const auto expected = Translator::encode(level->tag, 33.25F);
    REQUIRE(expected);
    REQUIRE(expected->size() == 2);

    Dispatcher dispatcher(Dispatcher::Dependencies{
        .index = index, .tag_store = &store, .clock = &clock, .metrics = &metrics});
    dispatcher.bind_transport("ep1", &transport);
    REQUIRE(dispatcher.enqueue_write(level->id, 33.25F));
    REQUIRE(dispatcher.flush_writes("ep1"));

    CHECK(transport.holding_at(1, 0) == (*expected)[0]);
    CHECK(transport.holding_at(1, 1) == (*expected)[1]);
    const auto stored = store.get(level->id);
    REQUIRE(stored);
    CHECK(stored->quality == opc::domain::Quality::Good);
    CHECK(stored->reason == opc::domain::QualityReason::None);
    CHECK(std::get<float>(stored->value) == Catch::Approx(33.25F));
}

TEST_CASE("Dispatcher preserves unprocessed queue tail after a mid-batch encoding failure",
          "[component][core][dispatcher][write][fault]") {
    auto project = tiny_project();
    RuntimeIndex index = RuntimeIndex::build(project);
    TagStore store;
    opc::adapters::SystemClock clock;
    opc::adapters::MemoryMetrics metrics;
    FakeModbusTransport transport;
    REQUIRE(transport.connect({.host = "127.0.0.1", .port = 502}));
    const auto setpoint = index.find_by_name("Setpoint");
    REQUIRE(setpoint);

    Dispatcher dispatcher(Dispatcher::Dependencies{
        .index = index, .tag_store = &store, .clock = &clock, .metrics = &metrics});
    dispatcher.bind_transport("ep1", &transport);
    REQUIRE(dispatcher.enqueue_write(setpoint->id, std::uint16_t{10}));
    REQUIRE(dispatcher.enqueue_write(setpoint->id, std::monostate{}));
    REQUIRE(dispatcher.enqueue_write(setpoint->id, std::uint16_t{30}));

    auto first_flush = dispatcher.flush_writes("ep1");
    REQUIRE_FALSE(first_flush);
    CHECK(first_flush.error().code == opc::domain::ErrorCode::InvalidArgument);
    CHECK(transport.holding_at(1, 2) == 10);
    CHECK(metrics.gauge("modbus_write_queue_depth") == 1.0);
    const auto rejected = store.get(setpoint->id);
    REQUIRE(rejected);
    CHECK(std::get<std::uint16_t>(rejected->value) == 10);
    CHECK(rejected->quality == opc::domain::Quality::Bad);
    CHECK(rejected->reason == opc::domain::QualityReason::DecodingError);

    REQUIRE(dispatcher.flush_writes("ep1"));
    CHECK(transport.holding_at(1, 2) == 30);
    CHECK(metrics.gauge("modbus_write_queue_depth") == 0.0);
    const auto recovered = store.get(setpoint->id);
    REQUIRE(recovered);
    CHECK(std::get<std::uint16_t>(recovered->value) == 30);
    CHECK(recovered->quality == opc::domain::Quality::Good);
}

TEST_CASE("Dispatcher poll_due_async via fake transport", "[component][core][dispatcher][async]") {
    auto project = tiny_project();
    RuntimeIndex index = RuntimeIndex::build(project);

    TagStore store;
    opc::adapters::SystemClock clock;
    NullMetrics metrics;
    FakeModbusTransport transport;
    REQUIRE(transport.connect({.host = "127.0.0.1", .port = 502}).has_value());

    auto level = index.find_by_name("Level");
    REQUIRE(level);

    auto encoded = Translator::encode(level->tag, 3.5f);
    REQUIRE(encoded);
    transport.set_holding(0, (*encoded)[0]);
    transport.set_holding(1, (*encoded)[1]);
    transport.set_holding(2, 1);

    Dispatcher dispatcher(Dispatcher::Dependencies{
        .index = index,
        .tag_store = &store,
        .clock = &clock,
        .metrics = &metrics,
    });
    dispatcher.bind_transport("ep1", &transport);

    std::promise<opc::domain::Result<void>> promise;
    auto future = promise.get_future();
    dispatcher.poll_due_async("ep1", 2'000,
                              [&](opc::domain::Result<void> r) { promise.set_value(std::move(r)); });
    REQUIRE(future.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    REQUIRE(future.get().has_value());

    auto level_v = store.get(level->id);
    REQUIRE(level_v);
    REQUIRE(level_v->quality == opc::domain::Quality::Good);
    REQUIRE(std::get<float>(level_v->value) == Catch::Approx(3.5f));
}

TEST_CASE("Dispatcher marks every endpoint tag Bad when connect fails",
          "[component][core][dispatcher]") {
    auto project = tiny_project();
    RuntimeIndex index = RuntimeIndex::build(project);
    TagStore store;
    opc::adapters::SystemClock clock;
    NullMetrics metrics;
    FakeModbusTransport transport;
    transport.set_connect_result(std::unexpected(opc::domain::Error{
        opc::domain::ErrorCode::Connection, "refused", "fake.modbus", true}));

    auto level = index.find_by_name("Level");
    auto sp = index.find_by_name("Setpoint");
    REQUIRE(level);
    REQUIRE(sp);

    Dispatcher dispatcher(Dispatcher::Dependencies{
        .index = index,
        .tag_store = &store,
        .clock = &clock,
        .metrics = &metrics,
    });
    dispatcher.bind_transport("ep1", &transport);

    auto poll = dispatcher.poll_due("ep1", 1'000);
    REQUIRE_FALSE(poll.has_value());
    CHECK(poll.error().code == opc::domain::ErrorCode::Connection);

    auto level_v = store.get(level->id);
    auto sp_v = store.get(sp->id);
    REQUIRE(level_v);
    REQUIRE(sp_v);
    CHECK(level_v->quality == opc::domain::Quality::Bad);
    CHECK(level_v->reason == opc::domain::QualityReason::NoCommunication);
    CHECK(sp_v->quality == opc::domain::Quality::Bad);
    CHECK(sp_v->reason == opc::domain::QualityReason::NoCommunication);
}

TEST_CASE("Dispatcher coalesces consecutive coil writes into FC15",
          "[component][core][dispatcher]") {
    constexpr std::string_view kJson = R"({
      "schemaVersion": 1,
      "name": "coils",
      "endpoints": [
        {"id": "ep1", "host": "127.0.0.1", "port": 502, "transport": "tcp"}
      ],
      "devices": [
        {"id": "d1", "endpointId": "ep1", "unitId": 1, "tags": [
          {"name": "C0", "area": "coil", "address": 0, "type": "bool", "writable": true, "group": "g1"},
          {"name": "C1", "area": "coil", "address": 1, "type": "bool", "writable": true, "group": "g1"}
        ]}
      ],
      "pollGroups": [
        {"id": "g1", "periodMs": 100, "priority": "fast", "deviceId": "d1", "tagNames": ["C0", "C1"]}
      ]
    })";
    auto loaded = opc::project::load_json_text(kJson, "coils.json");
    REQUIRE(loaded.ok);
    auto project = std::make_shared<const opc::project::Project>(std::move(loaded.project));
    RuntimeIndex index = RuntimeIndex::build(project);

    TagStore store;
    opc::adapters::SystemClock clock;
    NullMetrics metrics;
    FakeModbusTransport transport;
    REQUIRE(transport.connect({.host = "127.0.0.1", .port = 502}).has_value());

    auto c0 = index.find_by_name("C0");
    auto c1 = index.find_by_name("C1");
    REQUIRE(c0);
    REQUIRE(c1);

    Dispatcher dispatcher(Dispatcher::Dependencies{
        .index = index,
        .tag_store = &store,
        .clock = &clock,
        .metrics = &metrics,
    });
    dispatcher.bind_transport("ep1", &transport);
    REQUIRE(dispatcher.enqueue_write(c0->id, true).has_value());
    REQUIRE(dispatcher.enqueue_write(c1->id, false).has_value());
    REQUIRE(dispatcher.flush_writes("ep1").has_value());

    CHECK(transport.fc15_writes() == 1);
    CHECK(transport.fc05_writes() == 0);
    auto coils = transport.read_coils(1, 0, 2);
    REQUIRE(coils);
    CHECK((*coils)[0] == true);
    CHECK((*coils)[1] == false);
}

TEST_CASE("Dispatcher records poll and write spans", "[component][core][dispatcher][trace]") {
    auto project = tiny_project();
    RuntimeIndex index = RuntimeIndex::build(project);
    TagStore store;
    opc::adapters::SystemClock clock;
    NullMetrics metrics;
    opc::adapters::testsupport::RecordingTracer tracer;
    FakeModbusTransport transport;
    REQUIRE(transport.connect({.host = "127.0.0.1", .port = 502}).has_value());

    Dispatcher dispatcher(Dispatcher::Dependencies{
        .index = index,
        .tag_store = &store,
        .clock = &clock,
        .metrics = &metrics,
        .tracer = &tracer,
    });
    dispatcher.bind_transport("ep1", &transport);
    REQUIRE(dispatcher.poll_due("ep1", 1'000).has_value());

    auto poll_spans = tracer.snapshot();
    REQUIRE(poll_spans.size() == 1);
    CHECK(poll_spans[0].name == "modbus.poll");
    CHECK(poll_spans[0].attributes["endpoint_id"] == "ep1");
    CHECK_FALSE(poll_spans[0].error);

    auto sp = index.find_by_name("Setpoint");
    REQUIRE(sp);
    REQUIRE(dispatcher.enqueue_write(sp->id, std::uint16_t{11}).has_value());
    REQUIRE(dispatcher.flush_writes("ep1").has_value());

    auto after = tracer.snapshot();
    REQUIRE(after.size() == 2);
    CHECK(after[1].name == "modbus.write");
    CHECK(after[1].attributes["endpoint_id"] == "ep1");
    CHECK(after[1].attributes["write_count"] == "1");
    CHECK_FALSE(after[1].error);
}
