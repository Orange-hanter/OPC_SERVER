#pragma once

#include "ports/i_tracer.hpp"

#include <memory>
#include <string>

namespace opc::adapters {

enum class TracesExportMode {
    None,
    OStream,
    OtlpHttp,
};

struct OtelTracerOptions {
    TracesExportMode export_mode{TracesExportMode::None};
    std::string otlp_endpoint;  // metrics URL or collector base; rewritten to /v1/traces
    std::string service_name{"opc-server"};
    std::string service_version{"0.1.0"};
};

/// OpenTelemetry traces adapter for `ITracer` (ADR-0008 poll/write spans).
class OtelTracer final : public ports::ITracer {
public:
    explicit OtelTracer(OtelTracerOptions options = {});
    ~OtelTracer() override;

    OtelTracer(const OtelTracer&) = delete;
    OtelTracer& operator=(const OtelTracer&) = delete;

    std::unique_ptr<ports::ISpan> start_span(std::string_view name) override;

    void force_flush();

    [[nodiscard]] bool ok() const { return ok_; }
    [[nodiscard]] const std::string& init_error() const { return init_error_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool ok_{false};
    std::string init_error_;
};

[[nodiscard]] bool otlp_traces_supported();
[[nodiscard]] std::string otlp_traces_url(std::string_view metrics_or_base);

}  // namespace opc::adapters
