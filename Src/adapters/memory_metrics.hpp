#pragma once

#include "ports/i_metrics.hpp"

#include <mutex>
#include <string>
#include <unordered_map>

namespace opc::adapters {

/// Simple in-process metrics sink (Stage 5 stub; OTel/spdlog can wrap later).
class MemoryMetrics final : public ports::IMetrics {
public:
    void counter_add(std::string_view name, double value = 1.0) override;
    void gauge_set(std::string_view name, double value) override;
    void histogram_observe(std::string_view name, double value) override;

    [[nodiscard]] double counter(std::string_view name) const;
    [[nodiscard]] double gauge(std::string_view name) const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, double> counters_;
    std::unordered_map<std::string, double> gauges_;
};

}  // namespace opc::adapters
