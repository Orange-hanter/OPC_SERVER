#include "app/application.hpp"

#include "adapters/opc_ua_server.hpp"
#include "adapters/stderr_log.hpp"
#include "adapters/system_clock.hpp"
#include "app/version.hpp"
#include "ports/i_metrics.hpp"

#include <chrono>
#include <iostream>
#include <thread>

namespace opc::app {

Application::Application() = default;
Application::~Application() = default;

bool Application::init(const CliOptions& options) {
    options_ = options;
    log_ = std::make_unique<adapters::StderrLog>();
    clock_ = std::make_unique<adapters::SystemClock>();
    metrics_ = std::make_unique<ports::NullMetrics>();

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

    std::unique_ptr<ports::IOpcUaFacade> opcua;
    if (options_.enable_opcua) {
        opcua = std::make_unique<adapters::OpcUaServer>(log_.get());
    }

    auto runtime = ServerRuntime::create(ServerRuntimeDeps{
        .project = *project,
        .clock = clock_.get(),
        .metrics = metrics_.get(),
        .log = log_.get(),
        .transport_factory = default_tcp_transport_factory(),
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
        return 0;
    }

    log_->info("app", "entering poll loop (Ctrl+C to stop)");
    while (true) {
        poll_and_watch(clock_->now_ms());
        std::this_thread::sleep_for(std::chrono::milliseconds(options_.watch_period_ms));
    }
}

}  // namespace opc::app
