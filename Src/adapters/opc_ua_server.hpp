#pragma once

#include "domain/types.hpp"
#include "ports/i_log.hpp"
#include "ports/i_metrics.hpp"
#include "ports/i_opc_ua_facade.hpp"
#include "ports/i_tag_store.hpp"
#include "project/types.hpp"
#include "adapters/ua_pki.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct UA_Server;

namespace opc::adapters {

struct OpcUaNodeContext;

/// OPC UA server (open62541). Honors project securityMode (None / Sign / SignAndEncrypt).
/// Reads come from ITagStore via DataSource; writes enqueue via OpcUaWriteHandler.
class OpcUaServer final : public ports::IOpcUaFacade {
public:
    explicit OpcUaServer(ports::ILog* log = nullptr,
                         ports::IMetrics* metrics = nullptr,
                         OpcUaSecurityOptions security = {});
    ~OpcUaServer() override;

    void set_security_options(OpcUaSecurityOptions security) { security_ = std::move(security); }

    OpcUaServer(const OpcUaServer&) = delete;
    OpcUaServer& operator=(const OpcUaServer&) = delete;

    domain::Result<void> start(std::shared_ptr<const project::Project> project) override;
    void stop() override;

    domain::Result<void> bind_tag(domain::TagId id, std::string_view node_path) override;
    domain::Result<void> bind_tags(ports::ITagStore& store,
                                   std::span<const ports::OpcUaTagSpec> tags) override;
    void set_write_handler(ports::OpcUaWriteHandler handler) override;
    void iterate() override;

    void serve_async();

    [[nodiscard]] bool is_running() const { return server_ != nullptr; }
    [[nodiscard]] std::string endpoint_url() const { return endpoint_url_; }
    [[nodiscard]] std::uint16_t namespace_index() const { return ns_index_; }
    [[nodiscard]] std::optional<std::uint32_t> node_numeric_id(domain::TagId id) const;

    [[nodiscard]] ports::ITagStore* store() const { return store_; }
    [[nodiscard]] ports::OpcUaWriteHandler& write_handler() { return write_handler_; }
    [[nodiscard]] ports::ILog* log() const { return log_; }

    [[nodiscard]] static std::uint32_t quality_to_status(domain::Quality quality,
                                                        domain::QualityReason reason);
    [[nodiscard]] static std::uint32_t map_error_to_status(const domain::Error& error);

    void note_session_activate(std::uint32_t session_id);
    void note_session_close(std::uint32_t session_id);

private:
    domain::Result<void> ensure_path(std::string_view node_path, std::uint32_t& out_node_id);
    domain::Result<void> add_variable(domain::TagId tag_id,
                                      std::string_view browse_name,
                                      std::uint32_t parent_id,
                                      const project::Tag& tag);
    domain::Result<void> add_diagnostics();
    void note_tag_quality(domain::TagId id, const domain::TagValue& value);
    void publish_quality_metrics();
    void write_diagnostics();
    void pump_loop();

    ports::ILog* log_{nullptr};
    ports::IMetrics* metrics_{nullptr};
    OpcUaSecurityOptions security_{};
    UA_Server* server_{nullptr};
    std::shared_ptr<const project::Project> project_;
    std::string endpoint_url_;
    std::uint16_t ns_index_{1};
    std::uint32_t next_numeric_id_{1000};
    std::unordered_map<std::string, std::uint32_t> folder_ids_;
    std::unordered_map<domain::TagId, std::uint32_t> tag_node_ids_;
    std::unordered_map<domain::TagId, project::TagType> tag_types_;
    std::vector<std::unique_ptr<OpcUaNodeContext>> node_contexts_;
    ports::ITagStore* store_{nullptr};
    ports::OpcUaWriteHandler write_handler_;
    std::uint64_t store_subscription_{0};

    std::mutex diagnostics_mutex_;
    std::unordered_map<domain::TagId, domain::Quality> latest_quality_;
    std::uint64_t good_count_{0};
    std::uint64_t uncertain_count_{0};
    std::uint64_t bad_count_{0};
    std::unordered_set<std::uint32_t> active_sessions_;
    std::string last_error_;
    bool diagnostics_dirty_{false};
    std::uint32_t diagnostics_state_node_{0};
    std::uint32_t diagnostics_good_node_{0};
    std::uint32_t diagnostics_uncertain_node_{0};
    std::uint32_t diagnostics_bad_node_{0};
    std::uint32_t diagnostics_last_error_node_{0};

    std::atomic<bool> pump_running_{false};
    std::thread pump_thread_;
};

}  // namespace opc::adapters
