#pragma once

#include "domain/types.hpp"

#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace opc::adapters {

using ModbusTransact = std::function<domain::Result<std::vector<std::uint8_t>>(
    std::uint8_t unit, std::span<const std::uint8_t> pdu)>;

[[nodiscard]] std::vector<std::uint8_t>
pack_mbap(std::uint16_t transaction_id, std::uint8_t unit, std::span<const std::uint8_t> pdu);

struct UnpackedAdu {
    std::uint16_t transaction_id{0};
    std::uint8_t unit{0};
    std::vector<std::uint8_t> pdu;  // function + data (no unit)
};

[[nodiscard]] domain::Result<UnpackedAdu> unpack_adu(std::span<const std::uint8_t> adu);

[[nodiscard]] domain::Result<std::vector<std::uint16_t>>
modbus_read_registers(const ModbusTransact& tx,
                      std::uint8_t function,
                      std::uint8_t unit,
                      std::uint16_t address,
                      std::uint16_t quantity);

[[nodiscard]] domain::Result<std::vector<bool>>
modbus_read_bits(const ModbusTransact& tx,
                 std::uint8_t function,
                 std::uint8_t unit,
                 std::uint16_t address,
                 std::uint16_t quantity);

[[nodiscard]] domain::Result<void>
modbus_write_single_register(const ModbusTransact& tx,
                             std::uint8_t unit,
                             std::uint16_t address,
                             std::uint16_t value);

[[nodiscard]] domain::Result<void>
modbus_write_multiple_registers(const ModbusTransact& tx,
                                std::uint8_t unit,
                                std::uint16_t address,
                                std::span<const std::uint16_t> values);

[[nodiscard]] domain::Result<void>
modbus_write_single_coil(const ModbusTransact& tx,
                         std::uint8_t unit,
                         std::uint16_t address,
                         bool value);

[[nodiscard]] domain::Result<void>
modbus_write_multiple_coils(const ModbusTransact& tx,
                            std::uint8_t unit,
                            std::uint16_t address,
                            std::span<const std::uint8_t> values);

}  // namespace opc::adapters
