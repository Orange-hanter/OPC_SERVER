#pragma once

#include "ports/i_historian.hpp"
#include "ports/i_metrics.hpp"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace opc::adapters {

/// In-RAM ring buffer historian (hot layer). Thread-safe for concurrent record/recent.
class RingHistorian final : public ports::IHistorian {
public:
    explicit RingHistorian(std::size_t capacity = 4096, ports::IMetrics* metrics = nullptr);

    void record(domain::TagId id, const domain::TagValue& value) override;
    domain::Result<void> flush() override;
    [[nodiscard]] std::vector<ports::HistorianSample> recent(std::size_t max) const override;
    [[nodiscard]] std::uint64_t dropped() const override;

    [[nodiscard]] std::size_t capacity() const { return capacity_; }
    [[nodiscard]] std::size_t size() const;

    /// Oldest-first copy of all resident samples (for replay / cold export).
    [[nodiscard]] std::vector<ports::HistorianSample> snapshot() const;

private:
    std::size_t capacity_;
    ports::IMetrics* metrics_{nullptr};
    mutable std::mutex mutex_;
    std::vector<ports::HistorianSample> buffer_;
    std::size_t head_{0};
    std::size_t count_{0};
    std::uint64_t dropped_{0};
};

}  // namespace opc::adapters
