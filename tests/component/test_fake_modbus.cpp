#include <catch2/catch_test_macros.hpp>

#include "adapters/testsupport/fake_modbus_transport.hpp"

TEST_CASE("FakeModbusTransport holding roundtrip", "[component][adapters][fake]") {
    opc::adapters::testsupport::FakeModbusTransport transport;
    REQUIRE_FALSE(transport.is_connected());

    auto connected = transport.connect({.host = "127.0.0.1", .port = 502});
    REQUIRE(connected.has_value());
    REQUIRE(transport.is_connected());

    REQUIRE(transport.write_single_register(1, 10, 0x1234).has_value());
    const auto regs = transport.read_holding_registers(1, 10, 2);
    REQUIRE(regs.has_value());
    REQUIRE(regs->size() == 2);
    REQUIRE((*regs)[0] == 0x1234);
    REQUIRE((*regs)[1] == 0);
}

TEST_CASE("FakeModbusTransport coils, discrete, input and injected faults",
          "[component][adapters][fake]") {
    opc::adapters::testsupport::FakeModbusTransport transport;
    REQUIRE(transport.connect({.host = "127.0.0.1", .port = 502}));
    REQUIRE(transport.write_single_coil(2, 5, true));
    REQUIRE(transport.coil_at(2, 5));
    auto coils = transport.read_coils(2, 5, 1);
    REQUIRE(coils);
    REQUIRE((*coils)[0]);

    transport.set_discrete(2, 1, true);
    auto discs = transport.read_discrete_inputs(2, 1, 2);
    REQUIRE(discs);
    REQUIRE((*discs)[0]);
    REQUIRE_FALSE((*discs)[1]);

    transport.set_input(3, 0, 42);
    auto inputs = transport.read_input_registers(3, 0, 1);
    REQUIRE(inputs);
    REQUIRE((*inputs)[0] == 42);

    transport.fail_next(opc::domain::Error{
        opc::domain::ErrorCode::Timeout, "boom", "fake.modbus", true});
    REQUIRE_FALSE(transport.read_holding_registers(1, 0, 1).has_value());
    REQUIRE(transport.read_holding_registers(1, 0, 1).has_value());

    transport.close();
    REQUIRE_FALSE(transport.is_connected());
    REQUIRE_FALSE(transport.read_holding_registers(1, 0, 1).has_value());
}
