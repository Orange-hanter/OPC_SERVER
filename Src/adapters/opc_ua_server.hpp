#pragma once

#include "core/runtime_index.hpp"
#include "domain/types.hpp"
#include "ports/i_log.hpp"
#include "ports/i_opc_ua_facade.hpp"
#include "ports/i_tag_store.hpp"
#include "project/types.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

struct UA_Server;

namespace opc::adapters {

/// OPC UA server (open62541). Security None for lab; TagStore is source of truth for reads.
class OpcUaServer final : public ports::IOpcUaFacade {
public:
    explicit OpcUaServer(ports::ILog* log = nullptr);
    ~OpcUaServer() override;

    OpcUaServer(const OpcUaServer&) = delete;
    OpcUaServer& operator=(const OpcUaServer&) = delete;

    domain::Result<void> start(std::shared_ptr<const project::Project> project) override;
    void stop() override;

    domain::Result<void> bind_tag(domain::TagId id, std::string_view node_path) override;
    void iterate() override;

    /// Bind all tags from runtime index and subscribe to TagStore updates.
    domain::Result<void> bind_index(const core::RuntimeIndex& index, ports::ITagStore& store);

    /// Start background event-loop pump (call after model bind).
    void serve_async();

    [[nodiscard]] bool is_running() const { return server_ != nullptr; }
    [[nodiscard]] std::string endpoint_url() const { return endpoint_url_; }

private:
    domain::Result<void> ensure_path(std::string_view node_path, std::uint32_t& out_node_id);
    domain::Result<void> add_variable(domain::TagId tag_id,
                                      std::string_view browse_name,
                                      std::uint32_t parent_id,
                                      const project::Tag& tag);
    void enqueue_update(domain::TagId id, const domain::TagValue& value);
    void flush_updates();
    void pump_loop();
    [[nodiscard]] static std::uint32_t quality_to_status(domain::Quality quality,
                                                         domain::QualityReason reason);

    ports::ILog* log_{nullptr};
    UA_Server* server_{nullptr};
    std::shared_ptr<const project::Project> project_;
    std::string endpoint_url_;
    std::uint16_t ns_index_{1};
    std::uint32_t next_numeric_id_{1000};
    std::unordered_map<std::string, std::uint32_t> folder_ids_;
    std::unordered_map<domain::TagId, std::uint32_t> tag_node_ids_;
    std::unordered_map<domain::TagId, project::TagType> tag_types_;
    std::uint64_t store_subscription_{0};
    ports::ITagStore* store_{nullptr};

    std::mutex pending_mutex_;
    std::unordered_map<domain::TagId, domain::TagValue> pending_;

    std::atomic<bool> pump_running_{false};
    std::thread pump_thread_;
};

}  // namespace opc::adapters
