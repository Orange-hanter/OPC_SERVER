#include "core/translator.hpp"

#include <array>
#include <cmath>
#include <cstring>
#include <type_traits>

namespace opc::core {
namespace {

using domain::Error;
using domain::ErrorCode;
using domain::Result;
using domain::ScalarValue;
using project::Tag;
using project::TagType;

int register_count_for(TagType type) {
    switch (type) {
    case TagType::Bool:
    case TagType::UInt16:
    case TagType::Int16:
        return 1;
    case TagType::UInt32:
    case TagType::Int32:
    case TagType::Float32:
        return 2;
    case TagType::Float64:
        return 4;
    }
    return 1;
}

Result<void> ensure_count(const Tag& tag, std::span<const std::uint16_t> registers) {
    const int need = tag.quantity.value_or(register_count_for(tag.type));
    if (static_cast<int>(registers.size()) < need) {
        return std::unexpected(Error{
            ErrorCode::Decoding, "not enough registers for tag type", "core.translator", false});
    }
    return {};
}

std::string effective_order(const Tag& tag) {
    if (!tag.byte_order.empty()) {
        return tag.byte_order;
    }
    switch (tag.type) {
    case TagType::UInt16:
    case TagType::Int16:
        return "AB";
    case TagType::Bool:
        return {};
    default:
        return "ABCD";
    }
}

Result<std::uint16_t> reorder_u16(std::uint16_t raw, const std::string& order) {
    if (order.empty() || order == "AB") {
        return raw;
    }
    if (order == "BA") {
        return static_cast<std::uint16_t>(((raw & 0x00FFu) << 8) | ((raw & 0xFF00u) >> 8));
    }
    return std::unexpected(
        Error{ErrorCode::Decoding, "unsupported byteOrder for 16-bit", "core.translator", false});
}

/// Map two registers into IEEE/host byte order ABCD payload (4 bytes).
Result<std::array<std::uint8_t, 4>> regs_to_be_bytes(std::span<const std::uint16_t> regs,
                                                     const std::string& order) {
    if (regs.size() < 2) {
        return std::unexpected(
            Error{ErrorCode::Decoding, "need 2 registers", "core.translator", false});
    }
    const auto r0 = regs[0];
    const auto r1 = regs[1];
    const std::uint8_t a = static_cast<std::uint8_t>((r0 >> 8) & 0xFF);
    const std::uint8_t b = static_cast<std::uint8_t>(r0 & 0xFF);
    const std::uint8_t c = static_cast<std::uint8_t>((r1 >> 8) & 0xFF);
    const std::uint8_t d = static_cast<std::uint8_t>(r1 & 0xFF);

    std::array<std::uint8_t, 4> out{};
    if (order == "ABCD") {
        out = {a, b, c, d};
    } else if (order == "CDAB") {
        out = {c, d, a, b};
    } else if (order == "BADC") {
        out = {b, a, d, c};
    } else if (order == "DCBA") {
        out = {d, c, b, a};
    } else {
        return std::unexpected(
            Error{ErrorCode::Decoding, "unsupported byteOrder for 32-bit", "core.translator", false});
    }
    return out;
}

Result<std::array<std::uint16_t, 2>> be_bytes_to_regs(std::array<std::uint8_t, 4> be,
                                                     const std::string& order) {
    std::uint8_t a = be[0], b = be[1], c = be[2], d = be[3];
    std::uint8_t p = a, q = b, r = c, s = d;
    if (order == "ABCD") {
        p = a;
        q = b;
        r = c;
        s = d;
    } else if (order == "CDAB") {
        p = c;
        q = d;
        r = a;
        s = b;
    } else if (order == "BADC") {
        p = b;
        q = a;
        r = d;
        s = c;
    } else if (order == "DCBA") {
        p = d;
        q = c;
        r = b;
        s = a;
    } else {
        return std::unexpected(
            Error{ErrorCode::Decoding, "unsupported byteOrder for 32-bit", "core.translator", false});
    }
    const std::uint16_t r0 = static_cast<std::uint16_t>((p << 8) | q);
    const std::uint16_t r1 = static_cast<std::uint16_t>((r << 8) | s);
    return std::array<std::uint16_t, 2>{r0, r1};
}

float bytes_to_float(std::array<std::uint8_t, 4> be) {
    const std::uint32_t bits = (std::uint32_t{be[0]} << 24) | (std::uint32_t{be[1]} << 16) |
                               (std::uint32_t{be[2]} << 8) | std::uint32_t{be[3]};
    float f = 0.0f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

std::array<std::uint8_t, 4> float_to_bytes(float f) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &f, sizeof(bits));
    return {static_cast<std::uint8_t>((bits >> 24) & 0xFF),
            static_cast<std::uint8_t>((bits >> 16) & 0xFF),
            static_cast<std::uint8_t>((bits >> 8) & 0xFF),
            static_cast<std::uint8_t>(bits & 0xFF)};
}

std::uint32_t bytes_to_u32(std::array<std::uint8_t, 4> be) {
    return (std::uint32_t{be[0]} << 24) | (std::uint32_t{be[1]} << 16) | (std::uint32_t{be[2]} << 8) |
           std::uint32_t{be[3]};
}

std::array<std::uint8_t, 4> u32_to_bytes(std::uint32_t v) {
    return {static_cast<std::uint8_t>((v >> 24) & 0xFF),
            static_cast<std::uint8_t>((v >> 16) & 0xFF),
            static_cast<std::uint8_t>((v >> 8) & 0xFF),
            static_cast<std::uint8_t>(v & 0xFF)};
}

double apply_scale(double raw, const Tag& tag) {
    return raw * tag.scale + tag.offset;
}

Result<double> invert_scale(double eng, const Tag& tag) {
    if (tag.scale == 0.0) {
        return std::unexpected(
            Error{ErrorCode::InvalidArgument, "scale is zero", "core.translator", false});
    }
    return (eng - tag.offset) / tag.scale;
}

Result<double> scalar_to_double(const ScalarValue& value) {
    return std::visit(
        [](const auto& v) -> Result<double> {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return std::unexpected(
                    Error{ErrorCode::InvalidArgument, "empty value", "core.translator", false});
            } else if constexpr (std::is_same_v<T, bool>) {
                return v ? 1.0 : 0.0;
            } else {
                return static_cast<double>(v);
            }
        },
        value);
}

}  // namespace

Result<ScalarValue> Translator::decode(const Tag& tag, std::span<const std::uint16_t> registers) {
    if (auto ok = ensure_count(tag, registers); !ok) {
        return std::unexpected(ok.error());
    }
    const auto order = effective_order(tag);

    switch (tag.type) {
    case TagType::Bool:
        return ScalarValue{registers[0] != 0};
    case TagType::UInt16: {
        auto raw = reorder_u16(registers[0], order);
        if (!raw) {
            return std::unexpected(raw.error());
        }
        return ScalarValue{static_cast<std::uint16_t>(
            std::llround(apply_scale(static_cast<double>(*raw), tag)))};
    }
    case TagType::Int16: {
        auto raw = reorder_u16(registers[0], order);
        if (!raw) {
            return std::unexpected(raw.error());
        }
        const auto signed_raw = static_cast<std::int16_t>(*raw);
        return ScalarValue{static_cast<std::int16_t>(
            std::llround(apply_scale(static_cast<double>(signed_raw), tag)))};
    }
    case TagType::UInt32: {
        auto bytes = regs_to_be_bytes(registers, order);
        if (!bytes) {
            return std::unexpected(bytes.error());
        }
        const auto raw = bytes_to_u32(*bytes);
        return ScalarValue{
            static_cast<std::uint32_t>(std::llround(apply_scale(static_cast<double>(raw), tag)))};
    }
    case TagType::Int32: {
        auto bytes = regs_to_be_bytes(registers, order);
        if (!bytes) {
            return std::unexpected(bytes.error());
        }
        const auto raw = static_cast<std::int32_t>(bytes_to_u32(*bytes));
        return ScalarValue{
            static_cast<std::int32_t>(std::llround(apply_scale(static_cast<double>(raw), tag)))};
    }
    case TagType::Float32: {
        auto bytes = regs_to_be_bytes(registers, order);
        if (!bytes) {
            return std::unexpected(bytes.error());
        }
        const float raw = bytes_to_float(*bytes);
        return ScalarValue{static_cast<float>(apply_scale(static_cast<double>(raw), tag))};
    }
    case TagType::Float64: {
        // v1: four registers as big-endian word stream (ABCD…), ignore exotic 64-bit orders.
        std::uint64_t bits = 0;
        for (int i = 0; i < 4; ++i) {
            bits = (bits << 16) | registers[static_cast<std::size_t>(i)];
        }
        double d = 0.0;
        std::memcpy(&d, &bits, sizeof(d));
        return ScalarValue{apply_scale(d, tag)};
    }
    }
    return std::unexpected(Error{ErrorCode::Decoding, "unknown tag type", "core.translator", false});
}

Result<std::vector<std::uint16_t>>
Translator::encode(const Tag& tag, const ScalarValue& engineering_value) {
    const auto order = effective_order(tag);

    switch (tag.type) {
    case TagType::Bool: {
        const auto* b = std::get_if<bool>(&engineering_value);
        if (!b) {
            return std::unexpected(
                Error{ErrorCode::InvalidArgument, "bool expected", "core.translator", false});
        }
        return std::vector<std::uint16_t>{static_cast<std::uint16_t>(*b ? 1 : 0)};
    }
    case TagType::UInt16: {
        auto eng = scalar_to_double(engineering_value);
        if (!eng) {
            return std::unexpected(eng.error());
        }
        auto raw_d = invert_scale(*eng, tag);
        if (!raw_d) {
            return std::unexpected(raw_d.error());
        }
        auto raw = reorder_u16(static_cast<std::uint16_t>(std::llround(*raw_d)), order);
        if (!raw) {
            return std::unexpected(raw.error());
        }
        return std::vector<std::uint16_t>{*raw};
    }
    case TagType::Int16: {
        auto eng = scalar_to_double(engineering_value);
        if (!eng) {
            return std::unexpected(eng.error());
        }
        auto raw_d = invert_scale(*eng, tag);
        if (!raw_d) {
            return std::unexpected(raw_d.error());
        }
        const auto signed_raw = static_cast<std::int16_t>(std::llround(*raw_d));
        auto raw = reorder_u16(static_cast<std::uint16_t>(signed_raw), order);
        if (!raw) {
            return std::unexpected(raw.error());
        }
        return std::vector<std::uint16_t>{*raw};
    }
    case TagType::UInt32:
    case TagType::Int32: {
        auto eng = scalar_to_double(engineering_value);
        if (!eng) {
            return std::unexpected(eng.error());
        }
        auto raw_d = invert_scale(*eng, tag);
        if (!raw_d) {
            return std::unexpected(raw_d.error());
        }
        const auto bits = static_cast<std::uint32_t>(std::llround(*raw_d));
        auto words = be_bytes_to_regs(u32_to_bytes(bits), order);
        if (!words) {
            return std::unexpected(words.error());
        }
        return std::vector<std::uint16_t>{(*words)[0], (*words)[1]};
    }
    case TagType::Float32: {
        auto eng = scalar_to_double(engineering_value);
        if (!eng) {
            return std::unexpected(eng.error());
        }
        auto raw_d = invert_scale(*eng, tag);
        if (!raw_d) {
            return std::unexpected(raw_d.error());
        }
        auto words = be_bytes_to_regs(float_to_bytes(static_cast<float>(*raw_d)), order);
        if (!words) {
            return std::unexpected(words.error());
        }
        return std::vector<std::uint16_t>{(*words)[0], (*words)[1]};
    }
    case TagType::Float64: {
        auto eng = scalar_to_double(engineering_value);
        if (!eng) {
            return std::unexpected(eng.error());
        }
        auto raw_d = invert_scale(*eng, tag);
        if (!raw_d) {
            return std::unexpected(raw_d.error());
        }
        double d = *raw_d;
        std::uint64_t bits = 0;
        std::memcpy(&bits, &d, sizeof(bits));
        return std::vector<std::uint16_t>{
            static_cast<std::uint16_t>((bits >> 48) & 0xFFFF),
            static_cast<std::uint16_t>((bits >> 32) & 0xFFFF),
            static_cast<std::uint16_t>((bits >> 16) & 0xFFFF),
            static_cast<std::uint16_t>(bits & 0xFFFF),
        };
    }
    }
    return std::unexpected(Error{ErrorCode::Decoding, "unknown tag type", "core.translator", false});
}

}  // namespace opc::core
