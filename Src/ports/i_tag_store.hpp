#pragma once

#include "domain/types.hpp"

#include <functional>
#include <optional>
#include <span>

namespace opc::ports {

/// Central runtime snapshot. Thread-safe. Never perform I/O under store locks.
class ITagStore {
public:
    using ChangeHandler = std::function<void(domain::TagId, const domain::TagValue&)>;

    virtual ~ITagStore() = default;

    virtual void publish(domain::TagId id, domain::TagValue value) = 0;
    [[nodiscard]] virtual std::optional<domain::TagValue> get(domain::TagId id) const = 0;

    /// Returns subscription id; handler must not call back into publish re-entrantly.
    virtual std::uint64_t subscribe(ChangeHandler handler) = 0;
    virtual void unsubscribe(std::uint64_t subscription_id) = 0;

    virtual void mark_stale_before(domain::TimestampMs cutoff_server_ts,
                                   domain::QualityReason reason) = 0;
};

}  // namespace opc::ports
