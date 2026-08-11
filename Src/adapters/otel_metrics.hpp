#pragma once

#include "ports/i_metrics.hpp"

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace opc::adapters {

enum class MetricsExportMode {
    None,     // instruments recorded but no periodic export (tests / --metrics-export none)
    OStream,  // dump to stdout periodically
    OtlpHttp, // OTLP/HTTP (requires OPC_WITH_OTLP build)
};

struct OtelMetricsOptions {
    MetricsExportMode export_mode{MetricsExportMode::OStream};
    std::string otlp_endpoint;  // e.g. http://127.0.0.1:4318/v1/metrics
    int export_interval_ms{1000};
    std::string service_name{"opc-server"};
    std::string service_version{"0.1.0"};
};

/// OpenTelemetry metrics adapter for `IMetrics` (ADR-0008).
class OtelMetrics final : public ports::IMetrics {
public:
    explicit OtelMetrics(OtelMetricsOptions options = {});
    ~OtelMetrics() override;

    OtelMetrics(const OtelMetrics&) = delete;
    OtelMetrics& operator=(const OtelMetrics&) = delete;

    void counter_add(std::string_view name, double value = 1.0) override;
    void gauge_set(std::string_view name, double value) override;
    void histogram_observe(std::string_view name, double value) override;

    void force_flush();

    [[nodiscard]] bool ok() const { return ok_; }
    [[nodiscard]] const std::string& init_error() const { return init_error_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool ok_{false};
    std::string init_error_;
    mutable std::mutex mutex_;
};

[[nodiscard]] bool otlp_metrics_supported();

}  // namespace opc::adapters
