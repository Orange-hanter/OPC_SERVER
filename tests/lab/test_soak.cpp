#include <catch2/catch_test_macros.hpp>

#include "adapters/manual_clock.hpp"
#include "adapters/testsupport/fake_modbus_transport.hpp"
#include "app/server_runtime.hpp"
#include "core/translator.hpp"
#include "ports/i_log.hpp"
#include "ports/i_metrics.hpp"
#include "project/load.hpp"

#include <cstdlib>

using opc::adapters::ManualClock;
using opc::adapters::testsupport::FakeModbusTransport;
using opc::app::ServerRuntime;
using opc::app::ServerRuntimeDeps;
using opc::core::Translator;
using opc::ports::NullLog;
using opc::ports::NullMetrics;

TEST_CASE("Soak: thousands of poll cycles on fake transport stay Good", "[soak][lab]") {
    if (std::getenv("OPC_SOAK") == nullptr) {
        SKIP("set OPC_SOAK=1 to run soak cycles");
    }

    constexpr std::string_view kJson = R"({
      "schemaVersion": 1,
      "name": "soak",
      "endpoints": [
        {"id": "ep1", "host": "127.0.0.1", "port": 1502, "transport": "tcp"}
      ],
      "devices": [
        {"id": "d1", "endpointId": "ep1", "unitId": 1, "tags": [
          {"name": "Temp", "area": "holding", "address": 0, "type": "float32",
           "byteOrder": "ABCD", "group": "g1"}
        ]}
      ],
      "pollGroups": [
        {"id": "g1", "periodMs": 10, "priority": "fast", "deviceId": "d1", "tagNames": ["Temp"]}
      ]
    })";
    auto loaded = opc::project::load_json_text(kJson, "soak.json");
    REQUIRE(loaded.ok);
    auto project = std::make_shared<const opc::project::Project>(std::move(loaded.project));

    auto fake = std::make_shared<FakeModbusTransport>();
    auto encoded = Translator::encode(project->devices[0].tags[0], 21.5f);
    REQUIRE(encoded);
    REQUIRE(fake->connect({.host = "127.0.0.1", .port = 1502}));
    fake->set_holding(0, (*encoded)[0]);
    fake->set_holding(1, (*encoded)[1]);

    ManualClock clock{1};
    NullMetrics metrics;
    NullLog log;
    auto runtime = ServerRuntime::create(ServerRuntimeDeps{
        .project = project,
        .clock = &clock,
        .metrics = &metrics,
        .log = &log,
        .transport_factory =
            [fake](const opc::project::Endpoint&) {
                auto t = std::make_unique<FakeModbusTransport>();
                (void)t->connect({.host = "127.0.0.1", .port = 1502});
                t->set_holding(0, fake->holding_at(1, 0));
                t->set_holding(1, fake->holding_at(1, 1));
                return t;
            },
    });
    REQUIRE(runtime);
    REQUIRE((*runtime)->start());

    constexpr int kCycles = 5000;
    for (int i = 0; i < kCycles; ++i) {
        clock.advance_ms(1);
        REQUIRE((*runtime)->poll_once(clock.now_ms()));
    }
    auto binding = (*runtime)->index().find_by_name("Temp");
    REQUIRE(binding);
    auto value = (*runtime)->tag_store().get(binding->id);
    REQUIRE(value);
    CHECK(value->quality == opc::domain::Quality::Good);
}
