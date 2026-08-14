#include <catch2/catch_test_macros.hpp>

#include "core/runtime_index.hpp"
#include "project/load.hpp"

using opc::core::RuntimeIndex;

namespace {

std::shared_ptr<const opc::project::Project> two_device_project() {
    constexpr std::string_view kJson = R"({
      "schemaVersion": 1,
      "name": "idx",
      "endpoints": [
        {"id": "ep-a", "host": "127.0.0.1", "port": 502, "transport": "tcp"},
        {"id": "ep-b", "host": "127.0.0.1", "port": 503, "transport": "tcp"}
      ],
      "devices": [
        {"id": "da", "endpointId": "ep-a", "unitId": 1, "tags": [
          {"name": "A", "area": "holding", "address": 0, "type": "uint16",
           "byteOrder": "AB", "group": "ga"}
        ]},
        {"id": "db", "endpointId": "ep-b", "unitId": 7, "tags": [
          {"name": "B", "area": "input", "address": 4, "type": "int16",
           "byteOrder": "BA", "group": "gb"}
        ]}
      ],
      "pollGroups": [
        {"id": "ga", "periodMs": 100, "priority": "fast", "deviceId": "da", "tagNames": ["A"]},
        {"id": "gb", "periodMs": 200, "priority": "normal", "deviceId": "db", "tagNames": ["B"]}
      ]
    })";
    auto loaded = opc::project::load_json_text(kJson, "idx.json");
    REQUIRE(loaded.ok);
    return std::make_shared<opc::project::Project>(std::move(loaded.project));
}

}  // namespace

TEST_CASE("RuntimeIndex maps tags, endpoints and poll groups", "[unit][core][index]") {
    auto project = two_device_project();
    auto index = RuntimeIndex::build(project);

    REQUIRE(index.tags().size() == 2);

    auto a = index.find_by_name("A");
    REQUIRE(a);
    CHECK(a->device_id == "da");
    CHECK(a->endpoint_id == "ep-a");
    CHECK(a->unit_id == 1);
    CHECK(index.find_by_id(a->id)->tag.name == "A");

    auto b = index.find_by_name("B");
    REQUIRE(b);
    CHECK(b->unit_id == 7);
    CHECK(b->tag.area == opc::project::Area::Input);

    REQUIRE_FALSE(index.find_by_name("missing"));
    REQUIRE_FALSE(index.find_by_id(0));
    REQUIRE_FALSE(index.find_by_id(99));

    REQUIRE(index.endpoint("ep-a") != nullptr);
    REQUIRE(index.endpoint("nope") == nullptr);
    REQUIRE(index.device("db") != nullptr);
    REQUIRE(index.device("nope") == nullptr);

    auto groups_a = index.groups_for_endpoint("ep-a");
    REQUIRE(groups_a.size() == 1);
    CHECK(groups_a[0]->id == "ga");
    auto groups_b = index.groups_for_endpoint("ep-b");
    REQUIRE(groups_b.size() == 1);
    CHECK(groups_b[0]->period_ms == 200);
    CHECK(index.groups_for_endpoint("ep-x").empty());
}
