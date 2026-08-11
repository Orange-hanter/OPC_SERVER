#pragma once

#include "domain/types.hpp"

namespace opc::ports {

class IClock {
public:
    virtual ~IClock() = default;
    [[nodiscard]] virtual domain::TimestampMs now_ms() const = 0;
};

}  // namespace opc::ports
