#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "adapters/system_clock.hpp"
#include "adapters/testsupport/fake_modbus_transport.hpp"
#include "core/dispatcher.hpp"
#include "core/runtime_index.hpp"
#include "core/tag_store.hpp"
#include "core/translator.hpp"
#include "ports/i_metrics.hpp"
#include "project/load.hpp"

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
           "type": "float32", "byteOrder": "ABCD", "group": "g1"},
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

TEST_CASE("Dispatcher poll and write via fake transport", "[core][dispatcher]") {
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
