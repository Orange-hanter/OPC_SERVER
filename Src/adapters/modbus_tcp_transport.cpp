#include "adapters/modbus_tcp_transport.hpp"
#include "adapters/modbus_protocol.hpp"

#include <asio.hpp>

#include <array>
#include <chrono>
#include <cstring>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

namespace opc::adapters {
namespace {

using tcp = asio::ip::tcp;

domain::Error make_err(domain::ErrorCode code, std::string message, bool retryable = true) {
    return domain::Error{code, std::move(message), "adapters.modbus_tcp", retryable};
}

domain::TimestampMs wall_now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

}  // namespace

struct ModbusTcpTransport::Impl {
    explicit Impl(ModbusTcpTransportOptions options)
        : response_timeout_ms(options.response_timeout_ms < 1 ? 1 : options.response_timeout_ms),
          frame_log(options.frame_log),
          endpoint_id(std::move(options.endpoint_id)),
          socket(ctx) {}

    void emit_frame(ports::FrameRecord frame) {
        if (frame_log == nullptr) {
            return;
        }
        if (frame.endpoint_id.empty()) {
            frame.endpoint_id = endpoint_id;
        }
        if (frame.ts_ms == 0) {
            frame.ts_ms = wall_now_ms();
        }
        frame_log->log_frame(frame);
    }

    void reset_socket() {
        asio::error_code ec;
        socket.cancel(ec);
        socket.close(ec);
        socket = tcp::socket{ctx};
    }

    /// Run queued async work until completion or overall timeout.
    template <typename Fn>
    void run_until(Fn&& arm) {
        ctx.restart();
        asio::steady_timer deadline{ctx};
        active_deadline = &deadline;
        timed_out = false;
        deadline.expires_after(std::chrono::milliseconds{response_timeout_ms});
        deadline.async_wait([this](const asio::error_code& ec) {
            if (ec) {
                return;
            }
            timed_out = true;
            asio::error_code ignore;
            socket.cancel(ignore);
        });
        arm();
        ctx.run();
        active_deadline = nullptr;
        deadline.cancel();
        if (timed_out && last_error.empty()) {
            last_error = "operation timeout";
            last_code = domain::ErrorCode::Timeout;
        }
    }

    void complete_op() {
        if (active_deadline != nullptr) {
            active_deadline->cancel();
        }
    }

    int response_timeout_ms{1000};
    ports::IFrameLog* frame_log{nullptr};
    std::string endpoint_id;
    mutable std::mutex mutex;
    asio::io_context ctx;
    tcp::socket socket;
    asio::steady_timer* active_deadline{nullptr};
    bool timed_out{false};
    std::uint16_t transaction_id{1};
    bool connected{false};
    domain::ErrorCode last_code{domain::ErrorCode::Internal};
    std::string last_error;
};

ModbusTcpTransport::ModbusTcpTransport(int response_timeout_ms)
    : ModbusTcpTransport(ModbusTcpTransportOptions{.response_timeout_ms = response_timeout_ms}) {}

ModbusTcpTransport::ModbusTcpTransport(ModbusTcpTransportOptions options)
    : impl_(std::make_unique<Impl>(std::move(options))) {}

ModbusTcpTransport::~ModbusTcpTransport() {
    close();
}

void ModbusTcpTransport::set_frame_log(ports::IFrameLog* log) {
    std::lock_guard lock(impl_->mutex);
    impl_->frame_log = log;
}

void ModbusTcpTransport::set_endpoint_id(std::string id) {
    std::lock_guard lock(impl_->mutex);
    impl_->endpoint_id = std::move(id);
}

domain::Result<void> ModbusTcpTransport::connect(const ports::EndpointAddress& endpoint) {
    std::lock_guard lock(impl_->mutex);
    impl_->reset_socket();
    impl_->connected = false;
    impl_->last_error.clear();

    asio::error_code resolve_ec;
    tcp::resolver resolver{impl_->ctx};
    const auto results =
        resolver.resolve(endpoint.host, std::to_string(endpoint.port), resolve_ec);
    if (resolve_ec || results.empty()) {
        return std::unexpected(make_err(
            domain::ErrorCode::InvalidArgument,
            "resolve failed: " + (resolve_ec ? resolve_ec.message() : endpoint.host),
            false));
    }

    bool ok = false;
    impl_->run_until([&] {
        asio::async_connect(
            impl_->socket, results,
            [&](const asio::error_code& ec, const tcp::endpoint&) {
                if (ec) {
                    if (ec != asio::error::operation_aborted) {
                        impl_->last_code = domain::ErrorCode::Connection;
                        impl_->last_error = "connect failed: " + ec.message();
                    }
                    impl_->complete_op();
                    return;
                }
                ok = true;
                impl_->connected = true;
                impl_->complete_op();
            });
    });

    if (!ok) {
        impl_->reset_socket();
        impl_->connected = false;
        if (impl_->last_error.empty()) {
            return std::unexpected(make_err(domain::ErrorCode::Connection, "connect failed"));
        }
        return std::unexpected(make_err(impl_->last_code, impl_->last_error));
    }
    return {};
}

void ModbusTcpTransport::close() {
    if (!impl_) {
        return;
    }
    asio::error_code ec;
    impl_->socket.cancel(ec);
    impl_->socket.shutdown(tcp::socket::shutdown_both, ec);
    impl_->socket.close(ec);
    impl_->connected = false;
}

bool ModbusTcpTransport::is_connected() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->connected && impl_->socket.is_open();
}

domain::Result<std::vector<std::uint8_t>>
ModbusTcpTransport::transact(std::uint8_t unit, std::span<const std::uint8_t> pdu) {
    std::unique_lock lock(impl_->mutex);
    if (!impl_->connected || !impl_->socket.is_open()) {
        return std::unexpected(make_err(domain::ErrorCode::Connection, "not connected"));
    }

    const auto tid = impl_->transaction_id++;
    auto req = pack_mbap(tid, unit, pdu);

    ports::FrameRecord frame;
    frame.ts_ms = wall_now_ms();
    frame.endpoint_id = impl_->endpoint_id;
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
            if (result.error().code == domain::ErrorCode::Connection ||
                result.error().code == domain::ErrorCode::Timeout) {
                impl_->connected = false;
                asio::error_code ec;
                impl_->socket.close(ec);
            }
        }
        lock.unlock();
        impl_->emit_frame(std::move(frame));
        return result;
    };

    impl_->last_error.clear();
    std::optional<domain::Result<std::vector<std::uint8_t>>> out;

    impl_->run_until([&] {
        asio::async_write(
            impl_->socket, asio::buffer(req),
            [&](const asio::error_code& write_ec, std::size_t) {
                if (write_ec) {
                    if (write_ec != asio::error::operation_aborted) {
                        out = std::unexpected(
                            make_err(domain::ErrorCode::Connection,
                                     "send failed: " + write_ec.message()));
                    }
                    impl_->complete_op();
                    return;
                }

                // MBAP without UnitId (6 bytes), then `length` bytes = UnitId + PDU.
                auto mbap = std::make_shared<std::array<std::uint8_t, 6>>();
                asio::async_read(
                    impl_->socket, asio::buffer(*mbap),
                    [&, mbap](const asio::error_code& hdr_ec, std::size_t) {
                        if (hdr_ec) {
                            if (hdr_ec != asio::error::operation_aborted) {
                                if (hdr_ec == asio::error::eof) {
                                    out = std::unexpected(
                                        make_err(domain::ErrorCode::Connection, "peer closed"));
                                } else {
                                    out = std::unexpected(
                                        make_err(domain::ErrorCode::Connection,
                                                 "recv failed: " + hdr_ec.message()));
                                }
                            }
                            impl_->complete_op();
                            return;
                        }

                        const std::uint16_t length =
                            static_cast<std::uint16_t>(((*mbap)[4] << 8) | (*mbap)[5]);
                        // Modbus TCP: length = unit id + PDU; ADU max 260 → length ≤ 254.
                        if (length < 2 || length > 254) {
                            out = std::unexpected(
                                make_err(domain::ErrorCode::Decoding, "bad MBAP length", false));
                            impl_->complete_op();
                            return;
                        }

                        auto body = std::make_shared<std::vector<std::uint8_t>>(length);
                        asio::async_read(
                            impl_->socket, asio::buffer(*body),
                            [&, mbap, body](const asio::error_code& body_ec, std::size_t) {
                                if (body_ec) {
                                    if (body_ec != asio::error::operation_aborted) {
                                        if (body_ec == asio::error::eof) {
                                            out = std::unexpected(
                                                make_err(domain::ErrorCode::Connection,
                                                         "peer closed"));
                                        } else {
                                            out = std::unexpected(
                                                make_err(domain::ErrorCode::Connection,
                                                         "recv failed: " + body_ec.message()));
                                        }
                                    }
                                    impl_->complete_op();
                                    return;
                                }

                                frame.rx.assign(mbap->begin(), mbap->end());
                                frame.rx.insert(frame.rx.end(), body->begin(), body->end());

                                // body[0] = unit, body[1+] = function + data
                                if (body->size() < 2) {
                                    out = std::unexpected(
                                        make_err(domain::ErrorCode::Decoding, "short PDU", false));
                                    impl_->complete_op();
                                    return;
                                }
                                if ((*body)[1] & 0x80u) {
                                    const int ex = body->size() > 2 ? (*body)[2] : -1;
                                    domain::Error err = make_err(
                                        domain::ErrorCode::ModbusException, "modbus exception", true);
                                    err.protocol_status = ex;
                                    out = std::unexpected(err);
                                    impl_->complete_op();
                                    return;
                                }
                                out = std::vector<std::uint8_t>(body->begin() + 1, body->end());
                                impl_->complete_op();
                            });
                    });
            });
    });

    if (!out) {
        if (!impl_->last_error.empty()) {
            return finish(std::unexpected(make_err(impl_->last_code, impl_->last_error)));
        }
        return finish(std::unexpected(make_err(domain::ErrorCode::Timeout, "recv timeout")));
    }
    return finish(std::move(*out));
}

domain::Result<std::vector<std::uint16_t>>
ModbusTcpTransport::read_holding_registers(std::uint8_t unit,
                                           std::uint16_t address,
                                           std::uint16_t quantity) {
    return modbus_read_registers([this](auto u, auto p) { return transact(u, p); }, 0x03, unit, address,
                                 quantity);
}

domain::Result<std::vector<std::uint16_t>>
ModbusTcpTransport::read_input_registers(std::uint8_t unit,
                                         std::uint16_t address,
                                         std::uint16_t quantity) {
    return modbus_read_registers([this](auto u, auto p) { return transact(u, p); }, 0x04, unit, address,
                                 quantity);
}

domain::Result<std::vector<bool>>
ModbusTcpTransport::read_coils(std::uint8_t unit, std::uint16_t address, std::uint16_t quantity) {
    return modbus_read_bits([this](auto u, auto p) { return transact(u, p); }, 0x01, unit, address, quantity);
}

domain::Result<std::vector<bool>>
ModbusTcpTransport::read_discrete_inputs(std::uint8_t unit,
                                         std::uint16_t address,
                                         std::uint16_t quantity) {
    return modbus_read_bits([this](auto u, auto p) { return transact(u, p); }, 0x02, unit, address, quantity);
}

domain::Result<void>
ModbusTcpTransport::write_single_register(std::uint8_t unit,
                                          std::uint16_t address,
                                          std::uint16_t value) {
    return modbus_write_single_register([this](auto u, auto p) { return transact(u, p); }, unit, address,
                                        value);
}

domain::Result<void>
ModbusTcpTransport::write_multiple_registers(std::uint8_t unit,
                                             std::uint16_t address,
                                             std::span<const std::uint16_t> values) {
    return modbus_write_multiple_registers([this](auto u, auto p) { return transact(u, p); }, unit, address,
                                           values);
}

domain::Result<void>
ModbusTcpTransport::write_single_coil(std::uint8_t unit, std::uint16_t address, bool value) {
    return modbus_write_single_coil([this](auto u, auto p) { return transact(u, p); }, unit, address, value);
}

domain::Result<void>
ModbusTcpTransport::write_multiple_coils(std::uint8_t unit,
                                         std::uint16_t address,
                                         std::span<const std::uint8_t> values) {
    return modbus_write_multiple_coils([this](auto u, auto p) { return transact(u, p); }, unit, address,
                                       values);
}

}  // namespace opc::adapters
