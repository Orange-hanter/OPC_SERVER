#pragma once

#include "ports/i_frame_log.hpp"
#include "ports/i_modbus_transport.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace opc::adapters {

/// Parse a FileFrameLog text journal (comments starting with '#' are skipped).
[[nodiscard]] domain::Result<std::vector<ports::FrameRecord>>
load_frame_log_file(const std::string& path);

[[nodiscard]] domain::Result<ports::FrameRecord>
parse_frame_log_line(std::string_view line);

/// Replay recorded Modbus TCP frames as an `IModbusTransport` (no field I/O).
class ReplayModbusTransport final : public ports::IModbusTransport {
public:
    explicit ReplayModbusTransport(std::vector<ports::FrameRecord> frames);

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

    [[nodiscard]] std::size_t remaining() const { return frames_.size() - next_; }

private:
    domain::Result<ports::FrameRecord> consume();
    domain::Result<std::vector<std::uint8_t>> next_pdu();

    std::vector<ports::FrameRecord> frames_;
    std::size_t next_{0};
    bool connected_{false};
};

}  // namespace opc::adapters
