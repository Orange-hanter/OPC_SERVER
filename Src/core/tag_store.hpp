#pragma once

#include "ports/i_tag_store.hpp"

#include <mutex>
#include <unordered_map>
#include <vector>

namespace opc::core {

/// In-process TagStore implementation (stage 2 body). Header establishes API surface.
class TagStore final : public ports::ITagStore {
public:
    void publish(domain::TagId id, domain::TagValue value) override;
    [[nodiscard]] std::optional<domain::TagValue> get(domain::TagId id) const override;
    std::uint64_t subscribe(ChangeHandler handler) override;
    void unsubscribe(std::uint64_t subscription_id) override;
    void mark_stale_before(domain::TimestampMs cutoff_server_ts,
                           domain::QualityReason reason) override;

private:
    mutable std::mutex mutex_;
    std::unordered_map<domain::TagId, domain::TagValue> values_;
    std::unordered_map<std::uint64_t, ChangeHandler> subscribers_;
    std::uint64_t next_subscription_{1};
};

}  // namespace opc::core
