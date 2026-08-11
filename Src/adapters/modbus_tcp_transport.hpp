#pragma once

#include "ports/i_frame_log.hpp"
#include "ports/i_modbus_transport.hpp"

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace opc::adapters {

struct ModbusTcpTransportOptions {
    int response_timeout_ms{1000};
    ports::IFrameLog* frame_log{nullptr};
    std::string endpoint_id;
};

/// Synchronous Modbus TCP client (call only from one endpoint strand — ADR-0002/0007).
class ModbusTcpTransport final : public ports::IModbusTransport {
public:
    explicit ModbusTcpTransport(int response_timeout_ms = 1000);
    explicit ModbusTcpTransport(ModbusTcpTransportOptions options);

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

    void set_frame_log(ports::IFrameLog* log) { frame_log_ = log; }
    void set_endpoint_id(std::string id) { endpoint_id_ = std::move(id); }

private:
    domain::Result<std::vector<std::uint8_t>>
    transact(std::uint8_t unit, std::span<const std::uint8_t> pdu);

    domain::Result<std::vector<std::uint16_t>>
    read_registers(std::uint8_t function, std::uint8_t unit, std::uint16_t address, std::uint16_t quantity);

    domain::Result<std::vector<bool>>
    read_bits(std::uint8_t function, std::uint8_t unit, std::uint16_t address, std::uint16_t quantity);

    void emit_frame(ports::FrameRecord frame);

    int fd_{-1};
    int response_timeout_ms_{1000};
    std::uint16_t transaction_id_{1};
    mutable std::mutex mutex_;
    ports::IFrameLog* frame_log_{nullptr};
    std::string endpoint_id_;
};

}  // namespace opc::adapters
