#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "adapters/manual_clock.hpp"
#include "adapters/system_clock.hpp"
#include "adapters/testsupport/fake_modbus_transport.hpp"
#include "app/cli_options.hpp"
#include "app/server_runtime.hpp"
#include "core/translator.hpp"
#include "ports/i_log.hpp"
#include "ports/i_metrics.hpp"
#include "project/load.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <sstream>
#include <string_view>
#include <thread>
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

namespace {

std::shared_ptr<const opc::project::Project> two_endpoint_project() {
    constexpr std::string_view kJson = R"({
      "schemaVersion": 1,
      "name": "iso",
      "endpoints": [
        {"id": "ep-slow", "host": "127.0.0.1", "port": 1502, "transport": "tcp",
         "reconnectDelayMs": 2000},
        {"id": "ep-fast", "host": "127.0.0.1", "port": 1503, "transport": "tcp",
         "reconnectDelayMs": 2000}
      ],
      "devices": [
        {"id": "d-slow", "endpointId": "ep-slow", "unitId": 1, "tags": [
          {"name": "SlowTemp", "area": "holding", "address": 0, "type": "float32",
           "byteOrder": "ABCD", "group": "g-slow"}
        ]},
        {"id": "d-fast", "endpointId": "ep-fast", "unitId": 1, "tags": [
          {"name": "FastTemp", "area": "holding", "address": 0, "type": "float32",
           "byteOrder": "ABCD", "group": "g-fast"}
        ]}
      ],
      "pollGroups": [
        {"id": "g-slow", "periodMs": 20, "priority": "fast", "deviceId": "d-slow",
         "tagNames": ["SlowTemp"]},
        {"id": "g-fast", "periodMs": 20, "priority": "fast", "deviceId": "d-fast",
         "tagNames": ["FastTemp"]}
      ]
    })";
    auto loaded = opc::project::load_json_text(kJson, "iso.json");
    REQUIRE(loaded.ok);
    return std::make_shared<opc::project::Project>(std::move(loaded.project));
}

void seed_float(FakeModbusTransport& fake, const opc::project::Tag& tag, float value) {
    auto encoded = Translator::encode(tag, value);
    REQUIRE(encoded);
    REQUIRE(encoded->size() >= 2);
    fake.set_holding(0, (*encoded)[0]);
    fake.set_holding(1, (*encoded)[1]);
}

}  // namespace

TEST_CASE("Asio reactor keeps a fast endpoint moving while another is slow",
          "[app][runtime][asio]") {
    using namespace std::chrono_literals;
    auto project = two_endpoint_project();
    opc::adapters::SystemClock clock;
    NullMetrics metrics;
    NullLog log;

    std::unordered_map<std::string, FakeModbusTransport*> fakes;
    auto runtime = ServerRuntime::create(ServerRuntimeDeps{
        .project = project,
        .clock = &clock,
        .metrics = &metrics,
        .log = &log,
        .transport_factory =
            [&](const opc::project::Endpoint& endpoint)
                -> std::unique_ptr<opc::ports::IModbusTransport> {
                auto t = std::make_unique<FakeModbusTransport>();
                if (endpoint.id == "ep-slow") {
                    t->set_read_delay(250ms);
                    seed_float(*t, project->devices[0].tags[0], 1.0f);
                } else {
                    seed_float(*t, project->devices[1].tags[0], 9.5f);
                }
                fakes[endpoint.id] = t.get();
                return t;
            },
    });
    REQUIRE(runtime);

    std::atomic<bool> fast_good{false};
    auto fast = (*runtime)->index().find_by_name("FastTemp");
    REQUIRE(fast);
    (*runtime)->tag_store().subscribe(
        [&](opc::domain::TagId id, const opc::domain::TagValue& value) {
            if (id == fast->id && value.quality == opc::domain::Quality::Good) {
                fast_good = true;
            }
        });

    REQUIRE((*runtime)->start());
    REQUIRE((*runtime)->start_reactor());
    REQUIRE((*runtime)->reactor_running());

    const auto t0 = std::chrono::steady_clock::now();
    while (!fast_good.load() && std::chrono::steady_clock::now() - t0 < 100ms) {
        std::this_thread::sleep_for(2ms);
    }
    const auto elapsed = std::chrono::steady_clock::now() - t0;
    (*runtime)->stop();

    REQUIRE(fast_good.load());
    REQUIRE(elapsed < 100ms);
}

TEST_CASE("Asio reactor honors reconnectDelayMs instead of hammering connect",
          "[app][runtime][asio]") {
    using namespace std::chrono_literals;
    constexpr std::string_view kJson = R"({
      "schemaVersion": 1,
      "name": "backoff",
      "endpoints": [
        {"id": "ep1", "host": "127.0.0.1", "port": 1502, "transport": "tcp",
         "reconnectDelayMs": 400}
      ],
      "devices": [
        {"id": "d1", "endpointId": "ep1", "unitId": 1, "tags": [
          {"name": "Temp", "area": "holding", "address": 0, "type": "uint16", "group": "g1"}
        ]}
      ],
      "pollGroups": [
        {"id": "g1", "periodMs": 20, "priority": "fast", "deviceId": "d1", "tagNames": ["Temp"]}
      ]
    })";
    auto loaded = opc::project::load_json_text(kJson, "backoff.json");
    REQUIRE(loaded.ok);
    auto project = std::make_shared<opc::project::Project>(std::move(loaded.project));

    opc::adapters::SystemClock clock;
    NullMetrics metrics;
    NullLog log;
    FakeModbusTransport* fake = nullptr;

    auto runtime = ServerRuntime::create(ServerRuntimeDeps{
        .project = project,
        .clock = &clock,
        .metrics = &metrics,
        .log = &log,
        .transport_factory =
            [&](const opc::project::Endpoint&) -> std::unique_ptr<opc::ports::IModbusTransport> {
                auto t = std::make_unique<FakeModbusTransport>();
                t->set_connect_result(std::unexpected(opc::domain::Error{
                    opc::domain::ErrorCode::Connection, "down", "fake.modbus", true}));
                fake = t.get();
                return t;
            },
    });
    REQUIRE(runtime);
    REQUIRE((*runtime)->start());
    REQUIRE(fake != nullptr);
    const int after_start = fake->connect_attempts();
    REQUIRE(after_start >= 1);

    auto binding = (*runtime)->index().find_by_name("Temp");
    REQUIRE(binding);
    auto value = (*runtime)->tag_store().get(binding->id);
    REQUIRE(value);
    CHECK(value->quality == opc::domain::Quality::Bad);
    CHECK(value->reason == opc::domain::QualityReason::NoCommunication);

    REQUIRE((*runtime)->start_reactor());
    std::this_thread::sleep_for(150ms);
    const int during_backoff = fake->connect_attempts();
    (*runtime)->stop();

    CHECK(during_backoff <= after_start + 1);
    CHECK(during_backoff < 5);
}
