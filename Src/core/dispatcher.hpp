#pragma once

#include "core/runtime_index.hpp"
#include "domain/types.hpp"
#include "ports/i_clock.hpp"
#include "ports/i_metrics.hpp"
#include "ports/i_modbus_transport.hpp"
#include "ports/i_tag_store.hpp"
#include "project/types.hpp"

#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace opc::core {

/// Schedules poll groups and write-down for one logical runtime.
/// Transport instances are owned externally and bound per endpoint (strand affinity — ADR-0002).
class Dispatcher {
public:
    struct Dependencies {
        RuntimeIndex index;
        ports::ITagStore* tag_store{nullptr};
        ports::IClock* clock{nullptr};
        ports::IMetrics* metrics{nullptr};
    };

    explicit Dispatcher(Dependencies deps);

    Dispatcher(const Dispatcher&) = delete;
    Dispatcher& operator=(const Dispatcher&) = delete;

    void bind_transport(std::string endpoint_id, ports::IModbusTransport* transport);

    /// Execute due poll groups for endpoint (period-aware).
    domain::Result<void> poll_due(std::string_view endpoint_id, domain::TimestampMs now);

    domain::Result<void> enqueue_write(domain::TagId tag_id, domain::ScalarValue value);

    /// Drain write queue for endpoint (call on endpoint strand before/with poll).
    domain::Result<void> flush_writes(std::string_view endpoint_id);

    /// Publish Bad/NoCommunication for every tag on the endpoint (disconnect / connect fail).
    void mark_endpoint_bad(std::string_view endpoint_id,
                           domain::QualityReason reason,
                           domain::TimestampMs now);

private:
    domain::Result<void> poll_group(const project::PollGroup& group,
                                    ports::IModbusTransport& transport,
                                    domain::TimestampMs now);

    domain::Result<void> poll_tag(const TagBinding& binding,
                                  ports::IModbusTransport& transport,
                                  domain::TimestampMs now);

    Dependencies deps_;
    std::unordered_map<std::string, ports::IModbusTransport*> transports_;
    std::unordered_map<std::string, domain::TimestampMs> last_poll_ms_;

    struct PendingWrite {
        domain::TagId tag_id{0};
        domain::ScalarValue value{};
    };
    static constexpr std::size_t kMaxWriteQueueDepth = 1024;

    std::mutex write_mutex_;
    std::unordered_map<std::string, std::vector<PendingWrite>> write_queues_;
};

}  // namespace opc::core
