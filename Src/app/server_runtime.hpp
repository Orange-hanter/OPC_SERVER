#pragma once

#include "core/dispatcher.hpp"
#include "core/runtime_index.hpp"
#include "core/tag_store.hpp"
#include "domain/types.hpp"
#include "ports/i_clock.hpp"
#include "ports/i_log.hpp"
#include "ports/i_metrics.hpp"
#include "ports/i_modbus_transport.hpp"
#include "ports/i_opc_ua_facade.hpp"
#include "project/types.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <unordered_map>

namespace opc::app {

using TransportFactory =
    std::function<std::unique_ptr<ports::IModbusTransport>(const project::Endpoint&)>;

struct ServerRuntimeDeps {
    std::shared_ptr<const project::Project> project;
    ports::IClock* clock{nullptr};
    ports::IMetrics* metrics{nullptr};
    ports::ILog* log{nullptr};
    TransportFactory transport_factory;
    /// Optional northbound OPC UA facade (owned by caller or moved in).
    std::unique_ptr<ports::IOpcUaFacade> opcua;
};

/// Composition root for southbound+core runtime (ADR-0001).
/// Non-movable: Dispatcher holds pointers into this object.
class ServerRuntime {
public:
    static domain::Result<std::unique_ptr<ServerRuntime>> create(ServerRuntimeDeps deps);

    ServerRuntime(const ServerRuntime&) = delete;
    ServerRuntime& operator=(const ServerRuntime&) = delete;
    ServerRuntime(ServerRuntime&&) = delete;
    ServerRuntime& operator=(ServerRuntime&&) = delete;
    ~ServerRuntime();

    [[nodiscard]] const core::RuntimeIndex& index() const { return index_; }
    [[nodiscard]] core::TagStore& tag_store() { return tag_store_; }
    [[nodiscard]] const core::TagStore& tag_store() const { return tag_store_; }
    [[nodiscard]] core::Dispatcher& dispatcher() { return *dispatcher_; }
    [[nodiscard]] const project::Project& project() const { return *project_; }
    [[nodiscard]] ports::IOpcUaFacade* opcua() { return opcua_.get(); }
    [[nodiscard]] const ports::IOpcUaFacade* opcua() const { return opcua_.get(); }

    domain::Result<void> start();
    domain::Result<void> poll_once(domain::TimestampMs now);
    void write_watchlist(std::ostream& out) const;
    void stop();

private:
    explicit ServerRuntime(ServerRuntimeDeps deps);

    std::shared_ptr<const project::Project> project_;
    core::RuntimeIndex index_;
    core::TagStore tag_store_;
    std::unique_ptr<core::Dispatcher> dispatcher_;
    ports::IClock* clock_{nullptr};
    ports::IMetrics* metrics_{nullptr};
    ports::ILog* log_{nullptr};
    TransportFactory transport_factory_;
    std::unordered_map<std::string, std::unique_ptr<ports::IModbusTransport>> transports_;
    std::unique_ptr<ports::IOpcUaFacade> opcua_;
    bool started_{false};
};

[[nodiscard]] std::optional<std::string> resolve_project_path(const std::string& explicit_path);

[[nodiscard]] domain::Result<std::shared_ptr<const project::Project>>
load_project_or_error(const std::string& path, ports::ILog* log);

TransportFactory default_tcp_transport_factory();

}  // namespace opc::app
