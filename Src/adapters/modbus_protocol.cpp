#include "adapters/modbus_protocol.hpp"

namespace opc::adapters {
namespace {

domain::Error proto_err(domain::ErrorCode code, std::string message, bool retryable = false) {
    return domain::Error{code, std::move(message), "adapters.modbus", retryable};
}

void append_u16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
}

}  // namespace

std::vector<std::uint8_t>
pack_mbap(std::uint16_t transaction_id, std::uint8_t unit, std::span<const std::uint8_t> pdu) {
    std::vector<std::uint8_t> req;
    req.reserve(7 + pdu.size());
    append_u16(req, transaction_id);
    append_u16(req, 0);
    append_u16(req, static_cast<std::uint16_t>(1 + pdu.size()));
    req.push_back(unit);
    req.insert(req.end(), pdu.begin(), pdu.end());
    return req;
}

domain::Result<UnpackedAdu> unpack_adu(std::span<const std::uint8_t> adu) {
    if (adu.size() < 8) {
        return std::unexpected(proto_err(domain::ErrorCode::Decoding, "short Modbus ADU"));
    }
    const std::uint16_t length = static_cast<std::uint16_t>((adu[4] << 8) | adu[5]);
    if (length < 2 || static_cast<std::size_t>(6 + length) != adu.size()) {
        return std::unexpected(proto_err(domain::ErrorCode::Decoding, "bad MBAP length"));
    }
    UnpackedAdu out;
    out.transaction_id = static_cast<std::uint16_t>((adu[0] << 8) | adu[1]);
    out.unit = adu[6];
    const auto* body = adu.data() + 6;  // unit + PDU
    if (body[1] & 0x80u) {
        const int ex = length > 2 ? static_cast<int>(adu[8]) : -1;
        domain::Error err = proto_err(domain::ErrorCode::ModbusException, "modbus exception", true);
        err.protocol_status = ex;
        return std::unexpected(err);
    }
    out.pdu.assign(adu.begin() + 7, adu.end());
    return out;
}

domain::Result<std::vector<std::uint16_t>>
modbus_read_registers(const ModbusTransact& tx,
                      std::uint8_t function,
                      std::uint8_t unit,
                      std::uint16_t address,
                      std::uint16_t quantity) {
    auto resp = tx(unit, make_read_registers_pdu(function, address, quantity));
    if (!resp) {
        return std::unexpected(resp.error());
    }
    return decode_register_response(*resp, quantity);
}

domain::Result<std::vector<bool>>
modbus_read_bits(const ModbusTransact& tx,
                 std::uint8_t function,
                 std::uint8_t unit,
                 std::uint16_t address,
                 std::uint16_t quantity) {
    auto resp = tx(unit, make_read_bits_pdu(function, address, quantity));
    if (!resp) {
        return std::unexpected(resp.error());
    }
    return decode_bits_response(*resp, quantity);
}

domain::Result<void>
modbus_write_single_register(const ModbusTransact& tx,
                             std::uint8_t unit,
                             std::uint16_t address,
                             std::uint16_t value) {
    auto resp = tx(unit, make_write_single_register_pdu(address, value));
    if (!resp) {
        return std::unexpected(resp.error());
    }
    return {};
}

domain::Result<void>
modbus_write_multiple_registers(const ModbusTransact& tx,
                                std::uint8_t unit,
                                std::uint16_t address,
                                std::span<const std::uint16_t> values) {
    auto resp = tx(unit, make_write_multiple_registers_pdu(address, values));
    if (!resp) {
        return std::unexpected(resp.error());
    }
    return {};
}

domain::Result<void>
modbus_write_single_coil(const ModbusTransact& tx,
                         std::uint8_t unit,
                         std::uint16_t address,
                         bool value) {
    auto resp = tx(unit, make_write_single_coil_pdu(address, value));
    if (!resp) {
        return std::unexpected(resp.error());
    }
    return {};
}

domain::Result<void>
modbus_write_multiple_coils(const ModbusTransact& tx,
                            std::uint8_t unit,
                            std::uint16_t address,
                            std::span<const std::uint8_t> values) {
    auto pdu = make_write_multiple_coils_pdu(address, values);
    if (!pdu) {
        return std::unexpected(pdu.error());
    }
    auto resp = tx(unit, *pdu);
    if (!resp) {
        return std::unexpected(resp.error());
    }
    return {};
}

std::vector<std::uint8_t>
make_read_registers_pdu(std::uint8_t function, std::uint16_t address, std::uint16_t quantity) {
    std::vector<std::uint8_t> pdu;
    pdu.push_back(function);
    append_u16(pdu, address);
    append_u16(pdu, quantity);
    return pdu;
}

std::vector<std::uint8_t>
make_read_bits_pdu(std::uint8_t function, std::uint16_t address, std::uint16_t quantity) {
    return make_read_registers_pdu(function, address, quantity);
}

std::vector<std::uint8_t>
make_write_single_register_pdu(std::uint16_t address, std::uint16_t value) {
    std::vector<std::uint8_t> pdu;
    pdu.push_back(0x06);
    append_u16(pdu, address);
    append_u16(pdu, value);
    return pdu;
}

std::vector<std::uint8_t>
make_write_multiple_registers_pdu(std::uint16_t address, std::span<const std::uint16_t> values) {
    std::vector<std::uint8_t> pdu;
    pdu.push_back(0x10);
    append_u16(pdu, address);
    append_u16(pdu, static_cast<std::uint16_t>(values.size()));
    pdu.push_back(static_cast<std::uint8_t>(values.size() * 2));
    for (auto v : values) {
        append_u16(pdu, v);
    }
    return pdu;
}

std::vector<std::uint8_t>
make_write_single_coil_pdu(std::uint16_t address, bool value) {
    std::vector<std::uint8_t> pdu;
    pdu.push_back(0x05);
    append_u16(pdu, address);
    append_u16(pdu, value ? 0xFF00 : 0x0000);
    return pdu;
}

domain::Result<std::vector<std::uint8_t>>
make_write_multiple_coils_pdu(std::uint16_t address, std::span<const std::uint8_t> values) {
    if (values.empty() || values.size() > 1968) {
        return std::unexpected(proto_err(domain::ErrorCode::InvalidArgument, "FC15 quantity out of range"));
    }
    const auto quantity = static_cast<std::uint16_t>(values.size());
    const auto byte_count = static_cast<std::uint8_t>((quantity + 7) / 8);
    std::vector<std::uint8_t> packed(byte_count, 0);
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (values[i] != 0) {
            packed[i / 8] = static_cast<std::uint8_t>(packed[i / 8] | (1u << (i % 8)));
        }
    }
    std::vector<std::uint8_t> pdu;
    pdu.push_back(0x0F);
    append_u16(pdu, address);
    append_u16(pdu, quantity);
    pdu.push_back(byte_count);
    pdu.insert(pdu.end(), packed.begin(), packed.end());
    return pdu;
}

domain::Result<std::vector<std::uint16_t>>
decode_register_response(std::span<const std::uint8_t> resp, std::uint16_t quantity) {
    if (resp.size() < 2) {
        return std::unexpected(proto_err(domain::ErrorCode::Decoding, "short register response"));
    }
    const auto byte_count = resp[1];
    if (resp.size() < 2u + byte_count || byte_count != quantity * 2) {
        return std::unexpected(proto_err(domain::ErrorCode::Decoding, "register byte count mismatch"));
    }
    std::vector<std::uint16_t> out;
    out.reserve(quantity);
    for (std::uint16_t i = 0; i < quantity; ++i) {
        const auto hi = resp[2 + i * 2];
        const auto lo = resp[3 + i * 2];
        out.push_back(static_cast<std::uint16_t>((hi << 8) | lo));
    }
    return out;
}

domain::Result<std::vector<bool>>
decode_bits_response(std::span<const std::uint8_t> resp, std::uint16_t quantity) {
    if (resp.size() < 2) {
        return std::unexpected(proto_err(domain::ErrorCode::Decoding, "short bit response"));
    }
    const auto byte_count = resp[1];
    if (resp.size() < 2u + byte_count) {
        return std::unexpected(proto_err(domain::ErrorCode::Decoding, "bit byte count mismatch"));
    }
    std::vector<bool> out;
    out.reserve(quantity);
    for (std::uint16_t i = 0; i < quantity; ++i) {
        const auto byte = resp[2 + i / 8];
        out.push_back(((byte >> (i % 8)) & 0x1u) != 0);
    }
    return out;
}

}  // namespace opc::adapters
