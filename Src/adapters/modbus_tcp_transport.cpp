#include "adapters/modbus_tcp_transport.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <mutex>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace opc::adapters {
namespace {

domain::Error make_err(domain::ErrorCode code, std::string message, bool retryable = true) {
    return domain::Error{code, std::move(message), "adapters.modbus_tcp", retryable};
}

void append_u16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
}

domain::TimestampMs wall_now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

}  // namespace

ModbusTcpTransport::ModbusTcpTransport(int response_timeout_ms)
    : response_timeout_ms_(response_timeout_ms) {}

ModbusTcpTransport::ModbusTcpTransport(ModbusTcpTransportOptions options)
    : response_timeout_ms_(options.response_timeout_ms),
      frame_log_(options.frame_log),
      endpoint_id_(std::move(options.endpoint_id)) {}

void ModbusTcpTransport::emit_frame(ports::FrameRecord frame) {
    if (frame_log_ == nullptr) {
        return;
    }
    if (frame.endpoint_id.empty()) {
        frame.endpoint_id = endpoint_id_;
    }
    if (frame.ts_ms == 0) {
        frame.ts_ms = wall_now_ms();
    }
    frame_log_->log_frame(frame);
}
domain::Result<void> ModbusTcpTransport::connect(const ports::EndpointAddress& endpoint) {
    std::lock_guard lock(mutex_);
    close();

    fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ < 0) {
        return std::unexpected(make_err(domain::ErrorCode::Connection, "socket() failed"));
    }

    timeval tv{};
    tv.tv_sec = response_timeout_ms_ / 1000;
    tv.tv_usec = static_cast<decltype(tv.tv_usec)>(response_timeout_ms_ % 1000) * 1000;
    setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(endpoint.port);
    if (::inet_pton(AF_INET, endpoint.host.c_str(), &addr.sin_addr) != 1) {
        close();
        return std::unexpected(make_err(domain::ErrorCode::InvalidArgument,
                                        "invalid host IPv4: " + endpoint.host,
                                        false));
    }

    if (::connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        const int e = errno;
        close();
        return std::unexpected(make_err(domain::ErrorCode::Connection,
                                        std::string("connect failed: ") + std::strerror(e)));
    }
    return {};
}

void ModbusTcpTransport::close() {
    if (fd_ >= 0) {
        ::shutdown(fd_, SHUT_RDWR);
        ::close(fd_);
        fd_ = -1;
    }
}

bool ModbusTcpTransport::is_connected() const {
    std::lock_guard lock(mutex_);
    return fd_ >= 0;
}

domain::Result<std::vector<std::uint8_t>>
ModbusTcpTransport::transact(std::uint8_t unit, std::span<const std::uint8_t> pdu) {
    std::unique_lock lock(mutex_);
    if (fd_ < 0) {
        return std::unexpected(make_err(domain::ErrorCode::Connection, "not connected"));
    }

    const auto tid = transaction_id_++;
    std::vector<std::uint8_t> req;
    req.reserve(7 + pdu.size());
    append_u16(req, tid);
    append_u16(req, 0);  // protocol
    append_u16(req, static_cast<std::uint16_t>(1 + pdu.size()));
    req.push_back(unit);
    req.insert(req.end(), pdu.begin(), pdu.end());

    ports::FrameRecord frame;
    frame.ts_ms = wall_now_ms();
    frame.endpoint_id = endpoint_id_;
    frame.tx = req;
    const auto t0 = std::chrono::steady_clock::now();

    auto finish = [&](domain::Result<std::vector<std::uint8_t>> result) {
        const auto t1 = std::chrono::steady_clock::now();
        frame.rtt_ms =
            std::chrono::duration<double, std::milli>(t1 - t0).count();
        if (!result) {
            frame.error = result.error().message;
            if (result.error().protocol_status) {
                frame.exception_code = result.error().protocol_status;
            }
        }
        lock.unlock();
        emit_frame(std::move(frame));
        return result;
    };
    const auto protocol_failure = [&](std::string message) {
        close();
        return finish(std::unexpected(
            make_err(domain::ErrorCode::Decoding, std::move(message), false)));
    };

    std::size_t sent = 0;
    while (sent < req.size()) {
        const auto n = ::send(fd_, req.data() + sent, req.size() - sent, MSG_NOSIGNAL);
        if (n <= 0) {
            return finish(std::unexpected(make_err(domain::ErrorCode::Connection, "send failed")));
        }
        sent += static_cast<std::size_t>(n);
    }

    std::uint8_t header[6];
    std::size_t got = 0;
    while (got < sizeof(header)) {
        // Synchronous transport calls are serialized by mutex_ (ADR-0002).
        // NOLINTNEXTLINE(clang-analyzer-unix.BlockInCriticalSection)
        const auto n = ::recv(fd_, header + got, sizeof(header) - got, 0);
        if (n == 0) {
            return finish(std::unexpected(make_err(domain::ErrorCode::Connection, "peer closed")));
        }
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return finish(std::unexpected(make_err(domain::ErrorCode::Timeout, "recv timeout")));
            }
            return finish(std::unexpected(make_err(domain::ErrorCode::Connection, "recv failed")));
        }
        got += static_cast<std::size_t>(n);
    }

    const std::uint16_t length = static_cast<std::uint16_t>((header[4] << 8) | header[5]);
    const std::uint16_t response_tid =
        static_cast<std::uint16_t>((header[0] << 8) | header[1]);
    const std::uint16_t protocol_id =
        static_cast<std::uint16_t>((header[2] << 8) | header[3]);
    if (response_tid != tid) {
        return protocol_failure("MBAP transaction id mismatch");
    }
    if (protocol_id != 0) {
        return protocol_failure("invalid MBAP protocol id");
    }
    if (length < 2 || length > 254) {
        return protocol_failure("bad MBAP length");
    }
    std::vector<std::uint8_t> body(length);
    got = 0;
    while (got < body.size()) {
        // See the synchronous strand contract above.
        // NOLINTNEXTLINE(clang-analyzer-unix.BlockInCriticalSection)
        const auto n = ::recv(fd_, body.data() + got, body.size() - got, 0);
        if (n == 0) {
            return finish(std::unexpected(make_err(domain::ErrorCode::Connection, "peer closed")));
        }
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return finish(std::unexpected(make_err(domain::ErrorCode::Timeout, "recv timeout")));
            }
            return finish(std::unexpected(make_err(domain::ErrorCode::Connection, "recv failed")));
        }
        got += static_cast<std::size_t>(n);
    }

    frame.rx.assign(header, header + sizeof(header));
    frame.rx.insert(frame.rx.end(), body.begin(), body.end());

    // body[0] = unit, body[1] = function or exception
    if (body.size() < 2) {
        return protocol_failure("short PDU");
    }
    if (body[0] != unit) {
        return protocol_failure("MBAP unit id mismatch");
    }
    if (body[1] & 0x80u) {
        if (body[1] != static_cast<std::uint8_t>(pdu[0] | 0x80u)) {
            return protocol_failure("exception function mismatch");
        }
        const int ex = body.size() > 2 ? body[2] : -1;
        domain::Error err =
            make_err(domain::ErrorCode::ModbusException, "modbus exception", true);
        err.protocol_status = ex;
        return finish(std::unexpected(err));
    }
    if (body[1] != pdu[0]) {
        return protocol_failure("response function mismatch");
    }
    // Return PDU without unit id (function + data)
    return finish(std::vector<std::uint8_t>(body.begin() + 1, body.end()));
}
domain::Result<std::vector<std::uint16_t>>
ModbusTcpTransport::read_registers(std::uint8_t function,
                                   std::uint8_t unit,
                                   std::uint16_t address,
                                   std::uint16_t quantity) {
    if (quantity == 0 || quantity > 125) {
        return std::unexpected(make_err(
            domain::ErrorCode::InvalidArgument, "register quantity must be in [1, 125]", false));
    }
    std::vector<std::uint8_t> pdu;
    pdu.push_back(function);
    append_u16(pdu, address);
    append_u16(pdu, quantity);
    auto resp = transact(unit, pdu);
    if (!resp) {
        return std::unexpected(resp.error());
    }
    if (resp->size() < 2) {
        close();
        return std::unexpected(make_err(domain::ErrorCode::Decoding, "short register response", false));
    }
    const auto byte_count = (*resp)[1];
    if (resp->size() < 2u + byte_count || byte_count != quantity * 2) {
        close();
        return std::unexpected(make_err(domain::ErrorCode::Decoding, "register byte count mismatch", false));
    }
    std::vector<std::uint16_t> out;
    out.reserve(quantity);
    for (std::uint16_t i = 0; i < quantity; ++i) {
        const auto hi = (*resp)[2 + i * 2];
        const auto lo = (*resp)[3 + i * 2];
        out.push_back(static_cast<std::uint16_t>((hi << 8) | lo));
    }
    return out;
}

domain::Result<std::vector<bool>>
ModbusTcpTransport::read_bits(std::uint8_t function,
                              std::uint8_t unit,
                              std::uint16_t address,
                              std::uint16_t quantity) {
    if (quantity == 0 || quantity > 2000) {
        return std::unexpected(make_err(
            domain::ErrorCode::InvalidArgument, "bit quantity must be in [1, 2000]", false));
    }
    std::vector<std::uint8_t> pdu;
    pdu.push_back(function);
    append_u16(pdu, address);
    append_u16(pdu, quantity);
    auto resp = transact(unit, pdu);
    if (!resp) {
        return std::unexpected(resp.error());
    }
    if (resp->size() < 2) {
        close();
        return std::unexpected(make_err(domain::ErrorCode::Decoding, "short bit response", false));
    }
    const auto byte_count = (*resp)[1];
    const auto expected_byte_count = static_cast<std::uint8_t>((quantity + 7u) / 8u);
    if (byte_count != expected_byte_count || resp->size() != 2u + byte_count) {
        close();
        return std::unexpected(make_err(domain::ErrorCode::Decoding, "bit byte count mismatch", false));
    }
    std::vector<bool> out;
    out.reserve(quantity);
    for (std::uint16_t i = 0; i < quantity; ++i) {
        const auto byte = (*resp)[2 + i / 8];
        out.push_back(((byte >> (i % 8)) & 0x1u) != 0);
    }
    return out;
}

domain::Result<std::vector<std::uint16_t>>
ModbusTcpTransport::read_holding_registers(std::uint8_t unit,
                                           std::uint16_t address,
                                           std::uint16_t quantity) {
    return read_registers(0x03, unit, address, quantity);
}

domain::Result<std::vector<std::uint16_t>>
ModbusTcpTransport::read_input_registers(std::uint8_t unit,
                                         std::uint16_t address,
                                         std::uint16_t quantity) {
    return read_registers(0x04, unit, address, quantity);
}

domain::Result<std::vector<bool>>
ModbusTcpTransport::read_coils(std::uint8_t unit, std::uint16_t address, std::uint16_t quantity) {
    return read_bits(0x01, unit, address, quantity);
}

domain::Result<std::vector<bool>>
ModbusTcpTransport::read_discrete_inputs(std::uint8_t unit,
                                         std::uint16_t address,
                                         std::uint16_t quantity) {
    return read_bits(0x02, unit, address, quantity);
}

domain::Result<void>
ModbusTcpTransport::write_single_register(std::uint8_t unit,
                                          std::uint16_t address,
                                          std::uint16_t value) {
    std::vector<std::uint8_t> pdu;
    pdu.push_back(0x06);
    append_u16(pdu, address);
    append_u16(pdu, value);
    auto resp = transact(unit, pdu);
    if (!resp) {
        return std::unexpected(resp.error());
    }
    if (*resp != pdu) {
        close();
        return std::unexpected(
            make_err(domain::ErrorCode::Decoding, "single-register write echo mismatch", false));
    }
    return {};
}

domain::Result<void>
ModbusTcpTransport::write_multiple_registers(std::uint8_t unit,
                                             std::uint16_t address,
                                             std::span<const std::uint16_t> values) {
    if (values.empty() || values.size() > 123) {
        return std::unexpected(make_err(
            domain::ErrorCode::InvalidArgument, "write quantity must be in [1, 123]", false));
    }
    std::vector<std::uint8_t> pdu;
    pdu.push_back(0x10);
    append_u16(pdu, address);
    append_u16(pdu, static_cast<std::uint16_t>(values.size()));
    pdu.push_back(static_cast<std::uint8_t>(values.size() * 2));
    for (auto v : values) {
        append_u16(pdu, v);
    }
    auto resp = transact(unit, pdu);
    if (!resp) {
        return std::unexpected(resp.error());
    }
    std::vector<std::uint8_t> expected{0x10};
    append_u16(expected, address);
    append_u16(expected, static_cast<std::uint16_t>(values.size()));
    if (*resp != expected) {
        close();
        return std::unexpected(
            make_err(domain::ErrorCode::Decoding, "multiple-register write echo mismatch", false));
    }
    return {};
}

domain::Result<void>
ModbusTcpTransport::write_single_coil(std::uint8_t unit, std::uint16_t address, bool value) {
    std::vector<std::uint8_t> pdu;
    pdu.push_back(0x05);
    append_u16(pdu, address);
    append_u16(pdu, value ? 0xFF00 : 0x0000);
    auto resp = transact(unit, pdu);
    if (!resp) {
        return std::unexpected(resp.error());
    }
    if (*resp != pdu) {
        close();
        return std::unexpected(
            make_err(domain::ErrorCode::Decoding, "single-coil write echo mismatch", false));
    }
    return {};
}

}  // namespace opc::adapters
