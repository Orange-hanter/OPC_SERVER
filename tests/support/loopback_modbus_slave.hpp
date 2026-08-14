#pragma once

#include "support/free_tcp_port.hpp"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <span>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

/// Minimal Modbus TCP slave for loopback integration tests (FC 1,2,3,4,5,6,16).
class LoopbackModbusSlave {
public:
    explicit LoopbackModbusSlave(std::uint16_t port = 0) {
        listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd_ < 0) {
            throw std::runtime_error("LoopbackModbusSlave: socket failed");
        }
        int yes = 1;
        ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(port);
        if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            ::close(listen_fd_);
            throw std::runtime_error("LoopbackModbusSlave: bind failed");
        }
        socklen_t len = sizeof(addr);
        if (::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
            ::close(listen_fd_);
            throw std::runtime_error("LoopbackModbusSlave: getsockname failed");
        }
        port_ = ntohs(addr.sin_port);
        if (::listen(listen_fd_, 8) != 0) {
            ::close(listen_fd_);
            throw std::runtime_error("LoopbackModbusSlave: listen failed");
        }
        running_ = true;
        thread_ = std::thread([this] { serve(); });
    }

    LoopbackModbusSlave(const LoopbackModbusSlave&) = delete;
    LoopbackModbusSlave& operator=(const LoopbackModbusSlave&) = delete;

    ~LoopbackModbusSlave() { stop(); }

    [[nodiscard]] std::uint16_t port() const { return port_; }

    void set_holding(std::uint8_t unit, std::uint16_t address, std::uint16_t value) {
        std::lock_guard lock(mutex_);
        holding_[key(unit, address)] = value;
    }
    void set_input(std::uint8_t unit, std::uint16_t address, std::uint16_t value) {
        std::lock_guard lock(mutex_);
        input_[key(unit, address)] = value;
    }
    void set_coil(std::uint8_t unit, std::uint16_t address, bool value) {
        std::lock_guard lock(mutex_);
        coils_[key(unit, address)] = value;
    }
    void set_discrete(std::uint8_t unit, std::uint16_t address, bool value) {
        std::lock_guard lock(mutex_);
        discrete_[key(unit, address)] = value;
    }

    [[nodiscard]] std::uint16_t holding(std::uint8_t unit, std::uint16_t address) const {
        std::lock_guard lock(mutex_);
        const auto it = holding_.find(key(unit, address));
        return it == holding_.end() ? 0 : it->second;
    }
    [[nodiscard]] bool coil(std::uint8_t unit, std::uint16_t address) const {
        std::lock_guard lock(mutex_);
        const auto it = coils_.find(key(unit, address));
        return it != coils_.end() && it->second;
    }

    void fail_illegal_address_once() { illegal_address_once_ = true; }
    void fail_illegal_value_once() { illegal_value_once_ = true; }
    void corrupt_transaction_once() { corrupt_transaction_once_ = true; }
    void corrupt_protocol_once() { corrupt_protocol_once_ = true; }
    void corrupt_unit_once() { corrupt_unit_once_ = true; }
    void corrupt_function_once() { corrupt_function_once_ = true; }
    void corrupt_byte_count_once() { corrupt_byte_count_once_ = true; }
    void corrupt_write_echo_once() { corrupt_write_echo_once_ = true; }

    void stop() {
        if (!running_.exchange(false)) {
            return;
        }
        if (listen_fd_ >= 0) {
            ::shutdown(listen_fd_, SHUT_RDWR);
            ::close(listen_fd_);
            listen_fd_ = -1;
        }
        if (client_fd_ >= 0) {
            ::shutdown(client_fd_, SHUT_RDWR);
            ::close(client_fd_);
            client_fd_ = -1;
        }
        if (thread_.joinable()) {
            thread_.join();
        }
    }

private:
    using Key = std::uint32_t;
    static Key key(std::uint8_t unit, std::uint16_t address) {
        return (static_cast<Key>(unit) << 16) | address;
    }

    static std::uint16_t read_u16(const std::uint8_t* p) {
        return static_cast<std::uint16_t>((p[0] << 8) | p[1]);
    }
    static void append_u16(std::vector<std::uint8_t>& out, std::uint16_t value) {
        out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
        out.push_back(static_cast<std::uint8_t>(value & 0xFF));
    }

    void serve() {
        while (running_) {
            sockaddr_in peer{};
            socklen_t plen = sizeof(peer);
            const int fd = ::accept(listen_fd_, reinterpret_cast<sockaddr*>(&peer), &plen);
            if (fd < 0) {
                if (!running_) {
                    break;
                }
                continue;
            }
            client_fd_ = fd;
            serve_client(fd);
            if (client_fd_ == fd) {
                ::close(fd);
                client_fd_ = -1;
            }
        }
    }

    void serve_client(int fd) {
        while (running_) {
            std::uint8_t header[6];
            if (!read_full(fd, header, sizeof(header))) {
                return;
            }
            const std::uint16_t tid = read_u16(header);
            const std::uint16_t length = read_u16(header + 4);
            if (length < 2 || length > 260) {
                return;
            }
            std::vector<std::uint8_t> body(length);
            if (!read_full(fd, body.data(), body.size())) {
                return;
            }
            const std::uint8_t unit = body[0];
            const std::uint8_t fn = body[1];
            std::vector<std::uint8_t> pdu = handle_pdu(
                unit, fn, std::span<const std::uint8_t>(body.data() + 2, body.size() - 2));
            std::vector<std::uint8_t> resp;
            append_u16(resp, tid);
            append_u16(resp, 0);
            append_u16(resp, static_cast<std::uint16_t>(1 + pdu.size()));
            resp.push_back(unit);
            resp.insert(resp.end(), pdu.begin(), pdu.end());
            if (corrupt_transaction_once_.exchange(false)) {
                resp[1] = static_cast<std::uint8_t>(resp[1] ^ 0x01u);
            }
            if (corrupt_protocol_once_.exchange(false)) {
                resp[3] = 0x01;
            }
            if (corrupt_unit_once_.exchange(false)) {
                resp[6] = static_cast<std::uint8_t>(resp[6] + 1u);
            }
            if (corrupt_function_once_.exchange(false)) {
                resp[7] = static_cast<std::uint8_t>(resp[7] ^ 0x01u);
            }
            if (corrupt_byte_count_once_.exchange(false) && resp.size() > 8) {
                resp[8] = static_cast<std::uint8_t>(resp[8] + 1u);
            }
            if (corrupt_write_echo_once_.exchange(false) && resp.size() > 9) {
                resp[9] = static_cast<std::uint8_t>(resp[9] ^ 0x01u);
            }
            if (!write_full(fd, resp.data(), resp.size())) {
                return;
            }
        }
    }

    std::vector<std::uint8_t> handle_pdu(std::uint8_t unit,
                                         std::uint8_t fn,
                                         std::span<const std::uint8_t> data) {
        if (illegal_address_once_.exchange(false)) {
            return {static_cast<std::uint8_t>(fn | 0x80u), 0x02};
        }
        if (illegal_value_once_.exchange(false)) {
            return {static_cast<std::uint8_t>(fn | 0x80u), 0x03};
        }
        if ((fn == 0x03 || fn == 0x04 || fn == 0x01 || fn == 0x02) && data.size() >= 4) {
            const auto addr = read_u16(data.data());
            const auto qty = read_u16(data.data() + 2);
            if (fn == 0x03 || fn == 0x04) {
                std::vector<std::uint8_t> pdu;
                pdu.push_back(fn);
                pdu.push_back(static_cast<std::uint8_t>(qty * 2));
                std::lock_guard lock(mutex_);
                auto& map = fn == 0x03 ? holding_ : input_;
                for (std::uint16_t i = 0; i < qty; ++i) {
                    const auto it = map.find(key(unit, static_cast<std::uint16_t>(addr + i)));
                    append_u16(pdu, it == map.end() ? 0 : it->second);
                }
                return pdu;
            }
            const std::uint8_t byte_count = static_cast<std::uint8_t>((qty + 7) / 8);
            std::vector<std::uint8_t> pdu{fn, byte_count};
            pdu.resize(2u + byte_count, 0);
            std::lock_guard lock(mutex_);
            auto& map = fn == 0x01 ? coils_ : discrete_;
            for (std::uint16_t i = 0; i < qty; ++i) {
                const auto it = map.find(key(unit, static_cast<std::uint16_t>(addr + i)));
                if (it != map.end() && it->second) {
                    pdu[2 + i / 8] = static_cast<std::uint8_t>(pdu[2 + i / 8] | (1u << (i % 8)));
                }
            }
            return pdu;
        }
        if (fn == 0x06 && data.size() >= 4) {
            const auto addr = read_u16(data.data());
            const auto value = read_u16(data.data() + 2);
            {
                std::lock_guard lock(mutex_);
                holding_[key(unit, addr)] = value;
            }
            std::vector<std::uint8_t> pdu{fn};
            append_u16(pdu, addr);
            append_u16(pdu, value);
            return pdu;
        }
        if (fn == 0x10 && data.size() >= 5) {
            const auto addr = read_u16(data.data());
            const auto qty = read_u16(data.data() + 2);
            {
                std::lock_guard lock(mutex_);
                for (std::uint16_t i = 0; i < qty && static_cast<std::size_t>(5 + i * 2 + 1) < data.size(); ++i) {
                    holding_[key(unit, static_cast<std::uint16_t>(addr + i))] =
                        read_u16(data.data() + 5 + i * 2);
                }
            }
            std::vector<std::uint8_t> pdu{fn};
            append_u16(pdu, addr);
            append_u16(pdu, qty);
            return pdu;
        }
        if (fn == 0x05 && data.size() >= 4) {
            const auto addr = read_u16(data.data());
            const auto raw = read_u16(data.data() + 2);
            {
                std::lock_guard lock(mutex_);
                coils_[key(unit, addr)] = raw == 0xFF00;
            }
            std::vector<std::uint8_t> pdu{fn};
            append_u16(pdu, addr);
            append_u16(pdu, raw);
            return pdu;
        }
        return {static_cast<std::uint8_t>(fn | 0x80u), 0x01};
    }

    static bool read_full(int fd, std::uint8_t* buf, std::size_t n) {
        std::size_t got = 0;
        while (got < n) {
            const auto r = ::recv(fd, buf + got, n - got, 0);
            if (r <= 0) {
                return false;
            }
            got += static_cast<std::size_t>(r);
        }
        return true;
    }

    static bool write_full(int fd, const std::uint8_t* buf, std::size_t n) {
        std::size_t sent = 0;
        while (sent < n) {
            const auto w = ::send(fd, buf + sent, n - sent, MSG_NOSIGNAL);
            if (w <= 0) {
                return false;
            }
            sent += static_cast<std::size_t>(w);
        }
        return true;
    }

    int listen_fd_{-1};
    int client_fd_{-1};
    std::uint16_t port_{0};
    std::atomic<bool> running_{false};
    std::atomic<bool> illegal_address_once_{false};
    std::atomic<bool> illegal_value_once_{false};
    std::atomic<bool> corrupt_transaction_once_{false};
    std::atomic<bool> corrupt_protocol_once_{false};
    std::atomic<bool> corrupt_unit_once_{false};
    std::atomic<bool> corrupt_function_once_{false};
    std::atomic<bool> corrupt_byte_count_once_{false};
    std::atomic<bool> corrupt_write_echo_once_{false};
    std::thread thread_;
    mutable std::mutex mutex_;
    std::unordered_map<Key, std::uint16_t> holding_;
    std::unordered_map<Key, std::uint16_t> input_;
    std::unordered_map<Key, bool> coils_;
    std::unordered_map<Key, bool> discrete_;
};
