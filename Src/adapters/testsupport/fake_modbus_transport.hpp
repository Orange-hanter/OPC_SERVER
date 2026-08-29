#pragma once

#include "ports/i_modbus_transport.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <thread>
#include <unordered_map>
#include <vector>

namespace opc::adapters::testsupport {

/// Deterministic in-memory Modbus slave for component tests (ADR-0004).
/// Unit-keyed maps + fault injection from the testing program; atomics / connect
/// override / FC15 counters from the post-core stack (TSan + increment B).
class FakeModbusTransport final : public ports::IModbusTransport {
public:
    domain::Result<void> connect(const ports::EndpointAddress&) override {
        connect_attempts_.fetch_add(1, std::memory_order_relaxed);
        if (fail_connect_) {
            fail_connect_ = false;
            return std::unexpected(domain::Error{
                domain::ErrorCode::Connection, "injected connect failure", "fake.modbus", true});
        }
        if (connect_result_.has_value()) {
            auto result = *connect_result_;
            if (result) {
                connected_.store(true, std::memory_order_relaxed);
            }
            return result;
        }
        connected_.store(true, std::memory_order_relaxed);
        return {};
    }

    void close() override { connected_.store(false, std::memory_order_relaxed); }

    [[nodiscard]] bool is_connected() const override {
        return connected_.load(std::memory_order_relaxed);
    }

    domain::Result<std::vector<std::uint16_t>>
    read_holding_registers(std::uint8_t unit, std::uint16_t address, std::uint16_t quantity) override {
        if (auto fail = consume_failure()) {
            return std::unexpected(*fail);
        }
        maybe_delay();
        return read_words(holding_, unit, address, quantity);
    }

    domain::Result<std::vector<std::uint16_t>>
    read_input_registers(std::uint8_t unit, std::uint16_t address, std::uint16_t quantity) override {
        if (auto fail = consume_failure()) {
            return std::unexpected(*fail);
        }
        maybe_delay();
        return read_words(input_, unit, address, quantity);
    }

    domain::Result<std::vector<bool>>
    read_coils(std::uint8_t unit, std::uint16_t address, std::uint16_t quantity) override {
        if (auto fail = consume_failure()) {
            return std::unexpected(*fail);
        }
        maybe_delay();
        return read_bits(coils_, unit, address, quantity);
    }

    domain::Result<std::vector<bool>>
    read_discrete_inputs(std::uint8_t unit, std::uint16_t address, std::uint16_t quantity) override {
        if (auto fail = consume_failure()) {
            return std::unexpected(*fail);
        }
        maybe_delay();
        return read_bits(discrete_, unit, address, quantity);
    }

    domain::Result<void>
    write_single_register(std::uint8_t unit, std::uint16_t address, std::uint16_t value) override {
        if (auto fail = consume_failure()) {
            return std::unexpected(*fail);
        }
        if (!connected_.load(std::memory_order_relaxed)) {
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
        if (!connected_.load(std::memory_order_relaxed)) {
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
        if (!connected_.load(std::memory_order_relaxed)) {
            return not_connected();
        }
        coils_[key(unit, address)] = value;
        ++fc05_writes_;
        return {};
    }

    domain::Result<void>
    write_multiple_coils(std::uint8_t unit,
                         std::uint16_t address,
                         std::span<const std::uint8_t> values) override {
        if (auto fail = consume_failure()) {
            return std::unexpected(*fail);
        }
        if (!connected_.load(std::memory_order_relaxed)) {
            return not_connected();
        }
        if (values.empty()) {
            return std::unexpected(domain::Error{
                domain::ErrorCode::InvalidArgument, "empty coil write", "fake.modbus", false});
        }
        for (std::size_t i = 0; i < values.size(); ++i) {
            coils_[key(unit, static_cast<std::uint16_t>(address + i))] = values[i] != 0;
        }
        ++fc15_writes_;
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

    void set_read_delay(std::chrono::milliseconds delay) { read_delay_ = delay; }
    void set_connect_result(domain::Result<void> result) { connect_result_ = std::move(result); }
    [[nodiscard]] int connect_attempts() const {
        return connect_attempts_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] int fc05_writes() const { return fc05_writes_; }
    [[nodiscard]] int fc15_writes() const { return fc15_writes_; }

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

    void maybe_delay() const {
        if (read_delay_.count() > 0) {
            std::this_thread::sleep_for(read_delay_);
        }
    }

    std::optional<domain::Error> consume_failure() {
        if (!next_error_) {
            if (!connected_.load(std::memory_order_relaxed)) {
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
        if (!connected_.load(std::memory_order_relaxed)) {
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
        if (!connected_.load(std::memory_order_relaxed)) {
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

    std::atomic<bool> connected_{false};
    bool fail_connect_{false};
    std::optional<domain::Error> next_error_;
    std::chrono::milliseconds read_delay_{0};
    std::optional<domain::Result<void>> connect_result_;
    std::atomic<int> connect_attempts_{0};
    int fc05_writes_{0};
    int fc15_writes_{0};
    std::unordered_map<Key, std::uint16_t> holding_;
    std::unordered_map<Key, std::uint16_t> input_;
    std::unordered_map<Key, bool> coils_;
    std::unordered_map<Key, bool> discrete_;
};

}  // namespace opc::adapters::testsupport
