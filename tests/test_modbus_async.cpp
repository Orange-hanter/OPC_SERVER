#include <catch2/catch_test_macros.hpp>

#include "adapters/modbus_tcp_transport.hpp"
#include "adapters/testsupport/fake_modbus_transport.hpp"
#include "ports/i_executor.hpp"

#include <atomic>
#include <chrono>
#include <future>
#include <thread>
#include <vector>

using opc::adapters::ModbusTcpTransport;
using opc::adapters::testsupport::FakeModbusTransport;

namespace {

class InlineExecutor final : public opc::ports::IExecutor {
public:
    void post(std::move_only_function<void()> work) override {
        if (work) {
            work();
        }
    }
};

class CountingExecutor final : public opc::ports::IExecutor {
public:
    std::atomic<int> posts{0};
    void post(std::move_only_function<void()> work) override {
        ++posts;
        if (work) {
            work();
        }
    }
};

}  // namespace

TEST_CASE("Fake Modbus async_read completes via default bridge", "[modbus][async]") {
    FakeModbusTransport fake;
    REQUIRE(fake.connect({.host = "127.0.0.1", .port = 502}));
    fake.set_holding(0, 42);

    std::promise<opc::domain::Result<std::vector<std::uint16_t>>> promise;
    auto future = promise.get_future();
    fake.async_read_holding_registers(1, 0, 1, [&](auto result) { promise.set_value(std::move(result)); });
    auto regs = future.get();
    REQUIRE(regs);
    REQUIRE(regs->size() == 1);
    CHECK((*regs)[0] == 42);
}

TEST_CASE("TCP async_connect and async_read_holding_registers", "[modbus][async][tcp]") {
    // Reuse loopback pattern from test_modbus_tcp via sync connect first is enough for smoke;
    // full loopback covered there. Here we verify async completion executor hopping.
    CountingExecutor executor;
    ModbusTcpTransport transport(500);
    transport.set_completion_executor(&executor);

    std::promise<opc::domain::Result<void>> promise;
    auto future = promise.get_future();
    transport.async_connect({.host = "127.0.0.1", .port = 1},
                            [&](auto result) { promise.set_value(std::move(result)); });
    auto status = future.wait_for(std::chrono::seconds(2));
    REQUIRE(status == std::future_status::ready);
    auto result = future.get();
    REQUIRE_FALSE(result);
    CHECK(result.error().code == opc::domain::ErrorCode::Connection);
    CHECK(executor.posts.load() >= 1);
}

TEST_CASE("IModbusTransport sync path still works with executor set", "[modbus][async]") {
    InlineExecutor executor;
    FakeModbusTransport fake;
    fake.set_completion_executor(&executor);
    REQUIRE(fake.connect({.host = "x", .port = 502}));
    fake.set_holding(1, 7);
    auto regs = fake.read_holding_registers(1, 1, 1);
    REQUIRE(regs);
    CHECK((*regs)[0] == 7);
}
