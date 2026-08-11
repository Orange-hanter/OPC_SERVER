#pragma once

#include "domain/types.hpp"
#include "ports/i_clock.hpp"
#include "ports/i_metrics.hpp"
#include "ports/i_modbus_transport.hpp"
#include "ports/i_tag_store.hpp"
#include "project/types.hpp"

#include <memory>
#include <string>
#include <string_view>

namespace opc::core {

/// Schedules poll groups and write-down for one logical runtime.
/// Transport instances are owned externally and bound per endpoint (strand affinity — ADR-0002).
class Dispatcher {
public:
    struct Dependencies {
        std::shared_ptr<const project::Project> project;
        ports::ITagStore* tag_store{nullptr};
        ports::IClock* clock{nullptr};
        ports::IMetrics* metrics{nullptr};
    };

    explicit Dispatcher(Dependencies deps);

    Dispatcher(const Dispatcher&) = delete;
    Dispatcher& operator=(const Dispatcher&) = delete;

    /// Bind transport for endpoint id (must be used only on that endpoint's strand).
    void bind_transport(std::string endpoint_id, ports::IModbusTransport* transport);

    /// Called by reactor timers / strand: execute due poll work for endpoint.
    domain::Result<void> poll_due(std::string_view endpoint_id, domain::TimestampMs now);

    /// Enqueue write from northbound; executed on endpoint strand with priority (ADR-0002).
    domain::Result<void> enqueue_write(domain::TagId tag_id, domain::ScalarValue value);

private:
    Dependencies deps_;
};

}  // namespace opc::core
