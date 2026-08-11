#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "adapters/historian_replay.hpp"
#include "adapters/memory_metrics.hpp"
#include "adapters/ring_historian.hpp"
#include "adapters/sqlite_historian.hpp"
#include "adapters/manual_clock.hpp"
#include "adapters/testsupport/fake_modbus_transport.hpp"
#include "app/cli_options.hpp"
#include "app/server_runtime.hpp"
#include "core/tag_store.hpp"
#include "ports/i_log.hpp"
#include "project/load.hpp"

#include <filesystem>
#include <memory>

using opc::adapters::MemoryMetrics;
using opc::adapters::RingHistorian;
using opc::adapters::SqliteHistorian;
using opc::domain::Quality;
using opc::domain::TagValue;

namespace {

TagValue make_good(float v, opc::domain::TimestampMs ts) {
    TagValue tv;
    tv.value = v;
    tv.quality = Quality::Good;
    tv.server_ts = ts;
    tv.source_ts = ts;
    tv.epoch = 1;
    return tv;
}

std::shared_ptr<const opc::project::Project> tiny_project() {
    constexpr std::string_view kJson = R"({
      "schemaVersion": 1,
      "name": "hist-demo",
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
    auto loaded = opc::project::load_json_text(kJson, "hist.json");
    REQUIRE(loaded.ok);
    return std::make_shared<opc::project::Project>(std::move(loaded.project));
}

}  // namespace

TEST_CASE("RingHistorian rings and counts drops", "[adapters][historian]") {
    MemoryMetrics metrics;
    RingHistorian hist(3, &metrics);

    hist.record(1, make_good(1.f, 10));
    hist.record(1, make_good(2.f, 20));
    hist.record(1, make_good(3.f, 30));
    REQUIRE(hist.size() == 3);
    REQUIRE(hist.dropped() == 0);

    hist.record(1, make_good(4.f, 40));
    REQUIRE(hist.size() == 3);
    REQUIRE(hist.dropped() == 1);
    REQUIRE(metrics.counter("historian.dropped") == Catch::Approx(1.0));

    auto recent = hist.recent(10);
    REQUIRE(recent.size() == 3);
    REQUIRE(std::get<float>(recent[0].value.value) == Catch::Approx(4.f));
    REQUIRE(std::get<float>(recent[2].value.value) == Catch::Approx(2.f));
}

TEST_CASE("Historian replay restores TagStore chronologically", "[adapters][historian][replay]") {
    RingHistorian hist(8);
    hist.record(7, make_good(1.f, 1));
    hist.record(7, make_good(2.f, 2));
    hist.record(7, make_good(3.f, 3));

    opc::core::TagStore store;
    opc::adapters::replay_recent(store, hist, 10);
    auto got = store.get(7);
    REQUIRE(got);
    REQUIRE(std::get<float>(got->value) == Catch::Approx(3.f));
    REQUIRE(got->server_ts == 3);
}

TEST_CASE("SqliteHistorian flushes hot samples to cold storage", "[adapters][historian][sqlite]") {
    const auto db_path =
        (std::filesystem::temp_directory_path() / "opc_historian_test.sqlite").string();
    std::filesystem::remove(db_path);

    {
        SqliteHistorian hist(db_path, 16);
        REQUIRE_FALSE(hist.open_error().has_value());
        hist.record(2, make_good(11.5f, 100));
        hist.record(2, make_good(12.5f, 200));
        REQUIRE(hist.flush());
        auto cold = hist.load_cold(10);
        REQUIRE(cold);
        REQUIRE(cold->size() == 2);
        REQUIRE((*cold)[0].id == 2);
        REQUIRE(std::get<float>((*cold)[1].value.value) == Catch::Approx(12.5f));
    }

    SqliteHistorian reopened(db_path, 16);
    REQUIRE_FALSE(reopened.open_error().has_value());
    auto cold = reopened.load_cold(10);
    REQUIRE(cold);
    REQUIRE(cold->size() == 2);
    std::filesystem::remove(db_path);
}

TEST_CASE("ServerRuntime subscribes historian to TagStore", "[app][historian]") {
    auto project = tiny_project();
    opc::adapters::ManualClock clock{1000};
    MemoryMetrics metrics;
    opc::ports::NullLog log;
    RingHistorian historian(64, &metrics);

    auto runtime = opc::app::ServerRuntime::create(opc::app::ServerRuntimeDeps{
        .project = project,
        .clock = &clock,
        .metrics = &metrics,
        .log = &log,
        .historian = &historian,
        .transport_factory =
            [](const opc::project::Endpoint&) -> std::unique_ptr<opc::ports::IModbusTransport> {
                auto t = std::make_unique<opc::adapters::testsupport::FakeModbusTransport>();
                (void)t->connect({.host = "127.0.0.1", .port = 1502});
                return t;
            },
    });
    REQUIRE(runtime);
    REQUIRE((*runtime)->start());

    TagValue v = make_good(42.f, 1234);
    (*runtime)->tag_store().publish(1, v);
    REQUIRE(historian.size() >= 1);
    auto recent = historian.recent(1);
    REQUIRE_FALSE(recent.empty());
    REQUIRE(std::get<float>(recent[0].value.value) == Catch::Approx(42.f));
}

TEST_CASE("parse_cli historian and frame-log flags", "[app][cli]") {
    const char* argv[] = {"OPC_SERVER",
                          "--no-historian",
                          "--historian-capacity",
                          "128",
                          "--historian-db",
                          "/tmp/h.sqlite",
                          "--frame-log",
                          "/tmp/frames.log"};
    auto opts = opc::app::parse_cli(8, argv);
    REQUIRE(opts.errors.empty());
    CHECK_FALSE(opts.enable_historian);
    CHECK(opts.historian_capacity == 128);
    CHECK(opts.historian_db == "/tmp/h.sqlite");
    CHECK(opts.frame_log_path == "/tmp/frames.log");
}
