#include <catch2/catch_test_macros.hpp>

#include "adapters/manual_clock.hpp"
#include "adapters/testsupport/fake_modbus_transport.hpp"
#include "app/cli_options.hpp"
#include "app/server_runtime.hpp"
#include "core/runtime_doctor.hpp"
#include "core/runtime_index.hpp"
#include "core/tag_store.hpp"
#include "ports/i_log.hpp"
#include "ports/i_metrics.hpp"
#include "project/load.hpp"

#include <memory>
#include <string_view>

using opc::adapters::ManualClock;
using opc::adapters::testsupport::FakeModbusTransport;
using opc::app::ServerRuntime;
using opc::app::ServerRuntimeDeps;
using opc::ports::NullLog;
using opc::ports::NullMetrics;

namespace {

std::shared_ptr<const opc::project::Project> sample_project() {
    constexpr std::string_view kJson = R"({
      "schemaVersion": 1,
      "name": "doctor-rt",
      "endpoints": [
        {"id": "ep1", "host": "127.0.0.1", "port": 1502, "transport": "tcp"}
      ],
      "devices": [
        {"id": "d1", "endpointId": "ep1", "unitId": 1, "tags": [
          {"name": "Temp", "area": "holding", "address": 0, "type": "uint16",
           "byteOrder": "AB", "group": "g1"},
          {"name": "Status", "area": "holding", "address": 1, "type": "uint16",
           "byteOrder": "AB", "group": "g1"}
        ]}
      ],
      "pollGroups": [
        {"id": "g1", "periodMs": 50, "priority": "fast", "deviceId": "d1",
         "tagNames": ["Temp", "Status"]}
      ]
    })";
    auto loaded = opc::project::load_json_text(kJson, "doctor-rt.json");
    REQUIRE(loaded.ok);
    return std::make_shared<opc::project::Project>(std::move(loaded.project));
}

}  // namespace

TEST_CASE("parse_cli recognizes --runtime-doctor", "[app][cli]") {
    const char* argv[] = {"OPC_SERVER", "--once", "--runtime-doctor", "--no-opcua"};
    auto opts = opc::app::parse_cli(4, argv);
    REQUIRE(opts.errors.empty());
    CHECK(opts.once);
    CHECK(opts.runtime_doctor);
    CHECK_FALSE(opts.enable_opcua);
}

TEST_CASE("runtime doctor flags missing and never-Good tags", "[core][runtime-doctor]") {
    auto project = sample_project();
    auto index = opc::core::RuntimeIndex::build(project);
    opc::core::TagStore store;

    auto missing = opc::core::runtime_doctor(index, store);
    REQUIRE(missing.error_count == 2);
    CHECK(missing.findings.front().message.find("never published") != std::string::npos);

    auto temp = index.find_by_name("Temp");
    auto status = index.find_by_name("Status");
    REQUIRE(temp);
    REQUIRE(status);

    opc::domain::TagValue good;
    good.quality = opc::domain::Quality::Good;
    good.reason = opc::domain::QualityReason::None;
    store.publish(temp->id, good);

    opc::domain::TagValue bad;
    bad.quality = opc::domain::Quality::Bad;
    bad.reason = opc::domain::QualityReason::NoCommunication;
    store.publish(status->id, bad);

    auto mixed = opc::core::runtime_doctor(index, store);
    REQUIRE(mixed.error_count == 1);
    CHECK(mixed.warning_count == 0);
    CHECK(mixed.findings.front().tag_name == "Status");
    CHECK(mixed.findings.front().message.find("NoCommunication") != std::string::npos);

    opc::domain::TagValue stale;
    stale.quality = opc::domain::Quality::Uncertain;
    stale.reason = opc::domain::QualityReason::Timeout;
    store.publish(status->id, stale);
    auto warn = opc::core::runtime_doctor(index, store);
    CHECK(warn.error_count == 0);
    CHECK(warn.warning_count == 1);
    CHECK(warn.findings.front().message.find("Timeout") != std::string::npos);

    store.publish(status->id, good);
    auto ok = opc::core::runtime_doctor(index, store);
    CHECK(ok.error_count == 0);
    CHECK(ok.warning_count == 0);
    CHECK(ok.findings.empty());
}

TEST_CASE("runtime doctor reports Bad after connect fail", "[core][runtime-doctor]") {
    auto project = sample_project();
    ManualClock clock{1000};
    NullMetrics metrics;
    NullLog log;

    auto runtime = ServerRuntime::create(ServerRuntimeDeps{
        .project = project,
        .clock = &clock,
        .metrics = &metrics,
        .log = &log,
        .transport_factory =
            [](const opc::project::Endpoint&) -> std::unique_ptr<opc::ports::IModbusTransport> {
                auto t = std::make_unique<FakeModbusTransport>();
                t->set_connect_result(std::unexpected(opc::domain::Error{
                    opc::domain::ErrorCode::Connection, "down", "fake.modbus", true}));
                return t;
            },
    });
    REQUIRE(runtime);
    REQUIRE((*runtime)->start());

    const auto report = opc::core::runtime_doctor((*runtime)->index(), (*runtime)->tag_store());
    REQUIRE(report.error_count >= 2);
    bool saw_nocomm = false;
    for (const auto& finding : report.findings) {
        if (finding.message.find("NoCommunication") != std::string::npos) {
            saw_nocomm = true;
        }
    }
    CHECK(saw_nocomm);
    (*runtime)->stop();
}
