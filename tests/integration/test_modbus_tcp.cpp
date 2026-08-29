#include <catch2/catch_test_macros.hpp>

#include "adapters/modbus_protocol.hpp"
#include "adapters/modbus_tcp_transport.hpp"

#include <arpa/inet.h>
#include <atomic>
#include <cstring>
#include <mutex>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <unordered_map>
#include <unistd.h>
#include <vector>

using opc::adapters::AsioModbusTcpTransport;
using opc::adapters::ModbusTcpTransport;

namespace {

class TcpHoldingSlave {
public:
    TcpHoldingSlave() {
        const int listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
        REQUIRE(listen_fd >= 0);
        listen_fd_.store(listen_fd);
        int yes = 1;
        ::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        REQUIRE(::bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
        socklen_t len = sizeof(addr);
        REQUIRE(::getsockname(listen_fd, reinterpret_cast<sockaddr*>(&addr), &len) == 0);
        port_ = ntohs(addr.sin_port);
        REQUIRE(::listen(listen_fd, 1) == 0);
        run_.store(true);
        thread_ = std::thread([this] { loop(); });
    }

    ~TcpHoldingSlave() {
        run_.store(false);
        const int listen_fd = listen_fd_.exchange(-1);
        if (listen_fd >= 0) {
            ::shutdown(listen_fd, SHUT_RDWR);
        }
        const int client_fd = client_fd_.exchange(-1);
        if (client_fd >= 0) {
            ::shutdown(client_fd, SHUT_RDWR);
        }
        if (thread_.joinable()) {
            thread_.join();
        }
        if (client_fd >= 0) {
            ::close(client_fd);
        }
        if (listen_fd >= 0) {
            ::close(listen_fd);
        }
    }

    [[nodiscard]] std::uint16_t port() const { return port_; }

    void set_holding(std::uint16_t address, std::uint16_t value) {
        std::lock_guard lock(mutex_);
        holding_[address] = value;
    }

private:
    void loop() {
        while (run_.load()) {
            sockaddr_in from{};
            socklen_t from_len = sizeof(from);
            const int listen_fd = listen_fd_.load();
            if (listen_fd < 0) {
                break;
            }
            const int fd = ::accept(listen_fd, reinterpret_cast<sockaddr*>(&from), &from_len);
            if (fd < 0 || !run_.load()) {
                if (fd >= 0) {
                    ::close(fd);
                }
                continue;
            }
            client_fd_.store(fd);
            serve_client(fd);
            ::close(fd);
            client_fd_.store(-1);
        }
    }

    static bool recv_exact(int fd, std::uint8_t* data, std::size_t len, std::atomic<bool>& run) {
        std::size_t got = 0;
        while (got < len) {
            const auto n = ::recv(fd, data + got, len - got, 0);
            if (n <= 0 || !run.load()) {
                return false;
            }
            got += static_cast<std::size_t>(n);
        }
        return true;
    }

    void serve_client(int fd) {
        while (run_.load()) {
            std::uint8_t mbap[6];
            if (!recv_exact(fd, mbap, sizeof(mbap), run_)) {
                return;
            }
            const std::uint16_t length =
                static_cast<std::uint16_t>((mbap[4] << 8) | mbap[5]);
            if (length < 2) {
                return;
            }
            std::vector<std::uint8_t> body(length);
            if (!recv_exact(fd, body.data(), body.size(), run_)) {
                return;
            }
            // body[0]=unit, body[1]=fc
            if (body.size() < 6 || body[1] != 0x03) {
                continue;
            }
            const std::uint16_t addr =
                static_cast<std::uint16_t>((body[2] << 8) | body[3]);
            const std::uint16_t qty =
                static_cast<std::uint16_t>((body[4] << 8) | body[5]);
            std::vector<std::uint8_t> pdu;
            pdu.push_back(0x03);
            pdu.push_back(static_cast<std::uint8_t>(qty * 2));
            {
                std::lock_guard lock(mutex_);
                for (std::uint16_t i = 0; i < qty; ++i) {
                    const auto v = holding_[static_cast<std::uint16_t>(addr + i)];
                    pdu.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
                    pdu.push_back(static_cast<std::uint8_t>(v & 0xFF));
                }
            }
            const auto tid = static_cast<std::uint16_t>((mbap[0] << 8) | mbap[1]);
            const auto rsp = opc::adapters::pack_mbap(tid, body[0], pdu);
            std::size_t sent = 0;
            while (sent < rsp.size()) {
                const auto n = ::send(fd, rsp.data() + sent, rsp.size() - sent, 0);
                if (n <= 0) {
                    return;
                }
                sent += static_cast<std::size_t>(n);
            }
        }
    }

    std::atomic<int> listen_fd_{-1};
    std::atomic<int> client_fd_{-1};
    std::uint16_t port_{0};
    std::atomic<bool> run_{false};
    std::thread thread_;
    mutable std::mutex mutex_;
    std::unordered_map<std::uint16_t, std::uint16_t> holding_;
};

}  // namespace

TEST_CASE("Asio Modbus TCP roundtrips holding registers over loopback", "[modbus][tcp][asio]") {
    TcpHoldingSlave slave;
    slave.set_holding(10, 0x1234);
    slave.set_holding(11, 0xABCD);

    AsioModbusTcpTransport client{500};
    REQUIRE(client.connect({.host = "127.0.0.1", .port = slave.port()}));
    CHECK(client.is_connected());
    auto regs = client.read_holding_registers(1, 10, 2);
    REQUIRE(regs);
    REQUIRE(regs->size() == 2);
    CHECK((*regs)[0] == 0x1234);
    CHECK((*regs)[1] == 0xABCD);
    client.close();
    CHECK_FALSE(client.is_connected());
}

TEST_CASE("Asio Modbus TCP connect timeout to closed port", "[modbus][tcp][asio]") {
    ModbusTcpTransport client{200};
    auto r = client.connect({.host = "127.0.0.1", .port = 1});
    REQUIRE_FALSE(r);
    CHECK((r.error().code == opc::domain::ErrorCode::Connection ||
           r.error().code == opc::domain::ErrorCode::Timeout));
}
