#include "app/server_runtime.hpp"

#include "adapters/asio_reactor.hpp"
#include "adapters/modbus_tcp_transport.hpp"
#include "adapters/modbus_udp_transport.hpp"
#include "ports/i_opc_ua_facade.hpp"
#include "project/load.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

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
      historian_(deps.historian),
      frame_log_(deps.frame_log),
      tracer_(deps.tracer),
      transport_factory_(std::move(deps.transport_factory)),
      opcua_(std::move(deps.opcua)) {
    dispatcher_ = std::make_unique<core::Dispatcher>(core::Dispatcher::Dependencies{
        .index = index_,
        .tag_store = &tag_store_,
        .clock = clock_,
        .metrics = metrics_,
        .tracer = tracer_,
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
        deps.transport_factory = default_tcp_transport_factory(deps.frame_log);
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
            stop();
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
            dispatcher_->mark_endpoint_bad(endpoint.id, domain::QualityReason::NoCommunication,
                                           clock_->now_ms());
            next_reconnect_ms_[endpoint.id] =
                clock_->now_ms() + std::max(0, endpoint.reconnect_delay_ms);
        } else {
            log_msg(log_, ports::LogLevel::Info, "connected endpoint " + endpoint.id);
            next_reconnect_ms_.erase(endpoint.id);
        }
        dispatcher_->bind_transport(endpoint.id, raw);
        transports_.emplace(endpoint.id, std::move(transport));
    }

    if (opcua_ != nullptr) {
        install_write_handler();
        if (auto ua = opcua_->start(project_); !ua) {
            stop();
            return ua;
        }
        std::vector<ports::OpcUaTagSpec> specs;
        specs.reserve(index_.tags().size());
        for (const auto& binding : index_.tags()) {
            specs.push_back(ports::OpcUaTagSpec{.id = binding.id, .tag = binding.tag});
        }
        if (auto bind = opcua_->bind_tags(tag_store_, specs); !bind) {
            stop();
            return bind;
        }
    }

    if (historian_ != nullptr && !historian_sub_) {
        historian_sub_ = tag_store_.subscribe(
            [this](domain::TagId id, const domain::TagValue& value) { historian_->record(id, value); });
    }

    started_ = true;
    log_msg(log_, ports::LogLevel::Info,
            "runtime started: " + std::to_string(transports_.size()) + " endpoints, " +
                std::to_string(index_.tags().size()) + " tags" +
                (opcua_ != nullptr ? ", opcua on" : ", opcua off") +
                (historian_ != nullptr ? ", historian on" : ""));
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
    if (historian_ != nullptr) {
        if (auto flushed = historian_->flush(); !flushed) {
            log_msg(log_, ports::LogLevel::Warn, "historian flush: " + flushed.error().message);
        }
    }
    return first_error;
}

void ServerRuntime::install_write_handler() {
    opcua_->set_write_handler([this](domain::TagId id, domain::ScalarValue value) {
        auto binding = index_.find_by_id(id);
        auto queued = dispatcher_->enqueue_write(id, std::move(value));
        if (queued && reactor_ && reactor_->running() && binding) {
            reactor_->post(binding->endpoint_id, [this, endpoint = binding->endpoint_id] {
                if (auto flushed = dispatcher_->flush_writes(endpoint); !flushed) {
                    log_msg(log_, ports::LogLevel::Warn,
                            "flush_writes " + endpoint + ": " + flushed.error().message);
                }
            });
        }
        return queued;
    });
}

std::size_t ServerRuntime::choose_worker_count() const {
    const std::size_t endpoints = std::max<std::size_t>(project_->endpoints.size(), 1);
    std::size_t hw = std::thread::hardware_concurrency();
    if (hw < 2) {
        hw = 2;
    }
    return std::max<std::size_t>(2, std::min(endpoints, hw));
}

int ServerRuntime::min_group_period_ms(std::string_view endpoint_id) const {
    int min_period = 0;
    for (const auto* group : index_.groups_for_endpoint(endpoint_id)) {
        if (group == nullptr || group->period_ms < 1) {
            continue;
        }
        if (min_period == 0 || group->period_ms < min_period) {
            min_period = group->period_ms;
        }
    }
    return min_period > 0 ? min_period : 1000;
}

void ServerRuntime::tick_endpoint(const std::string& endpoint_id) {
    auto t_it = transports_.find(endpoint_id);
    if (t_it == transports_.end() || t_it->second == nullptr) {
        return;
    }
    auto& transport = *t_it->second;
    const auto now = clock_->now_ms();
    const auto* ep = index_.endpoint(endpoint_id);
    const int delay_ms = ep != nullptr ? std::max(0, ep->reconnect_delay_ms) : 2000;

    if (!transport.is_connected()) {
        const auto next = next_reconnect_ms_.find(endpoint_id);
        if (next != next_reconnect_ms_.end() && now < next->second) {
            return;
        }
    }

    auto r = dispatcher_->poll_due(endpoint_id, now);
    if (!transport.is_connected()) {
        dispatcher_->mark_endpoint_bad(endpoint_id, domain::QualityReason::NoCommunication, now);
        next_reconnect_ms_[endpoint_id] = now + delay_ms;
    } else {
        next_reconnect_ms_.erase(endpoint_id);
    }

    if (!r) {
        log_msg(log_, ports::LogLevel::Warn,
                "poll error on " + endpoint_id + ": " + r.error().message);
        if (r.error().code == domain::ErrorCode::Connection) {
            transport.close();
            dispatcher_->mark_endpoint_bad(endpoint_id, domain::QualityReason::NoCommunication, now);
            next_reconnect_ms_[endpoint_id] = now + delay_ms;
        }
    }

    if (historian_ != nullptr) {
        if (auto flushed = historian_->flush(); !flushed) {
            log_msg(log_, ports::LogLevel::Warn, "historian flush: " + flushed.error().message);
        }
    }
}

domain::Result<void> ServerRuntime::start_reactor(ReactorOptions options) {
    if (!started_) {
        if (auto s = start(); !s) {
            return s;
        }
    }
    if (reactor_ && reactor_->running()) {
        return {};
    }

    reactor_ = std::make_unique<adapters::AsioReactor>(choose_worker_count());
    for (const auto& endpoint : project_->endpoints) {
        reactor_->ensure_strand(endpoint.id);
    }
    reactor_->start();

    for (const auto& endpoint : project_->endpoints) {
        const auto period = std::chrono::milliseconds{min_group_period_ms(endpoint.id)};
        reactor_->repeat_on_strand(endpoint.id, period, [this, id = endpoint.id] { tick_endpoint(id); });
    }

    if (options.watch_out != nullptr && options.watch_period.count() > 0) {
        auto* out = options.watch_out;
        reactor_->repeat(options.watch_period, [this, out] { write_watchlist(*out); });
    }

    log_msg(log_, ports::LogLevel::Info,
            "asio reactor started: " + std::to_string(reactor_->worker_count()) + " workers, " +
                std::to_string(project_->endpoints.size()) + " endpoint strands");
    return {};
}

void ServerRuntime::run_until_stop() {
    if (reactor_) {
        reactor_->run_until_stop();
    }
}

bool ServerRuntime::reactor_running() const {
    return reactor_ && reactor_->running();
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
    if (reactor_) {
        reactor_->stop();
        reactor_.reset();
    }
    next_reconnect_ms_.clear();
    if (historian_sub_) {
        tag_store_.unsubscribe(*historian_sub_);
        historian_sub_.reset();
    }
    if (historian_ != nullptr) {
        if (auto flushed = historian_->flush(); !flushed) {
            log_msg(log_, ports::LogLevel::Warn, "historian flush on stop: " + flushed.error().message);
        }
    }
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

TransportFactory default_tcp_transport_factory(ports::IFrameLog* frame_log) {
    return default_transport_factory(frame_log);
}

TransportFactory default_transport_factory(ports::IFrameLog* frame_log) {
    return [frame_log](const project::Endpoint& endpoint) -> std::unique_ptr<ports::IModbusTransport> {
        if (endpoint.transport == project::Transport::Udp) {
            return std::make_unique<adapters::ModbusUdpTransport>(adapters::ModbusUdpTransportOptions{
                .response_timeout_ms = endpoint.response_timeout_ms,
                .frame_log = frame_log,
                .endpoint_id = endpoint.id,
            });
        }
        return std::make_unique<adapters::ModbusTcpTransport>(adapters::ModbusTcpTransportOptions{
            .response_timeout_ms = endpoint.response_timeout_ms,
            .frame_log = frame_log,
            .endpoint_id = endpoint.id,
        });
    };
}

}  // namespace opc::app
