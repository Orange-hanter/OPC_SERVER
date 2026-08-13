#pragma once

#include "ports/i_modbus_transport.hpp"

#include <chrono>
#include <optional>
#include <thread>
#include <unordered_map>

namespace opc::adapters::testsupport {

/// Deterministic transport for component tests (stage 2 expands behavior).
class FakeModbusTransport final : public ports::IModbusTransport {
public:
    domain::Result<void> connect(const ports::EndpointAddress&) override {
        ++connect_attempts_;
        if (connect_result_.has_value()) {
            auto result = *connect_result_;
            if (result) {
                connected_ = true;
            }
            return result;
        }
        connected_ = true;
        return {};
    }

    void close() override { connected_ = false; }

    [[nodiscard]] bool is_connected() const override { return connected_; }

    domain::Result<std::vector<std::uint16_t>>
    read_holding_registers(std::uint8_t, std::uint16_t address, std::uint16_t quantity) override {
        if (!connected_) {
            return std::unexpected(domain::Error{
                domain::ErrorCode::Connection, "not connected", "fake.modbus", true});
        }
        maybe_delay();
        std::vector<std::uint16_t> out(quantity, 0);
        for (std::uint16_t i = 0; i < quantity; ++i) {
            const auto it = holding_.find(static_cast<std::uint16_t>(address + i));
            if (it != holding_.end()) {
                out[i] = it->second;
            }
        }
        return out;
    }

    domain::Result<std::vector<std::uint16_t>>
    read_input_registers(std::uint8_t unit, std::uint16_t address, std::uint16_t quantity) override {
        return read_holding_registers(unit, address, quantity);
    }

    domain::Result<std::vector<bool>>
    read_coils(std::uint8_t, std::uint16_t, std::uint16_t quantity) override {
        return std::vector<bool>(quantity, false);
    }

    domain::Result<std::vector<bool>>
    read_discrete_inputs(std::uint8_t, std::uint16_t, std::uint16_t quantity) override {
        return std::vector<bool>(quantity, false);
    }

    domain::Result<void>
    write_single_register(std::uint8_t, std::uint16_t address, std::uint16_t value) override {
        holding_[address] = value;
        return {};
    }

    domain::Result<void>
    write_multiple_registers(std::uint8_t,
                             std::uint16_t address,
                             std::span<const std::uint16_t> values) override {
        for (std::size_t i = 0; i < values.size(); ++i) {
            holding_[static_cast<std::uint16_t>(address + i)] = values[i];
        }
        return {};
    }

    domain::Result<void>
    write_single_coil(std::uint8_t, std::uint16_t, bool) override {
        return {};
    }

    void set_holding(std::uint16_t address, std::uint16_t value) { holding_[address] = value; }

    void set_read_delay(std::chrono::milliseconds delay) { read_delay_ = delay; }
    void set_connect_result(domain::Result<void> result) { connect_result_ = std::move(result); }
    [[nodiscard]] int connect_attempts() const { return connect_attempts_; }

private:
    void maybe_delay() const {
        if (read_delay_.count() > 0) {
            std::this_thread::sleep_for(read_delay_);
        }
    }

    bool connected_{false};
    std::chrono::milliseconds read_delay_{0};
    std::optional<domain::Result<void>> connect_result_;
    int connect_attempts_{0};
    std::unordered_map<std::uint16_t, std::uint16_t> holding_;
};

}  // namespace opc::adapters::testsupport
