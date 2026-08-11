#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/translator.hpp"

using opc::core::Translator;
using opc::project::Tag;
using opc::project::TagType;

TEST_CASE("Translator float32 ABCD roundtrip", "[core][translator]") {
    Tag tag;
    tag.type = TagType::Float32;
    tag.byte_order = "ABCD";
    tag.scale = 1.0;
    tag.offset = 0.0;

    const float original = 12.5f;
    auto encoded = Translator::encode(tag, original);
    REQUIRE(encoded.has_value());
    REQUIRE(encoded->size() == 2);

    auto decoded = Translator::decode(tag, *encoded);
    REQUIRE(decoded.has_value());
    REQUIRE(std::holds_alternative<float>(*decoded));
    REQUIRE(std::get<float>(*decoded) == Catch::Approx(original));
}

TEST_CASE("Translator float32 CDAB differs from ABCD", "[core][translator]") {
    Tag abcd;
    abcd.type = TagType::Float32;
    abcd.byte_order = "ABCD";
    Tag cdab = abcd;
    cdab.byte_order = "CDAB";

    auto enc = Translator::encode(abcd, 1.0f);
    REQUIRE(enc.has_value());
    auto as_abcd = Translator::decode(abcd, *enc);
    auto as_cdab = Translator::decode(cdab, *enc);
    REQUIRE(as_abcd.has_value());
    REQUIRE(as_cdab.has_value());
    REQUIRE(std::get<float>(*as_abcd) == Catch::Approx(1.0f));
    REQUIRE(std::get<float>(*as_cdab) != Catch::Approx(1.0f));
}

TEST_CASE("Translator uint16 BA byte swap", "[core][translator]") {
    Tag tag;
    tag.type = TagType::UInt16;
    tag.byte_order = "BA";
    tag.scale = 1.0;
    tag.offset = 0.0;

    std::uint16_t regs[] = {0x0100};
    auto decoded = Translator::decode(tag, regs);
    REQUIRE(decoded.has_value());
    REQUIRE(std::get<std::uint16_t>(*decoded) == 1);
}

TEST_CASE("Translator encode/decode uint16", "[core][translator]") {
    Tag tag;
    tag.type = TagType::UInt16;
    tag.byte_order = "AB";
    tag.scale = 1.0;
    auto enc = Translator::encode(tag, std::uint16_t{25});
    REQUIRE(enc.has_value());
    REQUIRE((*enc)[0] == 25);
    auto dec = Translator::decode(tag, *enc);
    REQUIRE(std::get<std::uint16_t>(*dec) == 25);
}
