#include "adapters/ring_historian.hpp"

#include <algorithm>

namespace opc::adapters {

RingHistorian::RingHistorian(std::size_t capacity, ports::IMetrics* metrics)
    : capacity_(capacity == 0 ? 1 : capacity), metrics_(metrics) {
    buffer_.resize(capacity_);
}

void RingHistorian::record(domain::TagId id, const domain::TagValue& value) {
    std::lock_guard lock(mutex_);
    if (count_ == capacity_) {
        ++dropped_;
        if (metrics_ != nullptr) {
            metrics_->counter_add("historian.dropped", 1.0);
        }
    } else {
        ++count_;
    }
    buffer_[head_] = ports::HistorianSample{.id = id, .value = value};
    head_ = (head_ + 1) % capacity_;
}

domain::Result<void> RingHistorian::flush() {
    return {};
}

std::vector<ports::HistorianSample> RingHistorian::recent(std::size_t max) const {
    std::lock_guard lock(mutex_);
    const std::size_t n = std::min(max, count_);
    std::vector<ports::HistorianSample> out;
    out.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t idx = (head_ + capacity_ - 1 - i) % capacity_;
        out.push_back(buffer_[idx]);
    }
    return out;
}

std::uint64_t RingHistorian::dropped() const {
    std::lock_guard lock(mutex_);
    return dropped_;
}

std::size_t RingHistorian::size() const {
    std::lock_guard lock(mutex_);
    return count_;
}

std::vector<ports::HistorianSample> RingHistorian::snapshot() const {
    std::lock_guard lock(mutex_);
    std::vector<ports::HistorianSample> out;
    out.reserve(count_);
    const std::size_t oldest = (head_ + capacity_ - count_) % capacity_;
    for (std::size_t i = 0; i < count_; ++i) {
        out.push_back(buffer_[(oldest + i) % capacity_]);
    }
    return out;
}

}  // namespace opc::adapters
