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

TEST_CASE("demo-plant.modbusproj.json loads and validates", "[contract][project]") {
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
}

TEST_CASE("invalid project reports errors", "[contract][project]") {
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

TEST_CASE("unknown endpointId is an error", "[contract][project]") {
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

TEST_CASE("migrate legacy config.json produces loadable project", "[contract][project][migrate]") {
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
