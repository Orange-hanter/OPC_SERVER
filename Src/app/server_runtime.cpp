#include "app/server_runtime.hpp"

#include "adapters/modbus_tcp_transport.hpp"
#include "adapters/opc_ua_server.hpp"
#include "project/load.hpp"

#include <filesystem>
#include <type_traits>
#include <variant>

namespace opc::app {
namespace {

void log_msg(ports::ILog* log, ports::LogLevel level, std::string_view msg) {
    if (log != nullptr) {
        log->log(level, "app.runtime", msg);
    }
}

}  // namespace

ServerRuntime::ServerRuntime(ServerRuntimeDeps deps)
    : project_(std::move(deps.project)),
      index_(core::RuntimeIndex::build(project_)),
      clock_(deps.clock),
      metrics_(deps.metrics),
      log_(deps.log),
      transport_factory_(std::move(deps.transport_factory)),
      opcua_(std::move(deps.opcua)) {
    dispatcher_ = std::make_unique<core::Dispatcher>(core::Dispatcher::Dependencies{
        .index = index_,
        .tag_store = &tag_store_,
        .clock = clock_,
        .metrics = metrics_,
    });
}

ServerRuntime::~ServerRuntime() {
    stop();
}

domain::Result<std::unique_ptr<ServerRuntime>> ServerRuntime::create(ServerRuntimeDeps deps) {
    if (!deps.project) {
        return std::unexpected(domain::Error{
            domain::ErrorCode::InvalidArgument, "project is null", "app.runtime", false});
    }
    if (deps.clock == nullptr) {
        return std::unexpected(domain::Error{
            domain::ErrorCode::InvalidArgument, "clock is required", "app.runtime", false});
    }
    if (!deps.transport_factory) {
        deps.transport_factory = default_tcp_transport_factory();
    }
    return std::unique_ptr<ServerRuntime>(new ServerRuntime(std::move(deps)));
}

domain::Result<void> ServerRuntime::start() {
    if (started_) {
        return {};
    }
    for (const auto& endpoint : project_->endpoints) {
        auto transport = transport_factory_(endpoint);
        if (!transport) {
            return std::unexpected(domain::Error{domain::ErrorCode::Internal,
                                                 "transport factory returned null for " + endpoint.id,
                                                 "app.runtime",
                                                 false});
        }
        auto* raw = transport.get();
        auto conn = raw->connect({endpoint.host, endpoint.port});
        if (!conn) {
            log_msg(log_, ports::LogLevel::Warn,
                    "connect failed for endpoint " + endpoint.id + ": " + conn.error().message);
        } else {
            log_msg(log_, ports::LogLevel::Info, "connected endpoint " + endpoint.id);
        }
        dispatcher_->bind_transport(endpoint.id, raw);
        transports_.emplace(endpoint.id, std::move(transport));
    }

    if (opcua_ != nullptr) {
        if (auto ua = opcua_->start(project_); !ua) {
            return ua;
        }
        if (auto* concrete = dynamic_cast<adapters::OpcUaServer*>(opcua_.get())) {
            concrete->set_write_handler([this](domain::TagId id, domain::ScalarValue value) {
                return dispatcher_->enqueue_write(id, std::move(value));
            });
            if (auto bind = concrete->bind_index(index_, tag_store_); !bind) {
                return bind;
            }
        } else {
            for (const auto& binding : index_.tags()) {
                if (binding.tag.node_path.empty()) {
                    continue;
                }
                if (auto bind = opcua_->bind_tag(binding.id, binding.tag.node_path); !bind) {
                    return bind;
                }
            }
            opcua_->iterate();
        }
    }

    started_ = true;
    log_msg(log_, ports::LogLevel::Info,
            "runtime started: " + std::to_string(transports_.size()) + " endpoints, " +
                std::to_string(index_.tags().size()) + " tags" +
                (opcua_ != nullptr ? ", opcua on" : ", opcua off"));
    return {};
}

domain::Result<void> ServerRuntime::poll_once(domain::TimestampMs now) {
    if (!started_) {
        if (auto s = start(); !s) {
            return s;
        }
    }
    domain::Result<void> first_error = {};
    for (const auto& endpoint : project_->endpoints) {
        auto r = dispatcher_->poll_due(endpoint.id, now);
        if (!r && first_error.has_value()) {
            first_error = std::unexpected(r.error());
            log_msg(log_, ports::LogLevel::Warn,
                    "poll error on " + endpoint.id + ": " + r.error().message);
        }
    }
    if (opcua_ != nullptr) {
        opcua_->iterate();
    }
    return first_error;
}

void ServerRuntime::write_watchlist(std::ostream& out) const {
    out << "tag_id,name,quality,value\n";
    for (const auto& binding : index_.tags()) {
        out << binding.id << ',' << binding.tag.name << ',';
        const auto value = tag_store_.get(binding.id);
        if (!value) {
            out << "Missing,\n";
            continue;
        }
        switch (value->quality) {
        case domain::Quality::Good:
            out << "Good,";
            break;
        case domain::Quality::Uncertain:
            out << "Uncertain,";
            break;
        case domain::Quality::Bad:
            out << "Bad,";
            break;
        }
        std::visit(
            [&](const auto& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, std::monostate>) {
                    out << "";
                } else if constexpr (std::is_same_v<T, bool>) {
                    out << (v ? "true" : "false");
                } else {
                    out << v;
                }
            },
            value->value);
        out << '\n';
    }
}

void ServerRuntime::stop() {
    if (opcua_ != nullptr) {
        opcua_->stop();
    }
    for (auto& [id, transport] : transports_) {
        if (transport) {
            transport->close();
        }
        log_msg(log_, ports::LogLevel::Info, "closed endpoint " + id);
    }
    transports_.clear();
    started_ = false;
}

std::optional<std::string> resolve_project_path(const std::string& explicit_path) {
    if (!explicit_path.empty()) {
        if (std::filesystem::exists(explicit_path)) {
            return explicit_path;
        }
        return std::nullopt;
    }
    static const char* kCandidates[] = {
        "project.modbusproj.json",
        "DOCs/examples/demo-plant.modbusproj.json",
        "examples/demo-plant.modbusproj.json",
    };
    for (const char* path : kCandidates) {
        if (std::filesystem::exists(path)) {
            return std::string(path);
        }
    }
    return std::nullopt;
}

domain::Result<std::shared_ptr<const project::Project>>
load_project_or_error(const std::string& path, ports::ILog* log) {
    auto loaded = project::load_file(path);
    for (const auto& d : loaded.diagnostics) {
        const auto level = d.severity == project::Diagnostic::Severity::Error ? ports::LogLevel::Error
                                                                              : ports::LogLevel::Warn;
        log_msg(log, level, d.path + ": " + d.message);
    }
    if (!loaded.ok) {
        return std::unexpected(domain::Error{domain::ErrorCode::InvalidArgument,
                                             "project validation failed: " + path,
                                             "app.runtime",
                                             false});
    }
    return std::shared_ptr<const project::Project>(
        std::make_shared<project::Project>(std::move(loaded.project)));
}

TransportFactory default_tcp_transport_factory() {
    return [](const project::Endpoint& endpoint) -> std::unique_ptr<ports::IModbusTransport> {
        return std::make_unique<adapters::ModbusTcpTransport>(endpoint.response_timeout_ms);
    };
}

}  // namespace opc::app
