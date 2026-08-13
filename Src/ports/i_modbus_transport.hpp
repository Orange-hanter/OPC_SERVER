#pragma once

#include "domain/types.hpp"

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace opc::ports {

struct EndpointAddress {
    std::string host;
    std::uint16_t port{502};
};

struct ModbusRequestId {
    std::uint64_t value{0};
};

/// Southbound port. All methods for a given instance are called on one endpoint strand.
class IModbusTransport {
public:
    virtual ~IModbusTransport() = default;

    virtual domain::Result<void> connect(const EndpointAddress& endpoint) = 0;
    virtual void close() = 0;
    [[nodiscard]] virtual bool is_connected() const = 0;

    virtual domain::Result<std::vector<std::uint16_t>>
    read_holding_registers(std::uint8_t unit, std::uint16_t address, std::uint16_t quantity) = 0;

    virtual domain::Result<std::vector<std::uint16_t>>
    read_input_registers(std::uint8_t unit, std::uint16_t address, std::uint16_t quantity) = 0;

    virtual domain::Result<std::vector<bool>>
    read_coils(std::uint8_t unit, std::uint16_t address, std::uint16_t quantity) = 0;

    virtual domain::Result<std::vector<bool>>
    read_discrete_inputs(std::uint8_t unit, std::uint16_t address, std::uint16_t quantity) = 0;

    virtual domain::Result<void>
    write_single_register(std::uint8_t unit, std::uint16_t address, std::uint16_t value) = 0;

    virtual domain::Result<void>
    write_multiple_registers(std::uint8_t unit,
                             std::uint16_t address,
                             std::span<const std::uint16_t> values) = 0;

    virtual domain::Result<void>
    write_single_coil(std::uint8_t unit, std::uint16_t address, bool value) = 0;

    /// FC15. `values` are 0/1 bytes (non-zero = ON). Empty span is an error.
    virtual domain::Result<void>
    write_multiple_coils(std::uint8_t unit,
                         std::uint16_t address,
                         std::span<const std::uint8_t> values) = 0;
};

}  // namespace opc::ports
