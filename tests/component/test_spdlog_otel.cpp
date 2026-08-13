#include <catch2/catch_test_macros.hpp>

#include "adapters/otel_metrics.hpp"
#include "adapters/spdlog_log.hpp"
#include "app/cli_options.hpp"

#include <filesystem>
#include <fstream>
#include <string>

TEST_CASE("SpdlogLog writes structured lines to file", "[component][adapters][spdlog]") {
    const auto path = (std::filesystem::temp_directory_path() / "opc_spdlog_test.log").string();
    std::filesystem::remove(path);
    {
        opc::adapters::SpdlogLog log({
            .min_level = opc::ports::LogLevel::Info,
            .log_file = path,
            .async = false,
        });
        log.info("test.component", "hello-spdlog");
    }
    REQUIRE(std::filesystem::exists(path));
    std::ifstream in(path);
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    REQUIRE(content.find("hello-spdlog") != std::string::npos);
    REQUIRE(content.find("component=test.component") != std::string::npos);
    std::filesystem::remove(path);
}

TEST_CASE("OtelMetrics records counters without export", "[component][adapters][otel]") {
    opc::adapters::OtelMetrics metrics({
        .export_mode = opc::adapters::MetricsExportMode::None,
        .service_name = "opc-test",
    });
    REQUIRE(metrics.ok());
    metrics.counter_add("modbus_poll_errors_total", 2.0);
    metrics.gauge_set("modbus_write_queue_depth", 3.0);
    metrics.histogram_observe("modbus_poll_rtt_ms", 12.5);
    metrics.force_flush();
}

TEST_CASE("parse_cli log and metrics flags", "[component][app][cli]") {
    const char* argv[] = {"OPC_SERVER",
                          "--log-level",
                          "debug",
                          "--log-file",
                          "/tmp/opc.log",
                          "--metrics-export",
                          "none",
                          "--otlp-endpoint",
                          "http://127.0.0.1:4318/v1/metrics"};
    auto opts = opc::app::parse_cli(9, argv);
    REQUIRE(opts.errors.empty());
    CHECK(opts.log_level == opc::app::LogLevelOption::Debug);
    CHECK(opts.log_file == "/tmp/opc.log");
    CHECK(opts.metrics_export == opc::app::MetricsExportOption::None);
    CHECK(opts.otlp_endpoint == "http://127.0.0.1:4318/v1/metrics");
}
