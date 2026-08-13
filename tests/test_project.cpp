#include <catch2/catch_test_macros.hpp>

#include "project/load.hpp"
#include "project/migrate_legacy.hpp"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {

fs::path repo_root() {
    // tests run from build dir; walk up looking for DOCs/examples
    fs::path p = fs::current_path();
    for (int i = 0; i < 6; ++i) {
        if (fs::exists(p / "DOCs" / "examples" / "demo-plant.modbusproj.json")) {
            return p;
        }
        if (!p.has_parent_path() || p.parent_path() == p) {
            break;
        }
        p = p.parent_path();
    }
    // Fallback: relative to source via compile definition
#ifdef OPC_SERVER_SOURCE_DIR
    return fs::path{OPC_SERVER_SOURCE_DIR};
#else
    return fs::current_path();
#endif
}

}  // namespace

TEST_CASE("demo-plant.modbusproj.json loads and validates", "[project]") {
    const auto path = repo_root() / "DOCs" / "examples" / "demo-plant.modbusproj.json";
    REQUIRE(fs::exists(path));

    const auto result = opc::project::load_file(path.string());
    for (const auto& d : result.diagnostics) {
        INFO(d.path << ": " << d.message);
    }
    REQUIRE(result.ok);
    CHECK(result.project.name == "demo-plant");
    CHECK(result.project.endpoints.size() == 2);
    CHECK(result.project.devices.size() == 2);
    CHECK(result.project.poll_groups.size() == 3);

    const auto& tank = result.project.devices.front();
    CHECK(tank.id == "tank1");
    REQUIRE_FALSE(tank.tags.empty());
    CHECK(tank.tags.front().name == "Tank1.Level");
    CHECK(tank.tags.front().type == opc::project::TagType::Float32);
    REQUIRE(result.project.device_profiles.size() == 1);
    CHECK(result.project.device_profiles.front().tags.size() == 3);
    CHECK(tank.tags.size() == 4);
}

TEST_CASE("deviceProfiles expand onto devices at load (instance wins on name)", "[project][profiles]") {
    constexpr std::string_view kJson = R"({
      "schemaVersion": 1,
      "name": "profiles",
      "endpoints": [
        {"id": "ep1", "host": "127.0.0.1", "port": 502, "transport": "tcp"}
      ],
      "deviceProfiles": [
        {
          "id": "sensor",
          "name": "Sensor",
          "tags": [
            {"name": "Level", "nodePath": "Plant/Level", "area": "holding", "address": 0,
             "type": "float32", "byteOrder": "ABCD", "group": "g1"},
            {"name": "Temp", "nodePath": "Plant/Temp", "area": "holding", "address": 2,
             "type": "float32", "byteOrder": "ABCD", "group": "g1"}
          ]
        }
      ],
      "devices": [
        {
          "id": "d1",
          "endpointId": "ep1",
          "unitId": 1,
          "profileId": "sensor",
          "tags": [
            {"name": "Temp", "nodePath": "Plant/Tank1/Temp", "area": "holding", "address": 20,
             "type": "float32", "byteOrder": "CDAB", "group": "g1"},
            {"name": "Setpoint", "nodePath": "Plant/Setpoint", "area": "holding", "address": 5,
             "type": "uint16", "byteOrder": "AB", "writable": true, "group": "g1"}
          ]
        }
      ],
      "pollGroups": [
        {"id": "g1", "periodMs": 100, "priority": "fast", "deviceId": "d1",
         "tagNames": ["Level", "Temp", "Setpoint"]}
      ]
    })";
    const auto result = opc::project::load_json_text(kJson, "profiles.json");
    for (const auto& d : result.diagnostics) {
        INFO(d.path << ": " << d.message);
    }
    REQUIRE(result.ok);
    REQUIRE(result.project.devices.size() == 1);
    const auto& tags = result.project.devices.front().tags;
    REQUIRE(tags.size() == 3);
    CHECK(tags[0].name == "Level");
    CHECK(tags[0].address == 0);
    CHECK(tags[1].name == "Temp");
    CHECK(tags[1].address == 20);
    CHECK(tags[1].byte_order == "CDAB");
    CHECK(tags[2].name == "Setpoint");
    CHECK(tags[2].writable);
}

TEST_CASE("device with empty tags inherits profile tags", "[project][profiles]") {
    constexpr std::string_view kJson = R"({
      "schemaVersion": 1,
      "name": "inherit",
      "endpoints": [
        {"id": "ep1", "host": "127.0.0.1", "port": 502, "transport": "tcp"}
      ],
      "deviceProfiles": [
        {
          "id": "sensor",
          "name": "Sensor",
          "tags": [
            {"name": "Level", "area": "holding", "address": 0, "type": "uint16",
             "byteOrder": "AB", "group": "g1"}
          ]
        }
      ],
      "devices": [
        {"id": "d1", "endpointId": "ep1", "unitId": 7, "profileId": "sensor"}
      ],
      "pollGroups": [
        {"id": "g1", "periodMs": 100, "priority": "fast", "deviceId": "d1", "tagNames": ["Level"]}
      ]
    })";
    const auto result = opc::project::load_json_text(kJson, "inherit.json");
    for (const auto& d : result.diagnostics) {
        INFO(d.path << ": " << d.message);
    }
    REQUIRE(result.ok);
    REQUIRE(result.project.devices.front().tags.size() == 1);
    CHECK(result.project.devices.front().tags.front().name == "Level");
    CHECK(result.project.devices.front().unit_id == 7);
}

TEST_CASE("invalid project reports errors", "[project]") {
    constexpr std::string_view kBad = R"({
      "schemaVersion": 1,
      "name": "bad",
      "endpoints": [],
      "devices": [],
      "pollGroups": []
    })";
    const auto result = opc::project::load_json_text(kBad, "bad.json");
    REQUIRE_FALSE(result.ok);
    REQUIRE_FALSE(result.diagnostics.empty());
}

TEST_CASE("unknown endpointId is an error", "[project]") {
    constexpr std::string_view kJson = R"({
      "schemaVersion": 1,
      "name": "x",
      "endpoints": [
        {"id": "ep1", "host": "127.0.0.1", "port": 502, "transport": "tcp"}
      ],
      "devices": [
        {"id": "d1", "endpointId": "missing", "unitId": 1, "tags": [
          {"name": "A", "area": "holding", "address": 0, "type": "uint16", "byteOrder": "AB", "group": "g1"}
        ]}
      ],
      "pollGroups": [
        {"id": "g1", "periodMs": 100, "priority": "fast", "deviceId": "d1",
         "blocks": [{"area": "holding", "start": 0, "count": 1}]}
      ]
    })";
    const auto result = opc::project::load_json_text(kJson, "xref.json");
    REQUIRE_FALSE(result.ok);
    bool found = false;
    for (const auto& d : result.diagnostics) {
        if (d.message.find("unknown endpointId") != std::string::npos) {
            found = true;
        }
    }
    CHECK(found);
}

TEST_CASE("migrate legacy config.json produces loadable project", "[migrate]") {
    const auto legacy_path = repo_root() / "DOCs" / "config.json";
    REQUIRE(fs::exists(legacy_path));

    const std::string migrated = opc::project::migrate_legacy_file_to_string(legacy_path.string());
    const auto result = opc::project::load_json_text(migrated, "migrated.json");
    for (const auto& d : result.diagnostics) {
        INFO(d.path << ": " << d.message);
    }
    REQUIRE(result.ok);
    CHECK(result.project.devices.size() == 2);
    CHECK(result.project.poll_groups.size() == 2);
    CHECK(result.project.endpoints.size() == 1);
}

TEST_CASE("JSON Schema engine rejects extra properties and empty required arrays", "[project][schema]") {
    constexpr std::string_view kExtra = R"({
      "schemaVersion": 1,
      "name": "extra",
      "endpoints": [
        {"id": "ep1", "host": "127.0.0.1", "port": 502, "transport": "tcp"}
      ],
      "devices": [
        {"id": "d1", "endpointId": "ep1", "unitId": 1, "tags": [
          {"name": "A", "area": "holding", "address": 0, "type": "uint16", "group": "g1"}
        ]}
      ],
      "pollGroups": [
        {"id": "g1", "periodMs": 100, "priority": "fast", "deviceId": "d1", "tagNames": ["A"]}
      ],
      "notInSchema": true
    })";
    const auto extra = opc::project::load_json_text(kExtra, "extra.json");
    REQUIRE_FALSE(extra.ok);
    bool schema_hit = false;
    for (const auto& d : extra.diagnostics) {
        if (d.message.find("json schema") != std::string::npos) {
            schema_hit = true;
        }
    }
    CHECK(schema_hit);

    constexpr std::string_view kEmpty = R"({
      "schemaVersion": 1,
      "name": "empty",
      "endpoints": [],
      "devices": [],
      "pollGroups": []
    })";
    const auto empty = opc::project::load_json_text(kEmpty, "empty.json");
    REQUIRE_FALSE(empty.ok);
    bool min_items = false;
    for (const auto& d : empty.diagnostics) {
        if (d.message.find("json schema") != std::string::npos) {
            min_items = true;
        }
    }
    CHECK(min_items);
}
