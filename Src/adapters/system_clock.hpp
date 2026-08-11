#pragma once

#include "ports/i_clock.hpp"

#include <chrono>

namespace opc::adapters {

class SystemClock final : public ports::IClock {
public:
    [[nodiscard]] domain::TimestampMs now_ms() const override {
        using namespace std::chrono;
        return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
    }
};

}  // namespace opc::adapters
