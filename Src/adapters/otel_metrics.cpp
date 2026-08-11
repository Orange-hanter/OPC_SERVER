#include "adapters/otel_metrics.hpp"

#include "opentelemetry/context/context.h"
#include "opentelemetry/exporters/ostream/metric_exporter_factory.h"
#include "opentelemetry/metrics/meter.h"
#include "opentelemetry/metrics/provider.h"
#include "opentelemetry/nostd/shared_ptr.h"
#include "opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_factory.h"
#include "opentelemetry/sdk/metrics/meter_provider.h"
#include "opentelemetry/sdk/metrics/meter_provider_factory.h"
#include "opentelemetry/sdk/metrics/push_metric_exporter.h"
#include "opentelemetry/sdk/metrics/view/view_registry.h"
#include "opentelemetry/sdk/resource/resource.h"

#ifdef OPC_WITH_OTLP
#include "opentelemetry/exporters/otlp/otlp_http_metric_exporter_factory.h"
#include "opentelemetry/exporters/otlp/otlp_http_metric_exporter_options.h"
#endif

#include <algorithm>
#include <chrono>
#include <utility>

namespace opc::adapters {
namespace metrics_api = opentelemetry::metrics;
namespace metrics_sdk = opentelemetry::sdk::metrics;
namespace resource = opentelemetry::sdk::resource;
namespace nostd = opentelemetry::nostd;

struct OtelMetrics::Impl {
    nostd::shared_ptr<metrics_api::MeterProvider> provider;
    nostd::shared_ptr<metrics_api::Meter> meter;
    std::unordered_map<std::string, nostd::unique_ptr<metrics_api::Counter<double>>> counters;
    std::unordered_map<std::string, nostd::unique_ptr<metrics_api::Histogram<double>>> histograms;
    std::unordered_map<std::string, nostd::unique_ptr<metrics_api::UpDownCounter<double>>> gauges;
    std::unordered_map<std::string, double> gauge_last;
};

bool otlp_metrics_supported() {
#ifdef OPC_WITH_OTLP
    return true;
#else
    return false;
#endif
}

OtelMetrics::OtelMetrics(OtelMetricsOptions options) : impl_(std::make_unique<Impl>()) {
    try {
        resource::ResourceAttributes attrs = {
            {"service.name", options.service_name},
            {"service.version", options.service_version},
        };
        auto resource_ptr = resource::Resource::Create(attrs);

        std::unique_ptr<metrics_sdk::PushMetricExporter> exporter;
        switch (options.export_mode) {
        case MetricsExportMode::None:
            break;
        case MetricsExportMode::OStream:
            exporter = opentelemetry::exporter::metrics::OStreamMetricExporterFactory::Create();
            break;
        case MetricsExportMode::OtlpHttp:
#ifdef OPC_WITH_OTLP
        {
            opentelemetry::exporter::otlp::OtlpHttpMetricExporterOptions opts;
            if (!options.otlp_endpoint.empty()) {
                opts.url = options.otlp_endpoint;
            }
            exporter =
                opentelemetry::exporter::otlp::OtlpHttpMetricExporterFactory::Create(opts);
            break;
        }
#else
            init_error_ = "OTLP metrics exporter was not enabled at build time (OPC_WITH_OTLP)";
            return;
#endif
        }

        auto u_provider = metrics_sdk::MeterProviderFactory::Create(
            std::make_unique<metrics_sdk::ViewRegistry>(), resource_ptr);
        if (exporter) {
            metrics_sdk::PeriodicExportingMetricReaderOptions reader_options;
            reader_options.export_interval_millis =
                std::chrono::milliseconds(std::max(100, options.export_interval_ms));
            reader_options.export_timeout_millis = std::chrono::milliseconds(500);
            auto reader = metrics_sdk::PeriodicExportingMetricReaderFactory::Create(
                std::move(exporter), reader_options);
            u_provider->AddMetricReader(std::move(reader));
        }

        impl_->provider = nostd::shared_ptr<metrics_api::MeterProvider>(u_provider.release());
        metrics_api::Provider::SetMeterProvider(impl_->provider);
        impl_->meter = impl_->provider->GetMeter(options.service_name, options.service_version);
        ok_ = (impl_->meter.get() != nullptr);
        if (!ok_) {
            init_error_ = "failed to create OTel meter";
        }
    } catch (const std::exception& ex) {
        init_error_ = ex.what();
        ok_ = false;
    }
}

OtelMetrics::~OtelMetrics() {
    force_flush();
    std::shared_ptr<metrics_api::MeterProvider> none;
    metrics_api::Provider::SetMeterProvider(none);
    impl_.reset();
}

void OtelMetrics::force_flush() {
    if (!impl_ || !impl_->provider) {
        return;
    }
    if (auto* sdk = dynamic_cast<metrics_sdk::MeterProvider*>(impl_->provider.get())) {
        (void)sdk->ForceFlush(std::chrono::milliseconds(1000));
    }
}

void OtelMetrics::counter_add(std::string_view name, double value) {
    if (!ok_ || !impl_ || !impl_->meter) {
        return;
    }
    std::lock_guard lock(mutex_);
    const std::string key(name);
    auto it = impl_->counters.find(key);
    if (it == impl_->counters.end()) {
        auto counter = impl_->meter->CreateDoubleCounter(key);
        it = impl_->counters.emplace(key, std::move(counter)).first;
    }
    it->second->Add(value);
}

void OtelMetrics::gauge_set(std::string_view name, double value) {
    if (!ok_ || !impl_ || !impl_->meter) {
        return;
    }
    std::lock_guard lock(mutex_);
    const std::string key(name);
    auto it = impl_->gauges.find(key);
    if (it == impl_->gauges.end()) {
        auto gauge = impl_->meter->CreateDoubleUpDownCounter(key);
        it = impl_->gauges.emplace(key, std::move(gauge)).first;
        impl_->gauge_last[key] = 0.0;
    }
    const double prev = impl_->gauge_last[key];
    const double delta = value - prev;
    if (delta != 0.0) {
        it->second->Add(delta);
    }
    impl_->gauge_last[key] = value;
}

void OtelMetrics::histogram_observe(std::string_view name, double value) {
    if (!ok_ || !impl_ || !impl_->meter) {
        return;
    }
    std::lock_guard lock(mutex_);
    const std::string key(name);
    auto it = impl_->histograms.find(key);
    if (it == impl_->histograms.end()) {
        auto hist = impl_->meter->CreateDoubleHistogram(key);
        it = impl_->histograms.emplace(key, std::move(hist)).first;
    }
    it->second->Record(value, opentelemetry::context::Context{});
}

}  // namespace opc::adapters
