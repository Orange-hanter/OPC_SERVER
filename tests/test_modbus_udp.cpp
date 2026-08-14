#include <catch2/catch_test_macros.hpp>

#include "adapters/modbus_protocol.hpp"
#include "adapters/modbus_udp_transport.hpp"

#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <unordered_map>
#include <unistd.h>
#include <vector>

using opc::adapters::ModbusUdpTransport;

namespace {

class UdpHoldingSlave {
public:
    UdpHoldingSlave() {
        fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
        REQUIRE(fd_ >= 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        REQUIRE(::bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
        socklen_t len = sizeof(addr);
        REQUIRE(::getsockname(fd_, reinterpret_cast<sockaddr*>(&addr), &len) == 0);
        port_ = ntohs(addr.sin_port);
        run_ = true;
        thread_ = std::thread([this] { loop(); });
    }

    ~UdpHoldingSlave() {
        run_ = false;
        if (fd_ >= 0) {
            ::shutdown(fd_, SHUT_RDWR);
            ::close(fd_);
            fd_ = -1;
        }
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    [[nodiscard]] std::uint16_t port() const { return port_; }

    void set_holding(std::uint16_t address, std::uint16_t value) { holding_[address] = value; }

private:
    void loop() {
        while (run_) {
            std::uint8_t buf[260];
            sockaddr_in from{};
            socklen_t from_len = sizeof(from);
            const auto n = ::recvfrom(fd_, buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from), &from_len);
            if (n < 8 || !run_) {
                continue;
            }
            const auto unpacked = opc::adapters::unpack_adu({buf, static_cast<std::size_t>(n)});
            if (!unpacked || unpacked->pdu.empty() || unpacked->pdu[0] != 0x03 || unpacked->pdu.size() < 5) {
                continue;
            }
            const std::uint16_t addr =
                static_cast<std::uint16_t>((unpacked->pdu[1] << 8) | unpacked->pdu[2]);
            const std::uint16_t qty =
                static_cast<std::uint16_t>((unpacked->pdu[3] << 8) | unpacked->pdu[4]);
            std::vector<std::uint8_t> pdu;
            pdu.push_back(0x03);
            pdu.push_back(static_cast<std::uint8_t>(qty * 2));
            for (std::uint16_t i = 0; i < qty; ++i) {
                const auto v = holding_[static_cast<std::uint16_t>(addr + i)];
                pdu.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
                pdu.push_back(static_cast<std::uint8_t>(v & 0xFF));
            }
            const auto rsp = opc::adapters::pack_mbap(unpacked->transaction_id, unpacked->unit, pdu);
            ::sendto(fd_, rsp.data(), rsp.size(), 0, reinterpret_cast<sockaddr*>(&from), from_len);
        }
    }

    int fd_{-1};
    std::uint16_t port_{0};
    std::atomic<bool> run_{false};
    std::thread thread_;
    std::unordered_map<std::uint16_t, std::uint16_t> holding_;
};

}  // namespace

TEST_CASE("Modbus UDP roundtrips holding registers over loopback", "[modbus][udp]") {
    UdpHoldingSlave slave;
    slave.set_holding(10, 0x1234);
    slave.set_holding(11, 0xABCD);

    ModbusUdpTransport client{200};
    REQUIRE(client.connect({.host = "127.0.0.1", .port = slave.port()}));
    auto regs = client.read_holding_registers(1, 10, 2);
    REQUIRE(regs);
    REQUIRE(regs->size() == 2);
    CHECK((*regs)[0] == 0x1234);
    CHECK((*regs)[1] == 0xABCD);
    client.close();
}

TEST_CASE("pack/unpack MBAP ADU", "[modbus][udp]") {
    const std::uint8_t pdu[] = {0x03, 0x00, 0x01};
    const auto adu = opc::adapters::pack_mbap(7, 2, pdu);
    auto unpacked = opc::adapters::unpack_adu(adu);
    REQUIRE(unpacked);
    CHECK(unpacked->transaction_id == 7);
    CHECK(unpacked->unit == 2);
    REQUIRE(unpacked->pdu.size() == 3);
    CHECK(unpacked->pdu[0] == 0x03);
}
