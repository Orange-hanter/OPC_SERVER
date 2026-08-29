#include "adapters/modbus_tcp_transport.hpp"
#include "adapters/modbus_protocol.hpp"

#include <asio.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

namespace opc::adapters {
namespace {

using tcp = asio::ip::tcp;
using WorkGuard = asio::executor_work_guard<asio::io_context::executor_type>;
using Strand = asio::strand<asio::io_context::executor_type>;

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
          strand(asio::make_strand(ctx.get_executor())),
          socket(ctx) {}

    ~Impl() { stop_worker(); }

    void ensure_worker() {
        std::lock_guard lock(mutex);
        if (worker.joinable()) {
            return;
        }
        ctx.restart();
        guard.emplace(asio::make_work_guard(ctx));
        worker = std::thread([this] { ctx.run(); });
    }

    void stop_worker() {
        std::thread local;
        {
            std::lock_guard lock(mutex);
            if (guard) {
                guard->reset();
                guard.reset();
            }
            ctx.stop();
            if (worker.joinable() && worker.get_id() != std::this_thread::get_id()) {
                local = std::move(worker);
            } else if (worker.joinable()) {
                // Closing from the worker thread: detach so we do not self-join.
                worker.detach();
            }
        }
        if (local.joinable()) {
            local.join();
        }
        std::lock_guard lock(mutex);
        ctx.restart();
    }

    void emit_frame(ports::FrameRecord frame) {
        ports::IFrameLog* log = nullptr;
        std::string ep;
        {
            std::lock_guard lock(mutex);
            log = frame_log;
            ep = endpoint_id;
        }
        if (log == nullptr) {
            return;
        }
        if (frame.endpoint_id.empty()) {
            frame.endpoint_id = std::move(ep);
        }
        if (frame.ts_ms == 0) {
            frame.ts_ms = wall_now_ms();
        }
        log->log_frame(frame);
    }

    void reset_socket() {
        asio::error_code ec;
        socket.cancel(ec);
        socket.close(ec);
        socket = tcp::socket{ctx};
    }

    void mark_disconnected() {
        connected = false;
        asio::error_code ec;
        socket.close(ec);
    }

    template <typename T>
    void deliver(ports::ModbusCompletion<T> handler,
                 domain::Result<T> result,
                 bool inline_completion) {
        if (!handler) {
            return;
        }
        ports::IExecutor* executor = nullptr;
        if (!inline_completion) {
            std::lock_guard lock(mutex);
            executor = completion_executor;
        }
        if (!inline_completion && executor != nullptr) {
            executor->post(
                [h = std::move(handler), r = std::move(result)]() mutable { h(std::move(r)); });
            return;
        }
        handler(std::move(result));
    }

    int response_timeout_ms{1000};
    ports::IFrameLog* frame_log{nullptr};
    std::string endpoint_id;
    ports::IExecutor* completion_executor{nullptr};
    mutable std::mutex mutex;

    asio::io_context ctx;
    Strand strand;
    tcp::socket socket;
    std::optional<WorkGuard> guard;
    std::thread worker;

    std::uint16_t transaction_id{1};
    bool connected{false};
};

ModbusTcpTransport::ModbusTcpTransport(int response_timeout_ms)
    : ModbusTcpTransport(ModbusTcpTransportOptions{.response_timeout_ms = response_timeout_ms}) {}

ModbusTcpTransport::ModbusTcpTransport(ModbusTcpTransportOptions options)
    : impl_(std::make_unique<Impl>(std::move(options))) {}

ModbusTcpTransport::~ModbusTcpTransport() {
    close();
}

void ModbusTcpTransport::set_completion_executor(ports::IExecutor* executor) {
    std::lock_guard lock(impl_->mutex);
    impl_->completion_executor = executor;
}

void ModbusTcpTransport::set_frame_log(ports::IFrameLog* log) {
    std::lock_guard lock(impl_->mutex);
    impl_->frame_log = log;
}

void ModbusTcpTransport::set_endpoint_id(std::string id) {
    std::lock_guard lock(impl_->mutex);
    impl_->endpoint_id = std::move(id);
}

void ModbusTcpTransport::async_connect(ports::EndpointAddress endpoint,
                                       ports::ModbusCompletion<void> handler) {
    impl_->ensure_worker();
    asio::post(impl_->strand,
               [this, endpoint = std::move(endpoint), handler = std::move(handler)]() mutable {
                   impl_->reset_socket();
                   impl_->connected = false;

                   asio::error_code resolve_ec;
                   tcp::resolver resolver{impl_->ctx};
                   const auto results =
                       resolver.resolve(endpoint.host, std::to_string(endpoint.port), resolve_ec);
                   if (resolve_ec || results.empty()) {
                       impl_->deliver(
                           std::move(handler),
                           domain::Result<void>{std::unexpected(make_err(
                               domain::ErrorCode::InvalidArgument,
                               "resolve failed: " +
                                   (resolve_ec ? resolve_ec.message() : endpoint.host),
                               false))},
                           false);
                       return;
                   }

                   auto deadline = std::make_shared<asio::steady_timer>(impl_->ctx);
                   auto finished = std::make_shared<std::atomic<bool>>(false);
                   const int timeout_ms = impl_->response_timeout_ms;

                   deadline->expires_after(std::chrono::milliseconds{timeout_ms});
                   deadline->async_wait(asio::bind_executor(
                       impl_->strand,
                       [this, deadline, finished](const asio::error_code& ec) {
                           if (ec || finished->load()) {
                               return;
                           }
                           asio::error_code ignore;
                           impl_->socket.cancel(ignore);
                       }));

                   asio::async_connect(
                       impl_->socket, results,
                       asio::bind_executor(
                           impl_->strand,
                           [this, handler = std::move(handler), deadline, finished](
                               const asio::error_code& ec, const tcp::endpoint&) mutable {
                               if (finished->exchange(true)) {
                                   return;
                               }
                               deadline->cancel();
                               if (ec) {
                                   impl_->reset_socket();
                                   impl_->connected = false;
                                   if (ec == asio::error::operation_aborted) {
                                       impl_->deliver(
                                           std::move(handler),
                                           domain::Result<void>{std::unexpected(make_err(
                                               domain::ErrorCode::Timeout, "operation timeout"))},
                                           false);
                                       return;
                                   }
                                   impl_->deliver(
                                       std::move(handler),
                                       domain::Result<void>{std::unexpected(make_err(
                                           domain::ErrorCode::Connection,
                                           "connect failed: " + ec.message()))},
                                       false);
                                   return;
                               }
                               impl_->connected = true;
                               impl_->deliver(std::move(handler), domain::Result<void>{}, false);
                           }));
               });
}

domain::Result<void> ModbusTcpTransport::connect(const ports::EndpointAddress& endpoint) {
    impl_->ensure_worker();
    std::promise<domain::Result<void>> promise;
    auto future = promise.get_future();

    asio::post(impl_->strand, [this, endpoint, &promise]() mutable {
        impl_->reset_socket();
        impl_->connected = false;

        asio::error_code resolve_ec;
        tcp::resolver resolver{impl_->ctx};
        const auto results =
            resolver.resolve(endpoint.host, std::to_string(endpoint.port), resolve_ec);
        if (resolve_ec || results.empty()) {
            promise.set_value(std::unexpected(make_err(
                domain::ErrorCode::InvalidArgument,
                "resolve failed: " + (resolve_ec ? resolve_ec.message() : endpoint.host),
                false)));
            return;
        }

        auto deadline = std::make_shared<asio::steady_timer>(impl_->ctx);
        auto finished = std::make_shared<std::atomic<bool>>(false);
        const int timeout_ms = impl_->response_timeout_ms;

        deadline->expires_after(std::chrono::milliseconds{timeout_ms});
        deadline->async_wait(asio::bind_executor(
            impl_->strand, [this, deadline, finished](const asio::error_code& ec) {
                if (ec || finished->load()) {
                    return;
                }
                asio::error_code ignore;
                impl_->socket.cancel(ignore);
            }));

        asio::async_connect(
            impl_->socket, results,
            asio::bind_executor(
                impl_->strand,
                [this, &promise, deadline, finished](const asio::error_code& ec,
                                                     const tcp::endpoint&) {
                    if (finished->exchange(true)) {
                        return;
                    }
                    deadline->cancel();
                    if (ec) {
                        impl_->reset_socket();
                        impl_->connected = false;
                        if (ec == asio::error::operation_aborted) {
                            promise.set_value(std::unexpected(
                                make_err(domain::ErrorCode::Timeout, "operation timeout")));
                            return;
                        }
                        promise.set_value(std::unexpected(make_err(
                            domain::ErrorCode::Connection, "connect failed: " + ec.message())));
                        return;
                    }
                    impl_->connected = true;
                    promise.set_value({});
                }));
    });

    return future.get();
}

void ModbusTcpTransport::close() {
    if (!impl_) {
        return;
    }
    auto do_close = [this] {
        asio::error_code ec;
        impl_->socket.cancel(ec);
        impl_->socket.shutdown(tcp::socket::shutdown_both, ec);
        impl_->socket.close(ec);
        impl_->connected = false;
    };
    if (impl_->worker.joinable() && impl_->worker.get_id() != std::this_thread::get_id()) {
        std::promise<void> done;
        auto fut = done.get_future();
        asio::post(impl_->strand, [&] {
            do_close();
            done.set_value();
        });
        fut.wait();
    } else {
        do_close();
    }
    impl_->stop_worker();
}

bool ModbusTcpTransport::is_connected() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->connected && impl_->socket.is_open();
}

void ModbusTcpTransport::async_transact(std::uint8_t unit,
                                        std::vector<std::uint8_t> pdu,
                                        ports::ModbusCompletion<std::vector<std::uint8_t>> handler,
                                        bool inline_completion) {
    impl_->ensure_worker();
    asio::post(impl_->strand,
               [this, unit, pdu = std::move(pdu), handler = std::move(handler),
                inline_completion]() mutable {
                   if (!impl_->connected || !impl_->socket.is_open()) {
                       impl_->deliver(
                           std::move(handler),
                           domain::Result<std::vector<std::uint8_t>>{std::unexpected(
                               make_err(domain::ErrorCode::Connection, "not connected"))},
                           inline_completion);
                       return;
                   }

                   const auto tid = impl_->transaction_id++;
                   auto req = std::make_shared<std::vector<std::uint8_t>>(
                       pack_mbap(tid, unit, pdu));

                   ports::FrameRecord frame;
                   frame.ts_ms = wall_now_ms();
                   {
                       std::lock_guard lock(impl_->mutex);
                       frame.endpoint_id = impl_->endpoint_id;
                   }
                   frame.tx = *req;
                   const auto t0 = std::chrono::steady_clock::now();

                   auto finish =
                       [this, inline_completion](
                           ports::ModbusCompletion<std::vector<std::uint8_t>> h,
                           ports::FrameRecord fr,
                           std::chrono::steady_clock::time_point start,
                           domain::Result<std::vector<std::uint8_t>> result) mutable {
                           const auto t1 = std::chrono::steady_clock::now();
                           fr.rtt_ms =
                               std::chrono::duration<double, std::milli>(t1 - start).count();
                           if (!result) {
                               fr.error = result.error().message;
                               if (result.error().protocol_status) {
                                   fr.exception_code = *result.error().protocol_status;
                               }
                               if (result.error().code == domain::ErrorCode::Connection ||
                                   result.error().code == domain::ErrorCode::Timeout) {
                                   impl_->mark_disconnected();
                               }
                           }
                           impl_->emit_frame(std::move(fr));
                           impl_->deliver(std::move(h), std::move(result), inline_completion);
                       };

                   auto deadline = std::make_shared<asio::steady_timer>(impl_->ctx);
                   auto finished = std::make_shared<std::atomic<bool>>(false);
                   const int timeout_ms = impl_->response_timeout_ms;

                   deadline->expires_after(std::chrono::milliseconds{timeout_ms});
                   deadline->async_wait(asio::bind_executor(
                       impl_->strand,
                       [this, deadline, finished](const asio::error_code& ec) {
                           if (ec || finished->load()) {
                               return;
                           }
                           asio::error_code ignore;
                           impl_->socket.cancel(ignore);
                       }));

                   asio::async_write(
                       impl_->socket, asio::buffer(*req),
                       asio::bind_executor(
                           impl_->strand,
                           [this, req, handler = std::move(handler), frame = std::move(frame), t0,
                            finish = std::move(finish), deadline, finished](
                               const asio::error_code& write_ec, std::size_t) mutable {
                               if (finished->load()) {
                                   return;
                               }
                               if (write_ec) {
                                   if (finished->exchange(true)) {
                                       return;
                                   }
                                   deadline->cancel();
                                   if (write_ec == asio::error::operation_aborted) {
                                       finish(std::move(handler), std::move(frame), t0,
                                              std::unexpected(make_err(domain::ErrorCode::Timeout,
                                                                       "operation timeout")));
                                       return;
                                   }
                                   finish(std::move(handler), std::move(frame), t0,
                                          std::unexpected(make_err(
                                              domain::ErrorCode::Connection,
                                              "send failed: " + write_ec.message())));
                                   return;
                               }

                               auto mbap = std::make_shared<std::array<std::uint8_t, 6>>();
                               asio::async_read(
                                   impl_->socket, asio::buffer(*mbap),
                                   asio::bind_executor(
                                       impl_->strand,
                                       [this, req, mbap, handler = std::move(handler),
                                        frame = std::move(frame), t0, finish = std::move(finish),
                                        deadline, finished](const asio::error_code& hdr_ec,
                                                            std::size_t) mutable {
                                           if (finished->load()) {
                                               return;
                                           }
                                           if (hdr_ec) {
                                               if (finished->exchange(true)) {
                                                   return;
                                               }
                                               deadline->cancel();
                                               if (hdr_ec == asio::error::operation_aborted) {
                                                   finish(std::move(handler), std::move(frame), t0,
                                                          std::unexpected(make_err(
                                                              domain::ErrorCode::Timeout,
                                                              "operation timeout")));
                                                   return;
                                               }
                                               if (hdr_ec == asio::error::eof) {
                                                   finish(std::move(handler), std::move(frame), t0,
                                                          std::unexpected(make_err(
                                                              domain::ErrorCode::Connection,
                                                              "peer closed")));
                                                   return;
                                               }
                                               finish(std::move(handler), std::move(frame), t0,
                                                      std::unexpected(make_err(
                                                          domain::ErrorCode::Connection,
                                                          "recv failed: " + hdr_ec.message())));
                                               return;
                                           }

                                           const std::uint16_t length = static_cast<std::uint16_t>(
                                               ((*mbap)[4] << 8) | (*mbap)[5]);
                                           // Modbus TCP: length = unit id + PDU; ADU max 260 →
                                           // length ≤ 254.
                                           if (length < 2 || length > 254) {
                                               if (finished->exchange(true)) {
                                                   return;
                                               }
                                               deadline->cancel();
                                               finish(std::move(handler), std::move(frame), t0,
                                                      std::unexpected(make_err(
                                                          domain::ErrorCode::Decoding,
                                                          "bad MBAP length", false)));
                                               return;
                                           }

                                           auto body =
                                               std::make_shared<std::vector<std::uint8_t>>(length);
                                           asio::async_read(
                                               impl_->socket, asio::buffer(*body),
                                               asio::bind_executor(
                                                   impl_->strand,
                                                   [this, req, mbap, body,
                                                    handler = std::move(handler),
                                                    frame = std::move(frame), t0,
                                                    finish = std::move(finish), deadline, finished](
                                                       const asio::error_code& body_ec,
                                                       std::size_t) mutable {
                                                       if (finished->exchange(true)) {
                                                           return;
                                                       }
                                                       deadline->cancel();
                                                       if (body_ec) {
                                                           if (body_ec ==
                                                               asio::error::operation_aborted) {
                                                               finish(
                                                                   std::move(handler),
                                                                   std::move(frame), t0,
                                                                   std::unexpected(make_err(
                                                                       domain::ErrorCode::Timeout,
                                                                       "operation timeout")));
                                                               return;
                                                           }
                                                           if (body_ec == asio::error::eof) {
                                                               finish(
                                                                   std::move(handler),
                                                                   std::move(frame), t0,
                                                                   std::unexpected(make_err(
                                                                       domain::ErrorCode::
                                                                           Connection,
                                                                       "peer closed")));
                                                               return;
                                                           }
                                                           finish(
                                                               std::move(handler), std::move(frame),
                                                               t0,
                                                               std::unexpected(make_err(
                                                                   domain::ErrorCode::Connection,
                                                                   "recv failed: " +
                                                                       body_ec.message())));
                                                           return;
                                                       }

                                                       frame.rx.assign(mbap->begin(), mbap->end());
                                                       frame.rx.insert(frame.rx.end(), body->begin(),
                                                                       body->end());

                                                       if (body->size() < 2) {
                                                           finish(std::move(handler),
                                                                  std::move(frame), t0,
                                                                  std::unexpected(make_err(
                                                                      domain::ErrorCode::Decoding,
                                                                      "short PDU", false)));
                                                           return;
                                                       }
                                                       if ((*body)[1] & 0x80u) {
                                                           const int ex =
                                                               body->size() > 2 ? (*body)[2] : -1;
                                                           domain::Error err = make_err(
                                                               domain::ErrorCode::ModbusException,
                                                               "modbus exception", true);
                                                           err.protocol_status = ex;
                                                           finish(std::move(handler),
                                                                  std::move(frame), t0,
                                                                  std::unexpected(err));
                                                           return;
                                                       }
                                                       finish(
                                                           std::move(handler), std::move(frame), t0,
                                                           std::vector<std::uint8_t>(
                                                               body->begin() + 1, body->end()));
                                                   }));
                                       }));
                           }));
               });
}

domain::Result<std::vector<std::uint8_t>>
ModbusTcpTransport::transact(std::uint8_t unit, std::span<const std::uint8_t> pdu) {
    std::promise<domain::Result<std::vector<std::uint8_t>>> promise;
    auto future = promise.get_future();
    async_transact(
        unit, std::vector<std::uint8_t>(pdu.begin(), pdu.end()),
        [&promise](domain::Result<std::vector<std::uint8_t>> result) {
            promise.set_value(std::move(result));
        },
        true);
    return future.get();
}

domain::Result<std::vector<std::uint16_t>>
ModbusTcpTransport::read_holding_registers(std::uint8_t unit,
                                           std::uint16_t address,
                                           std::uint16_t quantity) {
    return modbus_read_registers([this](auto u, auto p) { return transact(u, p); }, 0x03, unit,
                                 address, quantity);
}

domain::Result<std::vector<std::uint16_t>>
ModbusTcpTransport::read_input_registers(std::uint8_t unit,
                                         std::uint16_t address,
                                         std::uint16_t quantity) {
    return modbus_read_registers([this](auto u, auto p) { return transact(u, p); }, 0x04, unit,
                                 address, quantity);
}

domain::Result<std::vector<bool>>
ModbusTcpTransport::read_coils(std::uint8_t unit, std::uint16_t address, std::uint16_t quantity) {
    return modbus_read_bits([this](auto u, auto p) { return transact(u, p); }, 0x01, unit, address,
                            quantity);
}

domain::Result<std::vector<bool>>
ModbusTcpTransport::read_discrete_inputs(std::uint8_t unit,
                                         std::uint16_t address,
                                         std::uint16_t quantity) {
    return modbus_read_bits([this](auto u, auto p) { return transact(u, p); }, 0x02, unit, address,
                            quantity);
}

domain::Result<void>
ModbusTcpTransport::write_single_register(std::uint8_t unit,
                                          std::uint16_t address,
                                          std::uint16_t value) {
    return modbus_write_single_register([this](auto u, auto p) { return transact(u, p); }, unit,
                                        address, value);
}

domain::Result<void>
ModbusTcpTransport::write_multiple_registers(std::uint8_t unit,
                                             std::uint16_t address,
                                             std::span<const std::uint16_t> values) {
    return modbus_write_multiple_registers([this](auto u, auto p) { return transact(u, p); }, unit,
                                           address, values);
}

domain::Result<void>
ModbusTcpTransport::write_single_coil(std::uint8_t unit, std::uint16_t address, bool value) {
    return modbus_write_single_coil([this](auto u, auto p) { return transact(u, p); }, unit, address,
                                    value);
}

domain::Result<void>
ModbusTcpTransport::write_multiple_coils(std::uint8_t unit,
                                         std::uint16_t address,
                                         std::span<const std::uint8_t> values) {
    return modbus_write_multiple_coils([this](auto u, auto p) { return transact(u, p); }, unit,
                                       address, values);
}

void ModbusTcpTransport::async_read_holding_registers(
    std::uint8_t unit,
    std::uint16_t address,
    std::uint16_t quantity,
    ports::ModbusCompletion<std::vector<std::uint16_t>> handler) {
    async_transact(
        unit, make_read_registers_pdu(0x03, address, quantity),
        [quantity, handler = std::move(handler)](
            domain::Result<std::vector<std::uint8_t>> resp) mutable {
            if (!handler) {
                return;
            }
            if (!resp) {
                handler(std::unexpected(resp.error()));
                return;
            }
            handler(decode_register_response(*resp, quantity));
        },
        false);
}

void ModbusTcpTransport::async_read_input_registers(
    std::uint8_t unit,
    std::uint16_t address,
    std::uint16_t quantity,
    ports::ModbusCompletion<std::vector<std::uint16_t>> handler) {
    async_transact(
        unit, make_read_registers_pdu(0x04, address, quantity),
        [quantity, handler = std::move(handler)](
            domain::Result<std::vector<std::uint8_t>> resp) mutable {
            if (!handler) {
                return;
            }
            if (!resp) {
                handler(std::unexpected(resp.error()));
                return;
            }
            handler(decode_register_response(*resp, quantity));
        },
        false);
}

void ModbusTcpTransport::async_read_coils(std::uint8_t unit,
                                          std::uint16_t address,
                                          std::uint16_t quantity,
                                          ports::ModbusCompletion<std::vector<bool>> handler) {
    async_transact(
        unit, make_read_bits_pdu(0x01, address, quantity),
        [quantity, handler = std::move(handler)](
            domain::Result<std::vector<std::uint8_t>> resp) mutable {
            if (!handler) {
                return;
            }
            if (!resp) {
                handler(std::unexpected(resp.error()));
                return;
            }
            handler(decode_bits_response(*resp, quantity));
        },
        false);
}

void ModbusTcpTransport::async_read_discrete_inputs(
    std::uint8_t unit,
    std::uint16_t address,
    std::uint16_t quantity,
    ports::ModbusCompletion<std::vector<bool>> handler) {
    async_transact(
        unit, make_read_bits_pdu(0x02, address, quantity),
        [quantity, handler = std::move(handler)](
            domain::Result<std::vector<std::uint8_t>> resp) mutable {
            if (!handler) {
                return;
            }
            if (!resp) {
                handler(std::unexpected(resp.error()));
                return;
            }
            handler(decode_bits_response(*resp, quantity));
        },
        false);
}

void ModbusTcpTransport::async_write_single_register(std::uint8_t unit,
                                                     std::uint16_t address,
                                                     std::uint16_t value,
                                                     ports::ModbusCompletion<void> handler) {
    async_transact(
        unit, make_write_single_register_pdu(address, value),
        [handler = std::move(handler)](domain::Result<std::vector<std::uint8_t>> resp) mutable {
            if (!handler) {
                return;
            }
            if (!resp) {
                handler(std::unexpected(resp.error()));
                return;
            }
            handler({});
        },
        false);
}

void ModbusTcpTransport::async_write_multiple_registers(std::uint8_t unit,
                                                        std::uint16_t address,
                                                        std::vector<std::uint16_t> values,
                                                        ports::ModbusCompletion<void> handler) {
    async_transact(
        unit, make_write_multiple_registers_pdu(address, values),
        [handler = std::move(handler)](domain::Result<std::vector<std::uint8_t>> resp) mutable {
            if (!handler) {
                return;
            }
            if (!resp) {
                handler(std::unexpected(resp.error()));
                return;
            }
            handler({});
        },
        false);
}

void ModbusTcpTransport::async_write_single_coil(std::uint8_t unit,
                                                 std::uint16_t address,
                                                 bool value,
                                                 ports::ModbusCompletion<void> handler) {
    async_transact(
        unit, make_write_single_coil_pdu(address, value),
        [handler = std::move(handler)](domain::Result<std::vector<std::uint8_t>> resp) mutable {
            if (!handler) {
                return;
            }
            if (!resp) {
                handler(std::unexpected(resp.error()));
                return;
            }
            handler({});
        },
        false);
}

void ModbusTcpTransport::async_write_multiple_coils(std::uint8_t unit,
                                                    std::uint16_t address,
                                                    std::vector<std::uint8_t> values,
                                                    ports::ModbusCompletion<void> handler) {
    auto pdu = make_write_multiple_coils_pdu(address, values);
    if (!pdu) {
        if (handler) {
            // Match async_* deliver path: post via executor when set.
            ports::IExecutor* executor = nullptr;
            {
                std::lock_guard lock(impl_->mutex);
                executor = impl_->completion_executor;
            }
            if (executor != nullptr) {
                executor->post([handler = std::move(handler),
                                err = pdu.error()]() mutable {
                    handler(std::unexpected(err));
                });
            } else {
                handler(std::unexpected(pdu.error()));
            }
        }
        return;
    }
    async_transact(
        unit, std::move(*pdu),
        [handler = std::move(handler)](domain::Result<std::vector<std::uint8_t>> resp) mutable {
            if (!handler) {
                return;
            }
            if (!resp) {
                handler(std::unexpected(resp.error()));
                return;
            }
            handler({});
        },
        false);
}

}  // namespace opc::adapters
