#include "adapters/modbus_udp_transport.hpp"
#include "adapters/modbus_protocol.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace opc::adapters {
namespace {

domain::Error udp_err(domain::ErrorCode code, std::string message, bool retryable = true) {
    return domain::Error{code, std::move(message), "adapters.modbus_udp", retryable};
}

domain::TimestampMs wall_now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

}  // namespace

ModbusUdpTransport::ModbusUdpTransport(int response_timeout_ms)
    : response_timeout_ms_(response_timeout_ms) {}

ModbusUdpTransport::ModbusUdpTransport(ModbusUdpTransportOptions options)
    : response_timeout_ms_(options.response_timeout_ms),
      frame_log_(options.frame_log),
      endpoint_id_(std::move(options.endpoint_id)) {}

ModbusUdpTransport::~ModbusUdpTransport() {
    close();
}

void ModbusUdpTransport::emit_frame(ports::FrameRecord frame) {
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

domain::Result<void> ModbusUdpTransport::connect(const ports::EndpointAddress& endpoint) {
    std::lock_guard lock(mutex_);
    close();

    fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_ < 0) {
        return std::unexpected(udp_err(domain::ErrorCode::Connection, "socket() failed"));
    }

    timeval tv{};
    tv.tv_sec = response_timeout_ms_ / 1000;
    tv.tv_usec = (response_timeout_ms_ % 1000) * 1000;
    setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    std::memset(&peer_, 0, sizeof(peer_));
    peer_.sin_family = AF_INET;
    peer_.sin_port = htons(endpoint.port);
    if (::inet_pton(AF_INET, endpoint.host.c_str(), &peer_.sin_addr) != 1) {
        close();
        return std::unexpected(udp_err(domain::ErrorCode::InvalidArgument,
                                       "invalid host IPv4: " + endpoint.host, false));
    }
    if (::connect(fd_, reinterpret_cast<sockaddr*>(&peer_), sizeof(peer_)) < 0) {
        const int e = errno;
        close();
        return std::unexpected(
            udp_err(domain::ErrorCode::Connection, std::string("udp connect failed: ") + std::strerror(e)));
    }
    return {};
}

void ModbusUdpTransport::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

bool ModbusUdpTransport::is_connected() const {
    std::lock_guard lock(mutex_);
    return fd_ >= 0;
}

domain::Result<std::vector<std::uint8_t>>
ModbusUdpTransport::transact(std::uint8_t unit, std::span<const std::uint8_t> pdu) {
    std::unique_lock lock(mutex_);
    if (fd_ < 0) {
        return std::unexpected(udp_err(domain::ErrorCode::Connection, "not connected"));
    }

    const auto tid = transaction_id_++;
    const auto req = pack_mbap(tid, unit, pdu);

    ports::FrameRecord frame;
    frame.ts_ms = wall_now_ms();
    frame.endpoint_id = endpoint_id_;
    frame.tx = req;
    const auto t0 = std::chrono::steady_clock::now();

    auto finish = [&](domain::Result<std::vector<std::uint8_t>> result) {
        const auto t1 = std::chrono::steady_clock::now();
        frame.rtt_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        if (!result) {
            frame.error = result.error().message;
            if (result.error().protocol_status) {
                frame.exception_code = *result.error().protocol_status;
            }
        }
        lock.unlock();
        emit_frame(std::move(frame));
        return result;
    };

    if (::send(fd_, req.data(), req.size(), 0) < 0) {
        return finish(std::unexpected(udp_err(domain::ErrorCode::Connection, "send failed")));
    }

    std::uint8_t buf[260];
    const auto n = ::recv(fd_, buf, sizeof(buf), 0);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return finish(std::unexpected(udp_err(domain::ErrorCode::Timeout, "recv timeout")));
        }
        return finish(std::unexpected(udp_err(domain::ErrorCode::Connection, "recv failed")));
    }
    frame.rx.assign(buf, buf + n);
    auto unpacked = unpack_adu({buf, static_cast<std::size_t>(n)});
    if (!unpacked) {
        return finish(std::unexpected(unpacked.error()));
    }
    if (unpacked->transaction_id != tid) {
        return finish(std::unexpected(udp_err(domain::ErrorCode::Decoding, "transaction id mismatch", false)));
    }
    return finish(std::move(unpacked->pdu));
}

domain::Result<std::vector<std::uint16_t>>
ModbusUdpTransport::read_holding_registers(std::uint8_t unit, std::uint16_t address, std::uint16_t quantity) {
    return modbus_read_registers([this](auto u, auto p) { return transact(u, p); }, 0x03, unit, address,
                                 quantity);
}

domain::Result<std::vector<std::uint16_t>>
ModbusUdpTransport::read_input_registers(std::uint8_t unit, std::uint16_t address, std::uint16_t quantity) {
    return modbus_read_registers([this](auto u, auto p) { return transact(u, p); }, 0x04, unit, address,
                                 quantity);
}

domain::Result<std::vector<bool>>
ModbusUdpTransport::read_coils(std::uint8_t unit, std::uint16_t address, std::uint16_t quantity) {
    return modbus_read_bits([this](auto u, auto p) { return transact(u, p); }, 0x01, unit, address, quantity);
}

domain::Result<std::vector<bool>>
ModbusUdpTransport::read_discrete_inputs(std::uint8_t unit, std::uint16_t address, std::uint16_t quantity) {
    return modbus_read_bits([this](auto u, auto p) { return transact(u, p); }, 0x02, unit, address, quantity);
}

domain::Result<void>
ModbusUdpTransport::write_single_register(std::uint8_t unit, std::uint16_t address, std::uint16_t value) {
    return modbus_write_single_register([this](auto u, auto p) { return transact(u, p); }, unit, address,
                                        value);
}

domain::Result<void>
ModbusUdpTransport::write_multiple_registers(std::uint8_t unit,
                                             std::uint16_t address,
                                             std::span<const std::uint16_t> values) {
    return modbus_write_multiple_registers([this](auto u, auto p) { return transact(u, p); }, unit, address,
                                           values);
}

domain::Result<void>
ModbusUdpTransport::write_single_coil(std::uint8_t unit, std::uint16_t address, bool value) {
    return modbus_write_single_coil([this](auto u, auto p) { return transact(u, p); }, unit, address, value);
}

domain::Result<void>
ModbusUdpTransport::write_multiple_coils(std::uint8_t unit,
                                         std::uint16_t address,
                                         std::span<const std::uint8_t> values) {
    return modbus_write_multiple_coils([this](auto u, auto p) { return transact(u, p); }, unit, address,
                                       values);
}

}  // namespace opc::adapters
