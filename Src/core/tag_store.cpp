#include "core/tag_store.hpp"

namespace opc::core {

void TagStore::publish(domain::TagId id, domain::TagValue value) {
    std::vector<ChangeHandler> handlers;
    {
        std::lock_guard lock(mutex_);
        values_[id] = value;
        handlers.reserve(subscribers_.size());
        for (auto& [_, handler] : subscribers_) {
            handlers.push_back(handler);
        }
    }
    for (auto& handler : handlers) {
        handler(id, value);
    }
}

std::optional<domain::TagValue> TagStore::get(domain::TagId id) const {
    std::lock_guard lock(mutex_);
    const auto it = values_.find(id);
    if (it == values_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::uint64_t TagStore::subscribe(ChangeHandler handler) {
    std::lock_guard lock(mutex_);
    const auto id = next_subscription_++;
    subscribers_.emplace(id, std::move(handler));
    return id;
}

void TagStore::unsubscribe(std::uint64_t subscription_id) {
    std::lock_guard lock(mutex_);
    subscribers_.erase(subscription_id);
}

void TagStore::mark_stale_before(domain::TimestampMs cutoff_server_ts,
                                 domain::QualityReason reason) {
    std::vector<std::pair<domain::TagId, domain::TagValue>> changed;
    {
        std::lock_guard lock(mutex_);
        for (auto& [id, value] : values_) {
            if (value.quality == domain::Quality::Good && value.server_ts < cutoff_server_ts) {
                value.quality = domain::Quality::Uncertain;
                value.reason = reason;
                changed.emplace_back(id, value);
            }
        }
    }
    for (auto& [id, value] : changed) {
        // Re-enter publish path without holding mutex during handlers: duplicate notify
        std::vector<ChangeHandler> handlers;
        {
            std::lock_guard lock(mutex_);
            for (auto& [_, handler] : subscribers_) {
                handlers.push_back(handler);
            }
        }
        for (auto& handler : handlers) {
            handler(id, value);
        }
    }
}

}  // namespace opc::core
