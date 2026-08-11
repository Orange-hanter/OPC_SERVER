#pragma once

#include "domain/types.hpp"
#include "project/types.hpp"

#include <span>
#include <vector>

namespace opc::core {

/// Pure codec: registers <-> engineering values. No I/O.
class Translator {
public:
    [[nodiscard]] static domain::Result<domain::ScalarValue>
    decode(const project::Tag& tag, std::span<const std::uint16_t> registers);

    [[nodiscard]] static domain::Result<std::vector<std::uint16_t>>
    encode(const project::Tag& tag, const domain::ScalarValue& engineering_value);
};

}  // namespace opc::core
