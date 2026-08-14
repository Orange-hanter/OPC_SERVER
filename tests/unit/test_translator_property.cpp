#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_adapters.hpp>

#include "core/translator.hpp"

#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

using opc::core::Translator;
using opc::project::Tag;
using opc::project::TagType;

namespace {

Tag make_tag(TagType type, std::string order, double scale = 1.0, double offset = 0.0) {
    Tag tag;
    tag.type = type;
    tag.byte_order = std::move(order);
    tag.scale = scale;
    tag.offset = offset;
    return tag;
}

}  // namespace

TEST_CASE("Translator 32-bit byte orders roundtrip", "[unit][core][translator][property]") {
    const auto order = GENERATE(as<std::string>{}, "ABCD", "CDAB", "BADC", "DCBA");
    const auto type = GENERATE(TagType::Float32, TagType::UInt32, TagType::Int32);
    Tag tag = make_tag(type, order);

    opc::domain::ScalarValue original;
    if (type == TagType::Float32) {
        original = 12.5f;
    } else if (type == TagType::UInt32) {
        original = std::uint32_t{0x89ABCDEFu};
    } else {
        original = std::int32_t{-123456};
    }

    auto encoded = Translator::encode(tag, original);
    REQUIRE(encoded.has_value());
    REQUIRE(encoded->size() == 2);
    auto decoded = Translator::decode(tag, *encoded);
    REQUIRE(decoded.has_value());
    if (type == TagType::Float32) {
        REQUIRE(std::get<float>(*decoded) == Catch::Approx(std::get<float>(original)));
    } else if (type == TagType::UInt32) {
        REQUIRE(std::get<std::uint32_t>(*decoded) == std::get<std::uint32_t>(original));
    } else {
        REQUIRE(std::get<std::int32_t>(*decoded) == std::get<std::int32_t>(original));
    }
}

TEST_CASE("Translator 16-bit AB/BA roundtrip with scale", "[unit][core][translator][property]") {
    const auto order = GENERATE(as<std::string>{}, "AB", "BA");
    const auto type = GENERATE(TagType::UInt16, TagType::Int16);
    Tag tag = make_tag(type, order, 0.1, 2.0);

    opc::domain::ScalarValue original =
        type == TagType::UInt16 ? opc::domain::ScalarValue{std::uint16_t{25}}
                                : opc::domain::ScalarValue{std::int16_t{-40}};

    auto encoded = Translator::encode(tag, original);
    REQUIRE(encoded.has_value());
    auto decoded = Translator::decode(tag, *encoded);
    REQUIRE(decoded.has_value());
    if (type == TagType::UInt16) {
        REQUIRE(std::get<std::uint16_t>(*decoded) == 25);
    } else {
        REQUIRE(std::get<std::int16_t>(*decoded) == -40);
    }
}

TEST_CASE("Translator bool and float64 roundtrip", "[unit][core][translator][property]") {
    Tag flag = make_tag(TagType::Bool, {});
    auto enc_bool = Translator::encode(flag, true);
    REQUIRE(enc_bool);
    auto dec_bool = Translator::decode(flag, *enc_bool);
    REQUIRE(dec_bool);
    REQUIRE(std::get<bool>(*dec_bool));

    Tag dbl = make_tag(TagType::Float64, "ABCD");
    auto enc = Translator::encode(dbl, 3.1415926535);
    REQUIRE(enc);
    REQUIRE(enc->size() == 4);
    auto dec = Translator::decode(dbl, *enc);
    REQUIRE(dec);
    REQUIRE(std::get<double>(*dec) == Catch::Approx(3.1415926535));
}

TEST_CASE("Translator rejects short register spans and unknown order", "[unit][core][translator]") {
    Tag tag = make_tag(TagType::Float32, "ABCD");
    std::uint16_t one[] = {1};
    REQUIRE_FALSE(Translator::decode(tag, one).has_value());

    tag.byte_order = "XYZZ";
    std::uint16_t two[] = {1, 2};
    REQUIRE_FALSE(Translator::decode(tag, two).has_value());
}

TEST_CASE("Translator rejects zero scale on encode", "[unit][core][translator]") {
    Tag tag = make_tag(TagType::UInt16, "AB", 0.0, 0.0);
    REQUIRE_FALSE(Translator::encode(tag, std::uint16_t{1}).has_value());
}

TEST_CASE("Translator rejects empty and type-mismatched values", "[unit][core][translator]") {
    Tag flag = make_tag(TagType::Bool, {});
    REQUIRE_FALSE(Translator::encode(flag, std::uint16_t{1}).has_value());
    REQUIRE_FALSE(Translator::encode(flag, std::monostate{}).has_value());

    Tag u16 = make_tag(TagType::UInt16, "AB");
    REQUIRE_FALSE(Translator::encode(u16, std::monostate{}).has_value());
}

TEST_CASE("Translator float32 applies scale and offset on roundtrip",
          "[unit][core][translator][property]") {
    Tag tag = make_tag(TagType::Float32, "ABCD", 2.0, 1.0);
    auto encoded = Translator::encode(tag, 5.0f);
    REQUIRE(encoded);
    auto decoded = Translator::decode(tag, *encoded);
    REQUIRE(decoded);
    REQUIRE(std::get<float>(*decoded) == Catch::Approx(5.0f));
}

TEST_CASE("Translator rejects integer overflow instead of wrapping",
          "[unit][core][translator][hardening]") {
    Tag u16 = make_tag(TagType::UInt16, "AB");
    REQUIRE_FALSE(Translator::encode(u16, std::uint32_t{65'536}));
    REQUIRE_FALSE(Translator::encode(u16, std::int32_t{-1}));

    Tag i16 = make_tag(TagType::Int16, "AB");
    REQUIRE_FALSE(Translator::encode(i16, std::int32_t{32'768}));
    REQUIRE_FALSE(Translator::encode(i16, std::int32_t{-32'769}));

    Tag u32 = make_tag(TagType::UInt32, "ABCD");
    REQUIRE_FALSE(Translator::encode(u32, std::int32_t{-1}));

    Tag i32 = make_tag(TagType::Int32, "ABCD");
    REQUIRE_FALSE(Translator::encode(i32, std::uint32_t{0xFFFFFFFFu}));
}

TEST_CASE("Translator rejects non-finite and overflowing scale results",
          "[unit][core][translator][hardening]") {
    Tag f32 = make_tag(TagType::Float32, "ABCD");
    REQUIRE_FALSE(Translator::encode(f32, std::numeric_limits<double>::infinity()));
    REQUIRE_FALSE(Translator::encode(f32, std::numeric_limits<double>::quiet_NaN()));
    REQUIRE_FALSE(Translator::encode(f32, std::numeric_limits<double>::max()));

    Tag scaled_u16 = make_tag(TagType::UInt16, "AB", 2.0, 0.0);
    const std::uint16_t raw_max[] = {0xFFFF};
    auto decoded = Translator::decode(scaled_u16, raw_max);
    REQUIRE_FALSE(decoded);
    CHECK(decoded.error().code == opc::domain::ErrorCode::InvalidArgument);
}

TEST_CASE("Translator 32-bit byte orders match exact Modbus wire registers",
          "[unit][core][translator][contract]") {
    struct Case {
        const char* order;
        std::array<std::uint16_t, 2> registers;
    };
    constexpr std::array cases = {
        Case{"ABCD", {0x1122, 0x3344}},
        Case{"CDAB", {0x3344, 0x1122}},
        Case{"BADC", {0x2211, 0x4433}},
        Case{"DCBA", {0x4433, 0x2211}},
    };

    for (const auto& test : cases) {
        CAPTURE(test.order);
        Tag tag = make_tag(TagType::UInt32, test.order);
        auto encoded = Translator::encode(tag, std::uint32_t{0x11223344U});
        REQUIRE(encoded);
        REQUIRE(encoded->size() == 2);
        CHECK((*encoded)[0] == test.registers[0]);
        CHECK((*encoded)[1] == test.registers[1]);

        auto decoded = Translator::decode(tag, test.registers);
        REQUIRE(decoded);
        CHECK(std::get<std::uint32_t>(*decoded) == 0x11223344U);
    }
}

TEST_CASE("Translator float32 byte orders match IEEE-754 Modbus words",
          "[unit][core][translator][contract]") {
    struct Case {
        const char* order;
        std::array<std::uint16_t, 2> registers;
    };
    constexpr std::array cases = {
        Case{"ABCD", {0x3F80, 0x0000}},
        Case{"CDAB", {0x0000, 0x3F80}},
        Case{"BADC", {0x803F, 0x0000}},
        Case{"DCBA", {0x0000, 0x803F}},
    };

    for (const auto& test : cases) {
        CAPTURE(test.order);
        Tag tag = make_tag(TagType::Float32, test.order);
        auto encoded = Translator::encode(tag, 1.0F);
        REQUIRE(encoded);
        CHECK(*encoded ==
              std::vector<std::uint16_t>(test.registers.begin(), test.registers.end()));

        auto decoded = Translator::decode(tag, test.registers);
        REQUIRE(decoded);
        CHECK(std::get<float>(*decoded) == Catch::Approx(1.0F));
    }
}

TEST_CASE("Translator default byte orders match the project format contract",
          "[unit][core][translator][contract]") {
    Tag u16 = make_tag(TagType::UInt16, {});
    auto u16_encoded = Translator::encode(u16, std::uint16_t{0x1234});
    REQUIRE(u16_encoded);
    CHECK(*u16_encoded == std::vector<std::uint16_t>{0x1234});

    Tag u32 = make_tag(TagType::UInt32, {});
    auto u32_encoded = Translator::encode(u32, std::uint32_t{0x11223344U});
    REQUIRE(u32_encoded);
    CHECK(*u32_encoded == std::vector<std::uint16_t>{0x1122, 0x3344});

    Tag f64 = make_tag(TagType::Float64, {});
    auto f64_encoded = Translator::encode(f64, 1.0);
    REQUIRE(f64_encoded);
    CHECK(*f64_encoded == std::vector<std::uint16_t>{0x3FF0, 0x0000, 0x0000, 0x0000});
}

TEST_CASE("Translator integer scaling rounds to the nearest raw register",
          "[unit][core][translator][contract]") {
    Tag tag = make_tag(TagType::Int16, "AB", 2.0, 1.0);

    auto positive = Translator::encode(tag, std::int16_t{6});
    REQUIRE(positive);
    CHECK(*positive == std::vector<std::uint16_t>{3});

    auto negative = Translator::encode(tag, std::int16_t{-4});
    REQUIRE(negative);
    CHECK(*negative == std::vector<std::uint16_t>{0xFFFD});
}

TEST_CASE("Translator bool maps nonzero registers to true and emits zero or one",
          "[unit][core][translator][contract]") {
    Tag tag = make_tag(TagType::Bool, {});

    for (const std::uint16_t raw : {std::uint16_t{1}, std::uint16_t{42}, std::uint16_t{0xFFFF}}) {
        CAPTURE(raw);
        const std::array registers{raw};
        auto decoded = Translator::decode(tag, registers);
        REQUIRE(decoded);
        CHECK(std::get<bool>(*decoded));
    }

    const std::array<std::uint16_t, 1> zero{0};
    auto decoded_zero = Translator::decode(tag, zero);
    REQUIRE(decoded_zero);
    CHECK_FALSE(std::get<bool>(*decoded_zero));

    auto encoded_false = Translator::encode(tag, false);
    auto encoded_true = Translator::encode(tag, true);
    REQUIRE(encoded_false);
    REQUIRE(encoded_true);
    CHECK(*encoded_false == std::vector<std::uint16_t>{0});
    CHECK(*encoded_true == std::vector<std::uint16_t>{1});
}
