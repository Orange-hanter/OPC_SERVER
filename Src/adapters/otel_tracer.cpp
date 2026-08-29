#include "adapters/otel_tracer.hpp"

#include "opentelemetry/exporters/ostream/span_exporter_factory.h"
#include "opentelemetry/sdk/resource/resource.h"
#include "opentelemetry/sdk/trace/simple_processor_factory.h"
#include "opentelemetry/sdk/trace/processor.h"
#include "opentelemetry/sdk/trace/tracer_provider.h"
#include "opentelemetry/sdk/trace/tracer_provider_factory.h"
#include "opentelemetry/trace/provider.h"
#include "opentelemetry/trace/span.h"
#include "opentelemetry/trace/span_metadata.h"
#include "opentelemetry/trace/tracer.h"

#ifdef OPC_WITH_OTLP
#include "opentelemetry/exporters/otlp/otlp_http_exporter_factory.h"
#include "opentelemetry/exporters/otlp/otlp_http_exporter_options.h"
#endif

#include <chrono>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace opc::adapters {
namespace trace_api = opentelemetry::trace;
namespace trace_sdk = opentelemetry::sdk::trace;
namespace resource = opentelemetry::sdk::resource;
namespace nostd = opentelemetry::nostd;

std::string otlp_traces_url(std::string_view metrics_or_base) {
    if (metrics_or_base.empty()) {
        return {};
    }
    std::string url{metrics_or_base};
    const auto metrics = url.find("/v1/metrics");
    if (metrics != std::string::npos) {
        return url.substr(0, metrics) + "/v1/traces";
    }
    if (url.ends_with("/v1/traces")) {
        return url;
    }
    if (url.back() == '/') {
        return url + "v1/traces";
    }
    return url + "/v1/traces";
}

bool otlp_traces_supported() {
#ifdef OPC_WITH_OTLP
    return true;
#else
    return false;
#endif
}

namespace {

class OtelSpan final : public ports::ISpan {
public:
    explicit OtelSpan(nostd::shared_ptr<trace_api::Span> span) : span_(std::move(span)) {}
    ~OtelSpan() override {
        if (span_) {
            span_->End();
        }
    }
    void set_attribute(std::string_view key, std::string_view value) override {
        if (!span_) {
            return;
        }
        span_->SetAttribute(nostd::string_view{key.data(), key.size()},
                            nostd::string_view{value.data(), value.size()});
    }
    void set_attribute(std::string_view key, std::int64_t value) override {
        if (!span_) {
            return;
        }
        span_->SetAttribute(nostd::string_view{key.data(), key.size()}, value);
    }
    void set_error(std::string_view message) override {
        if (!span_) {
            return;
        }
        span_->SetStatus(trace_api::StatusCode::kError,
                         nostd::string_view{message.data(), message.size()});
    }

private:
    nostd::shared_ptr<trace_api::Span> span_;
};

}  // namespace

struct OtelTracer::Impl {
    nostd::shared_ptr<trace_api::TracerProvider> provider;
    nostd::shared_ptr<trace_api::Tracer> tracer;
};

OtelTracer::OtelTracer(OtelTracerOptions options) : impl_(std::make_unique<Impl>()) {
    try {
        resource::ResourceAttributes attrs = {
            {"service.name", options.service_name},
            {"service.version", options.service_version},
        };
        auto resource_ptr = resource::Resource::Create(attrs);

        std::unique_ptr<trace_sdk::SpanExporter> exporter;
        switch (options.export_mode) {
        case TracesExportMode::None:
            break;
        case TracesExportMode::OStream:
            exporter = opentelemetry::exporter::trace::OStreamSpanExporterFactory::Create();
            break;
        case TracesExportMode::OtlpHttp:
#ifdef OPC_WITH_OTLP
        {
            opentelemetry::exporter::otlp::OtlpHttpExporterOptions opts;
            const auto url = otlp_traces_url(options.otlp_endpoint);
            if (!url.empty()) {
                opts.url = url;
            }
            exporter = opentelemetry::exporter::otlp::OtlpHttpExporterFactory::Create(opts);
            break;
        }
#else
            init_error_ = "OTLP traces exporter was not enabled at build time (OPC_WITH_OTLP)";
            return;
#endif
        }

        std::unique_ptr<trace_sdk::TracerProvider> u_provider;
        if (exporter) {
            auto processor = trace_sdk::SimpleSpanProcessorFactory::Create(std::move(exporter));
            u_provider = trace_sdk::TracerProviderFactory::Create(std::move(processor), resource_ptr);
        } else {
            std::vector<std::unique_ptr<trace_sdk::SpanProcessor>> none;
            u_provider = trace_sdk::TracerProviderFactory::Create(std::move(none), resource_ptr);
        }

        impl_->provider = nostd::shared_ptr<trace_api::TracerProvider>(u_provider.release());
        trace_api::Provider::SetTracerProvider(impl_->provider);
        impl_->tracer = impl_->provider->GetTracer(options.service_name, options.service_version);
        ok_ = (impl_->tracer.get() != nullptr);
        if (!ok_) {
            init_error_ = "failed to create OTel tracer";
        }
    } catch (const std::exception& ex) {
        init_error_ = ex.what();
        ok_ = false;
    }
}

OtelTracer::~OtelTracer() {
    force_flush();
    nostd::shared_ptr<trace_api::TracerProvider> none;
    trace_api::Provider::SetTracerProvider(none);
    impl_.reset();
}

void OtelTracer::force_flush() {
    if (!impl_ || !impl_->provider) {
        return;
    }
    if (auto* sdk = dynamic_cast<trace_sdk::TracerProvider*>(impl_->provider.get())) {
        (void)sdk->ForceFlush(std::chrono::milliseconds(1000));
    }
}

std::unique_ptr<ports::ISpan> OtelTracer::start_span(std::string_view name) {
    if (!ok_ || !impl_ || !impl_->tracer) {
        return std::make_unique<ports::NullSpan>();
    }
    auto span = impl_->tracer->StartSpan(std::string(name));
    return std::make_unique<OtelSpan>(std::move(span));
}

}  // namespace opc::adapters
