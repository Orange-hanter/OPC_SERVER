#include "adapters/memory_metrics.hpp"

namespace opc::adapters {

void MemoryMetrics::counter_add(std::string_view name, double value) {
    std::lock_guard lock(mutex_);
    counters_[std::string(name)] += value;
}

void MemoryMetrics::gauge_set(std::string_view name, double value) {
    std::lock_guard lock(mutex_);
    gauges_[std::string(name)] = value;
}

void MemoryMetrics::histogram_observe(std::string_view name, double value) {
    // Store last observation as a gauge-like series counter for MVP.
    std::lock_guard lock(mutex_);
    counters_[std::string(name) + ".count"] += 1.0;
    gauges_[std::string(name) + ".last"] = value;
}

double MemoryMetrics::counter(std::string_view name) const {
    std::lock_guard lock(mutex_);
    const auto it = counters_.find(std::string(name));
    return it == counters_.end() ? 0.0 : it->second;
}

double MemoryMetrics::gauge(std::string_view name) const {
    std::lock_guard lock(mutex_);
    const auto it = gauges_.find(std::string(name));
    return it == gauges_.end() ? 0.0 : it->second;
}

}  // namespace opc::adapters
