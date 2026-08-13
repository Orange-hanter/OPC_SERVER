#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_adapters.hpp>

#include "core/translator.hpp"

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
