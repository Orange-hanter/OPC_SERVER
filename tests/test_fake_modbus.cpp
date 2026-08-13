#include <catch2/catch_test_macros.hpp>

#include "adapters/testsupport/fake_modbus_transport.hpp"

#include <cstdint>

TEST_CASE("FakeModbusTransport holding roundtrip", "[adapters][fake]") {
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

TEST_CASE("FakeModbusTransport FC15 writes multiple coils", "[adapters][fake]") {
    opc::adapters::testsupport::FakeModbusTransport transport;
    REQUIRE(transport.connect({.host = "127.0.0.1", .port = 502}));
    const std::uint8_t bits[] = {1, 0, 1};
    REQUIRE(transport.write_multiple_coils(1, 10, bits).has_value());
    CHECK(transport.fc15_writes() == 1);
    const auto coils = transport.read_coils(1, 10, 3);
    REQUIRE(coils.has_value());
    REQUIRE(coils->size() == 3);
    CHECK((*coils)[0] == true);
    CHECK((*coils)[1] == false);
    CHECK((*coils)[2] == true);
}
