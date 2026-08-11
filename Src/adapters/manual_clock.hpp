#pragma once

#include "ports/i_clock.hpp"

namespace opc::adapters {

/// Deterministic clock for tests (ADR-0004).
class ManualClock final : public ports::IClock {
public:
    explicit ManualClock(domain::TimestampMs start_ms = 0) : now_(start_ms) {}

    [[nodiscard]] domain::TimestampMs now_ms() const override { return now_; }

    void set_now_ms(domain::TimestampMs value) { now_ = value; }
    void advance_ms(domain::TimestampMs delta) { now_ += delta; }

private:
    domain::TimestampMs now_{0};
};

}  // namespace opc::adapters
