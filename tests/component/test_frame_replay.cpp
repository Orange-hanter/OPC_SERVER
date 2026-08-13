#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "adapters/frame_replay.hpp"
#include "adapters/manual_clock.hpp"
#include "adapters/memory_metrics.hpp"
#include "core/dispatcher.hpp"
#include "core/runtime_index.hpp"
#include "core/tag_store.hpp"
#include "project/load.hpp"

using opc::adapters::ReplayModbusTransport;
using opc::core::Dispatcher;
using opc::core::RuntimeIndex;
using opc::core::TagStore;

namespace {

std::shared_ptr<const opc::project::Project> uint16_project() {
    constexpr std::string_view kJson = R"({
      "schemaVersion": 1,
      "name": "replay",
      "endpoints": [
        {"id": "ep1", "host": "127.0.0.1", "port": 1502, "transport": "tcp"}
      ],
      "devices": [
        {"id": "d1", "endpointId": "ep1", "unitId": 1, "tags": [
          {"name": "Count", "area": "holding", "address": 0, "type": "uint16",
           "byteOrder": "AB", "group": "g1"}
        ]}
      ],
      "pollGroups": [
        {"id": "g1", "periodMs": 50, "priority": "fast", "deviceId": "d1", "tagNames": ["Count"]}
      ]
    })";
    auto loaded = opc::project::load_json_text(kJson, "replay.json");
    REQUIRE(loaded.ok);
    return std::make_shared<opc::project::Project>(std::move(loaded.project));
}

opc::ports::FrameRecord holding_read_frame(std::uint16_t value) {
    opc::ports::FrameRecord frame;
    frame.ts_ms = 1;
    frame.endpoint_id = "ep1";
    // TX MBAP + FC03 addr=0 qty=1
    frame.tx = {0x00, 0x01, 0x00, 0x00, 0x00, 0x06, 0x01, 0x03, 0x00, 0x00, 0x00, 0x01};
    // RX MBAP + unit + FC03 + 2 bytes + value
    frame.rx = {0x00, 0x01, 0x00, 0x00, 0x00, 0x05, 0x01, 0x03, 0x02,
                static_cast<std::uint8_t>(value >> 8), static_cast<std::uint8_t>(value & 0xFF)};
    return frame;
}

}  // namespace

TEST_CASE("ReplayModbusTransport returns recorded holding registers", "[component][adapters][replay]") {
    ReplayModbusTransport transport({holding_read_frame(42)});
    REQUIRE(transport.connect({.host = "127.0.0.1", .port = 1502}));
    auto regs = transport.read_holding_registers(1, 0, 1);
    REQUIRE(regs);
    REQUIRE(regs->size() == 1);
    CHECK((*regs)[0] == 42);
    CHECK(transport.remaining() == 0);
}

TEST_CASE("Dispatcher poll via frame replay", "[component][adapters][replay][dispatcher]") {
    auto project = uint16_project();
    RuntimeIndex index = RuntimeIndex::build(project);
    TagStore store;
    opc::adapters::ManualClock clock{1000};
    opc::adapters::MemoryMetrics metrics;
    ReplayModbusTransport transport({holding_read_frame(99)});
    REQUIRE(transport.connect({.host = "127.0.0.1", .port = 1502}));

    Dispatcher dispatcher(Dispatcher::Dependencies{
        .index = index,
        .tag_store = &store,
        .clock = &clock,
        .metrics = &metrics,
    });
    dispatcher.bind_transport("ep1", &transport);
    REQUIRE(dispatcher.poll_due("ep1", 1000));

    auto binding = index.find_by_name("Count");
    REQUIRE(binding);
    auto value = store.get(binding->id);
    REQUIRE(value);
    REQUIRE(std::get<std::uint16_t>(value->value) == 99);
    CHECK(metrics.counter("modbus_poll_rtt_ms.count") == Catch::Approx(1.0));
}

TEST_CASE("parse_frame_log_line rejects comments", "[component][adapters][replay]") {
    auto parsed = opc::adapters::parse_frame_log_line("# header");
    REQUIRE_FALSE(parsed);
}
