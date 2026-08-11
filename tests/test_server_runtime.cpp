#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "adapters/manual_clock.hpp"
#include "adapters/testsupport/fake_modbus_transport.hpp"
#include "app/cli_options.hpp"
#include "app/server_runtime.hpp"
#include "core/translator.hpp"
#include "ports/i_log.hpp"
#include "ports/i_metrics.hpp"
#include "project/load.hpp"

#include <memory>
#include <sstream>
#include <unordered_map>

using opc::adapters::ManualClock;
using opc::adapters::testsupport::FakeModbusTransport;
using opc::app::ServerRuntime;
using opc::app::ServerRuntimeDeps;
using opc::core::Translator;
using opc::ports::NullLog;
using opc::ports::NullMetrics;

namespace {

std::shared_ptr<const opc::project::Project> sample_project() {
    constexpr std::string_view kJson = R"({
      "schemaVersion": 1,
      "name": "infra-demo",
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
        {"id": "g1", "periodMs": 50, "priority": "fast", "deviceId": "d1", "tagNames": ["Temp"]}
      ]
    })";
    auto loaded = opc::project::load_json_text(kJson, "infra.json");
    REQUIRE(loaded.ok);
    return std::make_shared<opc::project::Project>(std::move(loaded.project));
}

}  // namespace

TEST_CASE("parse_cli recognizes project and once/watch", "[app][cli]") {
    const char* argv[] = {"OPC_SERVER", "--project", "x.json", "--once", "--watch", "--period-ms", "250"};
    auto opts = opc::app::parse_cli(7, argv);
    REQUIRE(opts.errors.empty());
    CHECK(opts.project_path == "x.json");
    CHECK(opts.once);
    CHECK(opts.watch);
    CHECK(opts.watch_period_ms == 250);
}

TEST_CASE("ServerRuntime bootstraps with injected fake transport", "[app][runtime]") {
    auto project = sample_project();
    ManualClock clock{1000};
    NullMetrics metrics;
    NullLog log;

    auto fake = std::make_shared<FakeModbusTransport>();
    auto level_tag = project->devices[0].tags[0];
    auto encoded = Translator::encode(level_tag, 21.5f);
    REQUIRE(encoded);
    REQUIRE(fake->connect({.host = "127.0.0.1", .port = 1502}));
    fake->set_holding(0, (*encoded)[0]);
    fake->set_holding(1, (*encoded)[1]);

    auto runtime = ServerRuntime::create(ServerRuntimeDeps{
        .project = project,
        .clock = &clock,
        .metrics = &metrics,
        .log = &log,
        .transport_factory =
            [fake](const opc::project::Endpoint&) -> std::unique_ptr<opc::ports::IModbusTransport> {
                // Reuse same fake instance; ServerRuntime owns unique_ptr — wrap with no-op deleter? 
                // Instead create a forwarding shim is heavy; clone state into new Fake each call.
                auto t = std::make_unique<FakeModbusTransport>();
                (void)t->connect({.host = "127.0.0.1", .port = 1502});
                // copy holding map by reading known regs
                auto regs = fake->read_holding_registers(1, 0, 2);
                if (regs) {
                    t->set_holding(0, (*regs)[0]);
                    t->set_holding(1, (*regs)[1]);
                }
                return t;
            },
    });
    REQUIRE(runtime);
    REQUIRE((*runtime)->start());
    REQUIRE((*runtime)->poll_once(clock.now_ms()));

    auto binding = (*runtime)->index().find_by_name("Temp");
    REQUIRE(binding);
    auto value = (*runtime)->tag_store().get(binding->id);
    REQUIRE(value);
    REQUIRE(value->quality == opc::domain::Quality::Good);
    REQUIRE(std::get<float>(value->value) == Catch::Approx(21.5f));

    std::ostringstream watch;
    (*runtime)->write_watchlist(watch);
    const auto text = watch.str();
    REQUIRE(text.find("Temp") != std::string::npos);
    REQUIRE(text.find("Good") != std::string::npos);
}
