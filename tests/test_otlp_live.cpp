#include <catch2/catch_test_macros.hpp>

#include "adapters/otel_metrics.hpp"
#include "adapters/otel_tracer.hpp"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>

namespace {

[[nodiscard]] bool env_truthy(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr && value[0] != '\0' && value[0] != '0' &&
           std::string(value) != "false" && std::string(value) != "FALSE";
}

[[nodiscard]] std::string env_or(const char* name, const char* fallback) {
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') {
        return fallback;
    }
    return value;
}

[[nodiscard]] bool file_contains(const std::string& path, const std::string& needle) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    const std::string content((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());
    return content.find(needle) != std::string::npos;
}

[[nodiscard]] bool wait_for_file_contains(const std::string& path,
                                          const std::string& needle,
                                          std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (file_contains(path, needle)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    return file_contains(path, needle);
}

}  // namespace

#ifdef OPC_WITH_OTLP
TEST_CASE("live OTLP/HTTP export reaches collector", "[adapters][otel][otlp][live]") {
    if (!env_truthy("OPC_OTLP_SMOKE")) {
        SKIP("set OPC_OTLP_SMOKE=1 with a live OTLP/HTTP collector on OPC_OTLP_ENDPOINT");
    }

    REQUIRE(opc::adapters::otlp_metrics_supported());
    REQUIRE(opc::adapters::otlp_traces_supported());

    const auto endpoint = env_or("OPC_OTLP_ENDPOINT", "http://127.0.0.1:4318/v1/metrics");
    const auto mode = env_or("OPC_OTLP_MODE", "python");

    opc::adapters::OtelMetrics metrics({
        .export_mode = opc::adapters::MetricsExportMode::OtlpHttp,
        .otlp_endpoint = endpoint,
        .export_interval_ms = 800,
        .service_name = "opc-otlp-smoke",
        .service_version = "ci",
    });
    REQUIRE(metrics.ok());
    metrics.counter_add("otlp_smoke_counter", 1.0);
    metrics.gauge_set("otlp_smoke_gauge", 7.0);
    metrics.histogram_observe("otlp_smoke_hist_ms", 3.5);
    metrics.force_flush();

    opc::adapters::OtelTracer tracer({
        .export_mode = opc::adapters::TracesExportMode::OtlpHttp,
        .otlp_endpoint = endpoint,
        .service_name = "opc-otlp-smoke",
        .service_version = "ci",
    });
    REQUIRE(tracer.ok());
    {
        auto span = tracer.start_span("otlp.smoke");
        span->set_attribute("smoke", "true");
        span->set_attribute("mode", mode);
    }
    tracer.force_flush();

    // Give the collector a moment to flush file exporters / receipt lines.
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    if (mode == "docker") {
        const auto metrics_file = env_or("OPC_OTLP_METRICS_FILE", "");
        const auto traces_file = env_or("OPC_OTLP_TRACES_FILE", "");
        REQUIRE_FALSE(metrics_file.empty());
        REQUIRE_FALSE(traces_file.empty());
        REQUIRE(wait_for_file_contains(metrics_file, "opc-otlp-smoke", std::chrono::seconds(15)));
        REQUIRE(wait_for_file_contains(metrics_file, "otlp_smoke_counter", std::chrono::seconds(5)));
        REQUIRE(wait_for_file_contains(traces_file, "opc-otlp-smoke", std::chrono::seconds(15)));
        REQUIRE(wait_for_file_contains(traces_file, "otlp.smoke", std::chrono::seconds(5)));
    } else {
        const auto receipt = env_or("OPC_OTLP_RECEIPT", "");
        REQUIRE_FALSE(receipt.empty());
        REQUIRE(wait_for_file_contains(receipt, "\"signal\": \"metrics\"", std::chrono::seconds(15)));
        REQUIRE(wait_for_file_contains(receipt, "\"signal\": \"traces\"", std::chrono::seconds(15)));
    }
}
#else
TEST_CASE("live OTLP smoke skipped without OPC_WITH_OTLP", "[adapters][otel][otlp][live]") {
    if (!env_truthy("OPC_OTLP_SMOKE")) {
        SKIP("OTLP not compiled; live smoke needs OPC_WITH_OTLP=ON and OPC_OTLP_SMOKE=1");
    }
    FAIL("OPC_OTLP_SMOKE=1 requires a build with -DOPC_WITH_OTLP=ON");
}
#endif
