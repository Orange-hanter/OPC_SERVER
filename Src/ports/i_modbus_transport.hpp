#pragma once

#include "domain/types.hpp"
#include "ports/i_executor.hpp"

#include <cstdint>
#include <functional>
#include <span>
#include <string>
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

/// Completion token for async Modbus operations (ADR-0007). Invoked once with Result.
/// May run on an I/O thread unless the adapter posts through `IExecutor` (endpoint strand).
template <typename T>
using ModbusCompletion = std::move_only_function<void(domain::Result<T>)>;

/// Southbound port. Sync methods stay for Fake/Replay/`--once`.
/// Async methods must not block the calling strand when a completion executor is wired;
/// default implementations call the sync API and invoke the handler inline (OK for Fake/UDP).
class IModbusTransport {
public:
    virtual ~IModbusTransport() = default;

    /// Optional: post async completions onto the endpoint strand (reactor).
    virtual void set_completion_executor(IExecutor* /*executor*/) {}

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

    // --- Async API (completion tokens) -------------------------------------------------

    virtual void async_connect(EndpointAddress endpoint, ModbusCompletion<void> handler) {
        if (handler) {
            handler(connect(endpoint));
        }
    }

    virtual void async_read_holding_registers(std::uint8_t unit,
                                              std::uint16_t address,
                                              std::uint16_t quantity,
                                              ModbusCompletion<std::vector<std::uint16_t>> handler) {
        if (handler) {
            handler(read_holding_registers(unit, address, quantity));
        }
    }

    virtual void async_read_input_registers(std::uint8_t unit,
                                            std::uint16_t address,
                                            std::uint16_t quantity,
                                            ModbusCompletion<std::vector<std::uint16_t>> handler) {
        if (handler) {
            handler(read_input_registers(unit, address, quantity));
        }
    }

    virtual void async_read_coils(std::uint8_t unit,
                                  std::uint16_t address,
                                  std::uint16_t quantity,
                                  ModbusCompletion<std::vector<bool>> handler) {
        if (handler) {
            handler(read_coils(unit, address, quantity));
        }
    }

    virtual void async_read_discrete_inputs(std::uint8_t unit,
                                            std::uint16_t address,
                                            std::uint16_t quantity,
                                            ModbusCompletion<std::vector<bool>> handler) {
        if (handler) {
            handler(read_discrete_inputs(unit, address, quantity));
        }
    }

    virtual void async_write_single_register(std::uint8_t unit,
                                             std::uint16_t address,
                                             std::uint16_t value,
                                             ModbusCompletion<void> handler) {
        if (handler) {
            handler(write_single_register(unit, address, value));
        }
    }

    virtual void async_write_multiple_registers(std::uint8_t unit,
                                                std::uint16_t address,
                                                std::vector<std::uint16_t> values,
                                                ModbusCompletion<void> handler) {
        if (handler) {
            handler(write_multiple_registers(unit, address, values));
        }
    }

    virtual void async_write_single_coil(std::uint8_t unit,
                                         std::uint16_t address,
                                         bool value,
                                         ModbusCompletion<void> handler) {
        if (handler) {
            handler(write_single_coil(unit, address, value));
        }
    }

    virtual void async_write_multiple_coils(std::uint8_t unit,
                                            std::uint16_t address,
                                            std::vector<std::uint8_t> values,
                                            ModbusCompletion<void> handler) {
        if (handler) {
            handler(write_multiple_coils(unit, address, values));
        }
    }
};

}  // namespace opc::ports
