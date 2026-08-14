#pragma once

#include "ports/i_frame_log.hpp"
#include "ports/i_modbus_transport.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace opc::adapters {

struct ModbusTcpTransportOptions {
    int response_timeout_ms{1000};
    ports::IFrameLog* frame_log{nullptr};
    std::string endpoint_id;
};

/// Asio-native Modbus TCP client (ADR-0007). Public API stays synchronous so
/// Dispatcher can call it from an endpoint strand; I/O uses a private
/// `io_context` + async connect/read/write (does not nest on the reactor).
/// Call only from one endpoint strand (ADR-0002).
class ModbusTcpTransport final : public ports::IModbusTransport {
public:
    explicit ModbusTcpTransport(int response_timeout_ms = 1000);
    explicit ModbusTcpTransport(ModbusTcpTransportOptions options);
    ~ModbusTcpTransport() override;

    ModbusTcpTransport(const ModbusTcpTransport&) = delete;
    ModbusTcpTransport& operator=(const ModbusTcpTransport&) = delete;

    domain::Result<void> connect(const ports::EndpointAddress& endpoint) override;
    void close() override;
    [[nodiscard]] bool is_connected() const override;

    domain::Result<std::vector<std::uint16_t>>
    read_holding_registers(std::uint8_t unit, std::uint16_t address, std::uint16_t quantity) override;

    domain::Result<std::vector<std::uint16_t>>
    read_input_registers(std::uint8_t unit, std::uint16_t address, std::uint16_t quantity) override;

    domain::Result<std::vector<bool>>
    read_coils(std::uint8_t unit, std::uint16_t address, std::uint16_t quantity) override;

    domain::Result<std::vector<bool>>
    read_discrete_inputs(std::uint8_t unit, std::uint16_t address, std::uint16_t quantity) override;

    domain::Result<void>
    write_single_register(std::uint8_t unit, std::uint16_t address, std::uint16_t value) override;

    domain::Result<void>
    write_multiple_registers(std::uint8_t unit,
                             std::uint16_t address,
                             std::span<const std::uint16_t> values) override;

    domain::Result<void>
    write_single_coil(std::uint8_t unit, std::uint16_t address, bool value) override;

    domain::Result<void>
    write_multiple_coils(std::uint8_t unit,
                         std::uint16_t address,
                         std::span<const std::uint8_t> values) override;

    void set_frame_log(ports::IFrameLog* log);
    void set_endpoint_id(std::string id);

private:
    domain::Result<std::vector<std::uint8_t>>
    transact(std::uint8_t unit, std::span<const std::uint8_t> pdu);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// ADR-0007 name for the default TCP adapter.
using AsioModbusTcpTransport = ModbusTcpTransport;

}  // namespace opc::adapters
