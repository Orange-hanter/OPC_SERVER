#include "adapters/frame_replay.hpp"

#include <cctype>
#include <cstddef>
#include <fstream>
#include <sstream>
#include <string_view>

namespace opc::adapters {
namespace {

domain::Error replay_err(domain::ErrorCode code, std::string message, bool retryable = false) {
    return domain::Error{code, std::move(message), "adapters.frame_replay", retryable};
}

int hex_nibble(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

domain::Result<std::vector<std::uint8_t>> parse_hex_bytes(std::string_view text) {
    std::vector<std::uint8_t> out;
    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i])) != 0) {
            ++i;
        }
        if (i >= text.size()) {
            break;
        }
        if (i + 1 >= text.size()) {
            return std::unexpected(replay_err(domain::ErrorCode::Decoding, "odd hex nibble"));
        }
        const int hi = hex_nibble(text[i]);
        const int lo = hex_nibble(text[i + 1]);
        if (hi < 0 || lo < 0) {
            return std::unexpected(replay_err(domain::ErrorCode::Decoding, "invalid hex byte"));
        }
        out.push_back(static_cast<std::uint8_t>((hi << 4) | lo));
        i += 2;
    }
    return out;
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())) != 0) {
        s.remove_prefix(1);
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())) != 0) {
        s.remove_suffix(1);
    }
    return s;
}

domain::Result<std::vector<std::uint8_t>> pdu_from_rx(const ports::FrameRecord& frame) {
    if (frame.error) {
        return std::unexpected(replay_err(domain::ErrorCode::Connection, *frame.error, true));
    }
    if (frame.exception_code) {
        domain::Error err = replay_err(domain::ErrorCode::ModbusException, "modbus exception", true);
        err.protocol_status = *frame.exception_code;
        return std::unexpected(err);
    }
    if (frame.rx.size() < 8) {
        return std::unexpected(replay_err(domain::ErrorCode::Decoding, "short replay RX"));
    }
    const std::uint16_t length =
        static_cast<std::uint16_t>((frame.rx[4] << 8) | frame.rx[5]);
    // Canonical MBAP: 6-byte prefix + `length` (unit + PDU) → PDU at offset 7.
    // ModbusTcpTransport journal: 7-byte header + `length` body (unit again) → PDU at 8.
    std::size_t pdu_off = 7;
    if (frame.rx.size() == static_cast<std::size_t>(7 + length) && length >= 2) {
        pdu_off = 8;
    } else if (frame.rx.size() != static_cast<std::size_t>(6 + length)) {
        return std::unexpected(replay_err(domain::ErrorCode::Decoding, "RX length mismatch"));
    }
    if (frame.rx.size() <= pdu_off) {
        return std::unexpected(replay_err(domain::ErrorCode::Decoding, "short replay PDU"));
    }
    return std::vector<std::uint8_t>(frame.rx.begin() + static_cast<std::ptrdiff_t>(pdu_off),
                                     frame.rx.end());
}

domain::Result<std::vector<std::uint16_t>> decode_registers(const std::vector<std::uint8_t>& pdu,
                                                            std::uint16_t quantity) {
    if (pdu.size() < 2) {
        return std::unexpected(replay_err(domain::ErrorCode::Decoding, "short register PDU"));
    }
    const auto byte_count = pdu[1];
    if (pdu.size() < 2u + byte_count || byte_count != quantity * 2) {
        return std::unexpected(replay_err(domain::ErrorCode::Decoding, "register byte count mismatch"));
    }
    std::vector<std::uint16_t> out;
    out.reserve(quantity);
    for (std::uint16_t i = 0; i < quantity; ++i) {
        const auto hi = pdu[2 + i * 2];
        const auto lo = pdu[3 + i * 2];
        out.push_back(static_cast<std::uint16_t>((hi << 8) | lo));
    }
    return out;
}

domain::Result<std::vector<bool>> decode_bits(const std::vector<std::uint8_t>& pdu,
                                              std::uint16_t quantity) {
    if (pdu.size() < 2) {
        return std::unexpected(replay_err(domain::ErrorCode::Decoding, "short bit PDU"));
    }
    const auto byte_count = pdu[1];
    if (pdu.size() < 2u + byte_count) {
        return std::unexpected(replay_err(domain::ErrorCode::Decoding, "bit byte count mismatch"));
    }
    std::vector<bool> out;
    out.reserve(quantity);
    for (std::uint16_t i = 0; i < quantity; ++i) {
        const auto byte = pdu[2 + i / 8];
        out.push_back(((byte >> (i % 8)) & 0x1u) != 0);
    }
    return out;
}

}  // namespace

domain::Result<ports::FrameRecord> parse_frame_log_line(std::string_view line) {
    line = trim(line);
    if (line.empty() || line.front() == '#') {
        return std::unexpected(replay_err(domain::ErrorCode::InvalidArgument, "comment or empty"));
    }
    const auto bar = line.find('|');
    if (bar == std::string_view::npos) {
        return std::unexpected(replay_err(domain::ErrorCode::Decoding, "missing TX/RX separator"));
    }

    std::istringstream prefix{std::string(line.substr(0, bar))};
    ports::FrameRecord frame;
    std::string exception;
    std::string error;
    if (!(prefix >> frame.ts_ms >> frame.endpoint_id >> frame.rtt_ms >> exception >> error)) {
        return std::unexpected(replay_err(domain::ErrorCode::Decoding, "bad frame log prefix"));
    }
    if (exception != "-") {
        try {
            frame.exception_code = std::stoi(exception);
        } catch (...) {
            return std::unexpected(replay_err(domain::ErrorCode::Decoding, "bad exception code"));
        }
    }
    if (error != "-") {
        if (!error.empty() && error.front() == '"') {
            if (error.size() >= 2 && error.back() == '"') {
                frame.error = error.substr(1, error.size() - 2);
            } else {
                frame.error = error.substr(1);
            }
        } else {
            frame.error = error;
        }
    }

    std::string rest;
    std::getline(prefix, rest);
    auto tx = parse_hex_bytes(trim(rest));
    if (!tx) {
        return std::unexpected(tx.error());
    }
    frame.tx = std::move(*tx);

    auto rx = parse_hex_bytes(trim(line.substr(bar + 1)));
    if (!rx) {
        return std::unexpected(rx.error());
    }
    frame.rx = std::move(*rx);
    return frame;
}

domain::Result<std::vector<ports::FrameRecord>> load_frame_log_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        return std::unexpected(replay_err(domain::ErrorCode::NotFound, "cannot open frame log: " + path));
    }
    std::vector<ports::FrameRecord> out;
    std::string line;
    while (std::getline(in, line)) {
        const auto trimmed = trim(line);
        if (trimmed.empty() || trimmed.front() == '#') {
            continue;
        }
        auto parsed = parse_frame_log_line(trimmed);
        if (!parsed) {
            return std::unexpected(parsed.error());
        }
        out.push_back(std::move(*parsed));
    }
    return out;
}

ReplayModbusTransport::ReplayModbusTransport(std::vector<ports::FrameRecord> frames)
    : frames_(std::move(frames)) {}

domain::Result<void> ReplayModbusTransport::connect(const ports::EndpointAddress&) {
    connected_ = true;
    return {};
}

void ReplayModbusTransport::close() {
    connected_ = false;
}

bool ReplayModbusTransport::is_connected() const {
    return connected_;
}

domain::Result<ports::FrameRecord> ReplayModbusTransport::consume() {
    if (!connected_) {
        return std::unexpected(replay_err(domain::ErrorCode::Connection, "not connected", true));
    }
    if (next_ >= frames_.size()) {
        return std::unexpected(replay_err(domain::ErrorCode::NotFound, "replay exhausted"));
    }
    return frames_[next_++];
}

domain::Result<std::vector<std::uint8_t>> ReplayModbusTransport::next_pdu() {
    auto frame = consume();
    if (!frame) {
        return std::unexpected(frame.error());
    }
    return pdu_from_rx(*frame);
}

domain::Result<std::vector<std::uint16_t>>
ReplayModbusTransport::read_holding_registers(std::uint8_t, std::uint16_t, std::uint16_t quantity) {
    auto pdu = next_pdu();
    if (!pdu) {
        return std::unexpected(pdu.error());
    }
    return decode_registers(*pdu, quantity);
}

domain::Result<std::vector<std::uint16_t>>
ReplayModbusTransport::read_input_registers(std::uint8_t unit,
                                            std::uint16_t address,
                                            std::uint16_t quantity) {
    return read_holding_registers(unit, address, quantity);
}

domain::Result<std::vector<bool>>
ReplayModbusTransport::read_coils(std::uint8_t, std::uint16_t, std::uint16_t quantity) {
    auto pdu = next_pdu();
    if (!pdu) {
        return std::unexpected(pdu.error());
    }
    return decode_bits(*pdu, quantity);
}

domain::Result<std::vector<bool>>
ReplayModbusTransport::read_discrete_inputs(std::uint8_t unit,
                                            std::uint16_t address,
                                            std::uint16_t quantity) {
    return read_coils(unit, address, quantity);
}

domain::Result<void>
ReplayModbusTransport::write_single_register(std::uint8_t, std::uint16_t, std::uint16_t) {
    auto pdu = next_pdu();
    if (!pdu) {
        return std::unexpected(pdu.error());
    }
    return {};
}

domain::Result<void>
ReplayModbusTransport::write_multiple_registers(std::uint8_t,
                                                std::uint16_t,
                                                std::span<const std::uint16_t>) {
    auto pdu = next_pdu();
    if (!pdu) {
        return std::unexpected(pdu.error());
    }
    return {};
}

domain::Result<void>
ReplayModbusTransport::write_single_coil(std::uint8_t, std::uint16_t, bool) {
    auto pdu = next_pdu();
    if (!pdu) {
        return std::unexpected(pdu.error());
    }
    return {};
}

}  // namespace opc::adapters
