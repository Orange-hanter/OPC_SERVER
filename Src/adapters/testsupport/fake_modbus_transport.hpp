#pragma once

#include "ports/i_modbus_transport.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace opc::adapters::testsupport {

/// Deterministic in-memory Modbus slave for component tests (ADR-0004).
class FakeModbusTransport final : public ports::IModbusTransport {
public:
    domain::Result<void> connect(const ports::EndpointAddress&) override {
        if (fail_connect_) {
            fail_connect_ = false;
            return std::unexpected(domain::Error{
                domain::ErrorCode::Connection, "injected connect failure", "fake.modbus", true});
        }
        connected_ = true;
        return {};
    }

    void close() override { connected_ = false; }

    [[nodiscard]] bool is_connected() const override { return connected_; }

    domain::Result<std::vector<std::uint16_t>>
    read_holding_registers(std::uint8_t unit, std::uint16_t address, std::uint16_t quantity) override {
        if (auto fail = consume_failure()) {
            return std::unexpected(*fail);
        }
        return read_words(holding_, unit, address, quantity);
    }

    domain::Result<std::vector<std::uint16_t>>
    read_input_registers(std::uint8_t unit, std::uint16_t address, std::uint16_t quantity) override {
        if (auto fail = consume_failure()) {
            return std::unexpected(*fail);
        }
        return read_words(input_, unit, address, quantity);
    }

    domain::Result<std::vector<bool>>
    read_coils(std::uint8_t unit, std::uint16_t address, std::uint16_t quantity) override {
        if (auto fail = consume_failure()) {
            return std::unexpected(*fail);
        }
        return read_bits(coils_, unit, address, quantity);
    }

    domain::Result<std::vector<bool>>
    read_discrete_inputs(std::uint8_t unit, std::uint16_t address, std::uint16_t quantity) override {
        if (auto fail = consume_failure()) {
            return std::unexpected(*fail);
        }
        return read_bits(discrete_, unit, address, quantity);
    }

    domain::Result<void>
    write_single_register(std::uint8_t unit, std::uint16_t address, std::uint16_t value) override {
        if (auto fail = consume_failure()) {
            return std::unexpected(*fail);
        }
        if (!connected_) {
            return not_connected();
        }
        holding_[key(unit, address)] = value;
        return {};
    }

    domain::Result<void>
    write_multiple_registers(std::uint8_t unit,
                             std::uint16_t address,
                             std::span<const std::uint16_t> values) override {
        if (auto fail = consume_failure()) {
            return std::unexpected(*fail);
        }
        if (!connected_) {
            return not_connected();
        }
        for (std::size_t i = 0; i < values.size(); ++i) {
            holding_[key(unit, static_cast<std::uint16_t>(address + i))] = values[i];
        }
        return {};
    }

    domain::Result<void>
    write_single_coil(std::uint8_t unit, std::uint16_t address, bool value) override {
        if (auto fail = consume_failure()) {
            return std::unexpected(*fail);
        }
        if (!connected_) {
            return not_connected();
        }
        coils_[key(unit, address)] = value;
        return {};
    }

    void set_holding(std::uint16_t address, std::uint16_t value) { set_holding(1, address, value); }
    void set_holding(std::uint8_t unit, std::uint16_t address, std::uint16_t value) {
        holding_[key(unit, address)] = value;
    }
    void set_input(std::uint8_t unit, std::uint16_t address, std::uint16_t value) {
        input_[key(unit, address)] = value;
    }
    void set_coil(std::uint8_t unit, std::uint16_t address, bool value) {
        coils_[key(unit, address)] = value;
    }
    void set_discrete(std::uint8_t unit, std::uint16_t address, bool value) {
        discrete_[key(unit, address)] = value;
    }

    void fail_next(domain::Error error) { next_error_ = std::move(error); }
    void fail_connect_once() { fail_connect_ = true; }

    [[nodiscard]] std::uint16_t holding_at(std::uint8_t unit, std::uint16_t address) const {
        const auto it = holding_.find(key(unit, address));
        return it == holding_.end() ? 0 : it->second;
    }
    [[nodiscard]] bool coil_at(std::uint8_t unit, std::uint16_t address) const {
        const auto it = coils_.find(key(unit, address));
        return it != coils_.end() && it->second;
    }

private:
    using Key = std::uint32_t;

    static Key key(std::uint8_t unit, std::uint16_t address) {
        return (static_cast<Key>(unit) << 16) | address;
    }

    static domain::Result<void> not_connected() {
        return std::unexpected(domain::Error{
            domain::ErrorCode::Connection, "not connected", "fake.modbus", true});
    }

    std::optional<domain::Error> consume_failure() {
        if (!next_error_) {
            if (!connected_) {
                return domain::Error{
                    domain::ErrorCode::Connection, "not connected", "fake.modbus", true};
            }
            return std::nullopt;
        }
        auto err = *next_error_;
        next_error_.reset();
        return err;
    }

    domain::Result<std::vector<std::uint16_t>>
    read_words(const std::unordered_map<Key, std::uint16_t>& map,
               std::uint8_t unit,
               std::uint16_t address,
               std::uint16_t quantity) const {
        if (!connected_) {
            return std::unexpected(domain::Error{
                domain::ErrorCode::Connection, "not connected", "fake.modbus", true});
        }
        std::vector<std::uint16_t> out(quantity, 0);
        for (std::uint16_t i = 0; i < quantity; ++i) {
            const auto it = map.find(key(unit, static_cast<std::uint16_t>(address + i)));
            if (it != map.end()) {
                out[i] = it->second;
            }
        }
        return out;
    }

    domain::Result<std::vector<bool>>
    read_bits(const std::unordered_map<Key, bool>& map,
              std::uint8_t unit,
              std::uint16_t address,
              std::uint16_t quantity) const {
        if (!connected_) {
            return std::unexpected(domain::Error{
                domain::ErrorCode::Connection, "not connected", "fake.modbus", true});
        }
        std::vector<bool> out(quantity, false);
        for (std::uint16_t i = 0; i < quantity; ++i) {
            const auto it = map.find(key(unit, static_cast<std::uint16_t>(address + i)));
            if (it != map.end()) {
                out[i] = it->second;
            }
        }
        return out;
    }

    bool connected_{false};
    bool fail_connect_{false};
    std::optional<domain::Error> next_error_;
    std::unordered_map<Key, std::uint16_t> holding_;
    std::unordered_map<Key, std::uint16_t> input_;
    std::unordered_map<Key, bool> coils_;
    std::unordered_map<Key, bool> discrete_;
};

}  // namespace opc::adapters::testsupport
