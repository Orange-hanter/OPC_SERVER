#include <catch2/catch_test_macros.hpp>

#include "adapters/modbus_tcp_transport.hpp"
#include "support/loopback_modbus_slave.hpp"

#include <chrono>
#include <thread>

using opc::adapters::ModbusTcpTransport;

TEST_CASE("ModbusTcpTransport reads and writes holding registers on loopback",
          "[integration][modbus][tcp]") {
    LoopbackModbusSlave slave;
    slave.set_holding(1, 10, 0xCAFE);
    ModbusTcpTransport transport(500);
    REQUIRE(transport.connect({.host = "127.0.0.1", .port = slave.port()}));
    auto regs = transport.read_holding_registers(1, 10, 2);
    REQUIRE(regs);
    REQUIRE((*regs)[0] == 0xCAFE);
    REQUIRE((*regs)[1] == 0);

    REQUIRE(transport.write_single_register(1, 10, 0xBEEF));
    REQUIRE(slave.holding(1, 10) == 0xBEEF);

    std::uint16_t pair[] = {1, 2};
    REQUIRE(transport.write_multiple_registers(1, 20, pair));
    REQUIRE(slave.holding(1, 20) == 1);
    REQUIRE(slave.holding(1, 21) == 2);
}

TEST_CASE("ModbusTcpTransport coils and exception codes", "[integration][modbus][tcp]") {
    LoopbackModbusSlave slave;
    slave.set_coil(1, 3, true);
    ModbusTcpTransport transport(500);
    REQUIRE(transport.connect({.host = "127.0.0.1", .port = slave.port()}));
    auto coils = transport.read_coils(1, 3, 1);
    REQUIRE(coils);
    REQUIRE((*coils)[0]);
    REQUIRE(transport.write_single_coil(1, 4, true));
    REQUIRE(slave.coil(1, 4));

    slave.fail_illegal_address_once();
    auto failed = transport.read_holding_registers(1, 0, 1);
    REQUIRE_FALSE(failed);
    REQUIRE(failed.error().code == opc::domain::ErrorCode::ModbusException);
    REQUIRE(failed.error().protocol_status == 2);
}

TEST_CASE("ModbusTcpTransport reads input registers, discrete inputs and packed coils",
          "[integration][modbus][tcp]") {
    LoopbackModbusSlave slave;
    slave.set_input(1, 7, 0x1111);
    slave.set_discrete(1, 0, true);
    slave.set_discrete(1, 9, true);
    for (std::uint16_t i = 0; i < 10; ++i) {
        slave.set_coil(1, i, (i % 2) == 0);
    }
    ModbusTcpTransport transport(500);
    REQUIRE(transport.connect({.host = "127.0.0.1", .port = slave.port()}));

    auto inputs = transport.read_input_registers(1, 7, 1);
    REQUIRE(inputs);
    REQUIRE((*inputs)[0] == 0x1111);

    auto discs = transport.read_discrete_inputs(1, 0, 10);
    REQUIRE(discs);
    REQUIRE((*discs)[0]);
    REQUIRE((*discs)[9]);
    REQUIRE_FALSE((*discs)[1]);

    auto coils = transport.read_coils(1, 0, 10);
    REQUIRE(coils);
    REQUIRE(coils->size() == 10);
    REQUIRE((*coils)[0]);
    REQUIRE_FALSE((*coils)[1]);
    REQUIRE((*coils)[8]);
}

TEST_CASE("ModbusTcpTransport maps exception 03 illegal value", "[integration][modbus][tcp]") {
    LoopbackModbusSlave slave;
    ModbusTcpTransport transport(500);
    REQUIRE(transport.connect({.host = "127.0.0.1", .port = slave.port()}));
    slave.fail_illegal_value_once();
    auto failed = transport.read_input_registers(1, 0, 1);
    REQUIRE_FALSE(failed);
    REQUIRE(failed.error().code == opc::domain::ErrorCode::ModbusException);
    REQUIRE(failed.error().protocol_status == 3);
}

TEST_CASE("ModbusTcpTransport reconnects after peer close", "[integration][modbus][tcp]") {
    LoopbackModbusSlave slave;
    slave.set_holding(1, 0, 9);
    ModbusTcpTransport transport(500);
    const auto port = slave.port();
    REQUIRE(transport.connect({.host = "127.0.0.1", .port = port}));
    REQUIRE(transport.read_holding_registers(1, 0, 1));
    transport.close();
    REQUIRE_FALSE(transport.is_connected());
    REQUIRE(transport.connect({.host = "127.0.0.1", .port = port}));
    auto regs = transport.read_holding_registers(1, 0, 1);
    REQUIRE(regs);
    REQUIRE((*regs)[0] == 9);
}
