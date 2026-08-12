#include "app/application.hpp"

#include "adapters/frame_log.hpp"
#include "adapters/opc_ua_server.hpp"
#include "adapters/otel_metrics.hpp"
#include "adapters/ring_historian.hpp"
#include "adapters/spdlog_log.hpp"
#include "adapters/sqlite_historian.hpp"
#include "adapters/system_clock.hpp"
#include "app/version.hpp"
#include "ports/i_log.hpp"

#include <chrono>
#include <iostream>
#include <thread>

namespace opc::app {
namespace {

ports::LogLevel to_port_level(LogLevelOption level) {
    switch (level) {
    case LogLevelOption::Trace:
        return ports::LogLevel::Trace;
    case LogLevelOption::Debug:
        return ports::LogLevel::Debug;
    case LogLevelOption::Info:
        return ports::LogLevel::Info;
    case LogLevelOption::Warn:
        return ports::LogLevel::Warn;
    case LogLevelOption::Error:
        return ports::LogLevel::Error;
    }
    return ports::LogLevel::Info;
}

adapters::MetricsExportMode to_metrics_mode(MetricsExportOption mode) {
    switch (mode) {
    case MetricsExportOption::None:
        return adapters::MetricsExportMode::None;
    case MetricsExportOption::OStream:
        return adapters::MetricsExportMode::OStream;
    case MetricsExportOption::OtlpHttp:
        return adapters::MetricsExportMode::OtlpHttp;
    }
    return adapters::MetricsExportMode::OStream;
}

}  // namespace

Application::Application() = default;

Application::~Application() {
    if (auto* otel = dynamic_cast<adapters::OtelMetrics*>(metrics_.get())) {
        otel->force_flush();
    }
}

bool Application::init(const CliOptions& options) {
    options_ = options;

    adapters::SpdlogLogOptions log_opts;
    log_opts.min_level = to_port_level(options_.log_level);
    log_opts.log_file = options_.log_file;
    log_opts.async = true;
    log_ = std::make_unique<adapters::SpdlogLog>(std::move(log_opts));
    clock_ = std::make_unique<adapters::SystemClock>();

    if (!options_.errors.empty()) {
        for (const auto& err : options_.errors) {
            log_->error("app", err);
        }
        print_usage(std::cerr);
        return false;
    }
    if (options_.help) {
        print_usage(std::cout);
        return false;
    }
    if (options_.version) {
        std::cout << "OPC_SERVER " << OPC_SERVER_VERSION_STRING << '\n';
        return false;
    }

    if (options_.metrics_export == MetricsExportOption::OtlpHttp &&
        !adapters::otlp_metrics_supported()) {
        log_->error("app", "OTLP requested but build lacks OPC_WITH_OTLP");
        return false;
    }

    adapters::OtelMetricsOptions metrics_opts;
    metrics_opts.export_mode = to_metrics_mode(options_.metrics_export);
    metrics_opts.otlp_endpoint = options_.otlp_endpoint;
    metrics_opts.service_version = OPC_SERVER_VERSION_STRING;
    auto otel = std::make_unique<adapters::OtelMetrics>(std::move(metrics_opts));
    if (!otel->ok()) {
        log_->error("app", "otel metrics init failed: " + otel->init_error());
        return false;
    }
    metrics_ = std::move(otel);

    const auto path = resolve_project_path(options_.project_path);
    if (!path) {
        log_->error("app",
                    options_.project_path.empty() ? "no project file found (use --project)"
                                                  : "project file not found: " + options_.project_path);
        return false;
    }

    auto project = load_project_or_error(*path, log_.get());
    if (!project) {
        log_->error("app", project.error().message);
        return false;
    }

    if (!options_.frame_log_path.empty()) {
        auto file_log = std::make_unique<adapters::FileFrameLog>(options_.frame_log_path);
        if (!file_log->is_open()) {
            log_->error("app", "failed to open frame log: " + options_.frame_log_path);
            return false;
        }
        frame_log_ = std::move(file_log);
        log_->info("app", "frame log: " + options_.frame_log_path);
    }

    if (options_.enable_historian) {
        if (!options_.historian_db.empty()) {
            auto sqlite = std::make_unique<adapters::SqliteHistorian>(
                options_.historian_db, options_.historian_capacity, metrics_.get());
            if (sqlite->open_error()) {
                log_->error("app", "historian db: " + sqlite->open_error()->message);
                return false;
            }
            historian_ = std::move(sqlite);
            log_->info("app", "historian cold sqlite: " + options_.historian_db);
        } else {
            historian_ = std::make_unique<adapters::RingHistorian>(options_.historian_capacity,
                                                                   metrics_.get());
            log_->info("app", "historian hot ring capacity=" +
                                  std::to_string(options_.historian_capacity));
        }
    }

    std::unique_ptr<ports::IOpcUaFacade> opcua;
    if (options_.enable_opcua) {
        opcua = std::make_unique<adapters::OpcUaServer>(log_.get());
    }

    auto runtime = ServerRuntime::create(ServerRuntimeDeps{
        .project = *project,
        .clock = clock_.get(),
        .metrics = metrics_.get(),
        .log = log_.get(),
        .historian = historian_.get(),
        .frame_log = frame_log_.get(),
        .transport_factory = default_tcp_transport_factory(frame_log_.get()),
        .opcua = std::move(opcua),
    });
    if (!runtime) {
        log_->error("app", runtime.error().message);
        return false;
    }
    runtime_ = std::move(*runtime);
    log_->info("app", "loaded project '" + runtime_->project().name + "' from " + *path);
    return true;
}

int Application::run() {
    if (!runtime_) {
        return 1;
    }
    if (auto s = runtime_->start(); !s) {
        log_->error("app", "start failed: " + s.error().message);
        runtime_->stop();
        return 1;
    }

    const auto poll_and_watch = [&](domain::TimestampMs now) {
        auto r = runtime_->poll_once(now);
        if (!r) {
            log_->warn("app", "poll_once: " + r.error().message);
        }
        if (options_.watch) {
            runtime_->write_watchlist(std::cout);
        }
    };

    if (options_.once) {
        poll_and_watch(clock_->now_ms());
        runtime_->stop();
        if (auto* otel = dynamic_cast<adapters::OtelMetrics*>(metrics_.get())) {
            otel->force_flush();
        }
        return 0;
    }

    log_->info("app", "entering poll loop (Ctrl+C to stop)");
    while (true) {
        poll_and_watch(clock_->now_ms());
        std::this_thread::sleep_for(std::chrono::milliseconds(options_.watch_period_ms));
    }
}

}  // namespace opc::app
