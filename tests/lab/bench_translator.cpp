#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/translator.hpp"

using opc::core::Translator;
using opc::project::Tag;
using opc::project::TagType;

TEST_CASE("Translator encode/decode benchmark", "[benchmark][translator]") {
    Tag tag;
    tag.type = TagType::Float32;
    tag.byte_order = "CDAB";
    tag.scale = 0.1;
    tag.offset = 1.0;

    BENCHMARK("float32 CDAB roundtrip") {
        auto enc = Translator::encode(tag, 12.5f);
        return Translator::decode(tag, *enc);
    };
}
