#include "adapters/modbus_tcp_transport.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
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

}  // namespace

ModbusTcpTransport::ModbusTcpTransport(int response_timeout_ms)
    : response_timeout_ms_(response_timeout_ms) {}

domain::Result<void> ModbusTcpTransport::connect(const ports::EndpointAddress& endpoint) {
    std::lock_guard lock(mutex_);
    close();

    fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ < 0) {
        return std::unexpected(make_err(domain::ErrorCode::Connection, "socket() failed"));
    }

    timeval tv{};
    tv.tv_sec = response_timeout_ms_ / 1000;
    tv.tv_usec = (response_timeout_ms_ % 1000) * 1000;
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
    std::lock_guard lock(mutex_);
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

    std::size_t sent = 0;
    while (sent < req.size()) {
        const auto n = ::send(fd_, req.data() + sent, req.size() - sent, 0);
        if (n <= 0) {
            return std::unexpected(make_err(domain::ErrorCode::Connection, "send failed"));
        }
        sent += static_cast<std::size_t>(n);
    }

    std::uint8_t header[7];
    std::size_t got = 0;
    while (got < sizeof(header)) {
        const auto n = ::recv(fd_, header + got, sizeof(header) - got, 0);
        if (n == 0) {
            return std::unexpected(make_err(domain::ErrorCode::Connection, "peer closed"));
        }
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return std::unexpected(make_err(domain::ErrorCode::Timeout, "recv timeout"));
            }
            return std::unexpected(make_err(domain::ErrorCode::Connection, "recv failed"));
        }
        got += static_cast<std::size_t>(n);
    }

    const std::uint16_t length = static_cast<std::uint16_t>((header[4] << 8) | header[5]);
    if (length < 2) {
        return std::unexpected(make_err(domain::ErrorCode::Decoding, "bad MBAP length", false));
    }
    std::vector<std::uint8_t> body(length);
    got = 0;
    while (got < body.size()) {
        const auto n = ::recv(fd_, body.data() + got, body.size() - got, 0);
        if (n == 0) {
            return std::unexpected(make_err(domain::ErrorCode::Connection, "peer closed"));
        }
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return std::unexpected(make_err(domain::ErrorCode::Timeout, "recv timeout"));
            }
            return std::unexpected(make_err(domain::ErrorCode::Connection, "recv failed"));
        }
        got += static_cast<std::size_t>(n);
    }

    // body[0] = unit, body[1] = function or exception
    if (body.size() < 2) {
        return std::unexpected(make_err(domain::ErrorCode::Decoding, "short PDU", false));
    }
    if (body[1] & 0x80u) {
        const int ex = body.size() > 2 ? body[2] : -1;
        domain::Error err =
            make_err(domain::ErrorCode::ModbusException, "modbus exception", true);
        err.protocol_status = ex;
        return std::unexpected(err);
    }
    // Return PDU without unit id (function + data)
    return std::vector<std::uint8_t>(body.begin() + 1, body.end());
}

domain::Result<std::vector<std::uint16_t>>
ModbusTcpTransport::read_registers(std::uint8_t function,
                                   std::uint8_t unit,
                                   std::uint16_t address,
                                   std::uint16_t quantity) {
    std::vector<std::uint8_t> pdu;
    pdu.push_back(function);
    append_u16(pdu, address);
    append_u16(pdu, quantity);
    auto resp = transact(unit, pdu);
    if (!resp) {
        return std::unexpected(resp.error());
    }
    if (resp->size() < 2) {
        return std::unexpected(make_err(domain::ErrorCode::Decoding, "short register response", false));
    }
    const auto byte_count = (*resp)[1];
    if (resp->size() < 2u + byte_count || byte_count != quantity * 2) {
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
    std::vector<std::uint8_t> pdu;
    pdu.push_back(function);
    append_u16(pdu, address);
    append_u16(pdu, quantity);
    auto resp = transact(unit, pdu);
    if (!resp) {
        return std::unexpected(resp.error());
    }
    if (resp->size() < 2) {
        return std::unexpected(make_err(domain::ErrorCode::Decoding, "short bit response", false));
    }
    const auto byte_count = (*resp)[1];
    if (resp->size() < 2u + byte_count) {
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
    return {};
}

domain::Result<void>
ModbusTcpTransport::write_multiple_registers(std::uint8_t unit,
                                             std::uint16_t address,
                                             std::span<const std::uint16_t> values) {
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
    return {};
}

}  // namespace opc::adapters
