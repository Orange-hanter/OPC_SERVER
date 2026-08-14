#include <catch2/catch_test_macros.hpp>

#include "adapters/modbus_tcp_transport.hpp"
#include "support/loopback_modbus_slave.hpp"

#include <array>
#include <chrono>
#include <thread>
#include <vector>

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

TEST_CASE("ModbusTcpTransport rejects mismatched MBAP and PDU identity",
          "[integration][modbus][tcp][hardening]") {
    LoopbackModbusSlave slave;
    slave.set_holding(1, 0, 42);
    ModbusTcpTransport transport(500);

    const auto require_decoding_error = [&](auto inject) {
        REQUIRE(transport.connect({.host = "127.0.0.1", .port = slave.port()}));
        inject();
        auto result = transport.read_holding_registers(1, 0, 1);
        REQUIRE_FALSE(result);
        CHECK(result.error().code == opc::domain::ErrorCode::Decoding);
        CHECK_FALSE(result.error().retryable);
        CHECK_FALSE(transport.is_connected());
    };

    require_decoding_error([&] { slave.corrupt_transaction_once(); });
    require_decoding_error([&] { slave.corrupt_protocol_once(); });
    require_decoding_error([&] { slave.corrupt_unit_once(); });
    require_decoding_error([&] { slave.corrupt_function_once(); });
    require_decoding_error([&] { slave.corrupt_byte_count_once(); });

    REQUIRE(transport.connect({.host = "127.0.0.1", .port = slave.port()}));
    auto valid = transport.read_holding_registers(1, 0, 1);
    const std::string valid_error = valid ? std::string{} : valid.error().message;
    INFO(valid_error);
    REQUIRE(valid);
    CHECK((*valid)[0] == 42);
}

TEST_CASE("ModbusTcpTransport validates write response echoes",
          "[integration][modbus][tcp][hardening]") {
    LoopbackModbusSlave slave;
    ModbusTcpTransport transport(500);
    REQUIRE(transport.connect({.host = "127.0.0.1", .port = slave.port()}));

    slave.corrupt_write_echo_once();
    auto single = transport.write_single_register(1, 10, 0x1234);
    REQUIRE_FALSE(single);
    CHECK(single.error().code == opc::domain::ErrorCode::Decoding);

    REQUIRE(transport.connect({.host = "127.0.0.1", .port = slave.port()}));
    slave.corrupt_write_echo_once();
    const std::array<std::uint16_t, 2> pair{1, 2};
    auto multiple = transport.write_multiple_registers(1, 20, pair);
    REQUIRE_FALSE(multiple);
    CHECK(multiple.error().code == opc::domain::ErrorCode::Decoding);

    REQUIRE(transport.connect({.host = "127.0.0.1", .port = slave.port()}));
    slave.corrupt_write_echo_once();
    auto coil = transport.write_single_coil(1, 5, true);
    REQUIRE_FALSE(coil);
    CHECK(coil.error().code == opc::domain::ErrorCode::Decoding);
}

TEST_CASE("ModbusTcpTransport enforces Modbus quantity limits before I/O",
          "[integration][modbus][tcp][hardening]") {
    ModbusTcpTransport transport(50);

    auto zero_registers = transport.read_holding_registers(1, 0, 0);
    REQUIRE_FALSE(zero_registers);
    CHECK(zero_registers.error().code == opc::domain::ErrorCode::InvalidArgument);

    auto too_many_registers = transport.read_input_registers(1, 0, 126);
    REQUIRE_FALSE(too_many_registers);
    CHECK(too_many_registers.error().code == opc::domain::ErrorCode::InvalidArgument);

    auto zero_bits = transport.read_coils(1, 0, 0);
    REQUIRE_FALSE(zero_bits);
    CHECK(zero_bits.error().code == opc::domain::ErrorCode::InvalidArgument);

    auto too_many_bits = transport.read_discrete_inputs(1, 0, 2001);
    REQUIRE_FALSE(too_many_bits);
    CHECK(too_many_bits.error().code == opc::domain::ErrorCode::InvalidArgument);

    const std::vector<std::uint16_t> empty;
    auto zero_write = transport.write_multiple_registers(1, 0, empty);
    REQUIRE_FALSE(zero_write);
    CHECK(zero_write.error().code == opc::domain::ErrorCode::InvalidArgument);

    const std::vector<std::uint16_t> too_many(124, 0);
    auto oversized_write = transport.write_multiple_registers(1, 0, too_many);
    REQUIRE_FALSE(oversized_write);
    CHECK(oversized_write.error().code == opc::domain::ErrorCode::InvalidArgument);
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

TEST_CASE("ModbusTcpTransport destructor releases the TCP connection",
          "[integration][modbus][tcp][raii]") {
    LoopbackModbusSlave slave;
    slave.set_holding(1, 0, 77);
    const auto endpoint =
        opc::ports::EndpointAddress{.host = "127.0.0.1", .port = slave.port()};

    {
        ModbusTcpTransport first(500);
        REQUIRE(first.connect(endpoint));
        REQUIRE(first.read_holding_registers(1, 0, 1));
    }

    // The single-threaded slave can serve this request only after the first
    // connection observes EOF, proving that the transport destructor closed it.
    ModbusTcpTransport second(500);
    REQUIRE(second.connect(endpoint));
    auto regs = second.read_holding_registers(1, 0, 1);
    REQUIRE(regs);
    CHECK((*regs)[0] == 77);
}
