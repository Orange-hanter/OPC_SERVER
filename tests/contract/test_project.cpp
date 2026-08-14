#include <catch2/catch_test_macros.hpp>

#include "project/load.hpp"
#include "project/migrate_legacy.hpp"

#include <algorithm>
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

bool has_diagnostic(const opc::project::LoadResult& result,
                    std::string_view path,
                    std::string_view message) {
    return std::ranges::any_of(result.diagnostics, [&](const auto& diagnostic) {
        return diagnostic.path == path && diagnostic.message.find(message) != std::string::npos;
    });
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

TEST_CASE("semantic validation rejects writable input, bad byteOrder and duplicate names",
          "[contract][project]") {
    constexpr std::string_view kJson = R"({
      "schemaVersion": 1,
      "name": "semantic",
      "endpoints": [
        {"id": "ep1", "host": "127.0.0.1", "port": 502, "transport": "tcp"}
      ],
      "devices": [
        {"id": "d1", "endpointId": "ep1", "unitId": 1, "tags": [
          {"name": "InW", "area": "input", "address": 0, "type": "uint16",
           "byteOrder": "AB", "writable": true, "group": "g1"},
          {"name": "Order", "area": "holding", "address": 1, "type": "float32",
           "byteOrder": "AB", "group": "g1"}
        ]},
        {"id": "d2", "endpointId": "ep1", "unitId": 2, "tags": [
          {"name": "InW", "area": "holding", "address": 0, "type": "uint16",
           "byteOrder": "AB", "group": "g2"}
        ]}
      ],
      "pollGroups": [
        {"id": "g1", "periodMs": 5, "priority": "fast", "deviceId": "d1", "tagNames": ["InW"]},
        {"id": "g2", "periodMs": 100, "priority": "normal", "deviceId": "d2", "tagNames": ["InW"]}
      ]
    })";
    const auto result = opc::project::load_json_text(kJson, "semantic.json");
    REQUIRE_FALSE(result.ok);
    bool writable = false;
    bool byte_order = false;
    bool duplicate = false;
    bool period = false;
    for (const auto& d : result.diagnostics) {
        if (d.message.find("cannot be writable") != std::string::npos) {
            writable = true;
        }
        if (d.message.find("invalid byteOrder") != std::string::npos) {
            byte_order = true;
        }
        if (d.message.find("across devices") != std::string::npos) {
            duplicate = true;
        }
        if (d.message.find("periodMs must be >= 10") != std::string::npos) {
            period = true;
        }
    }
    CHECK(writable);
    CHECK(byte_order);
    CHECK(duplicate);
    CHECK(period);
}

TEST_CASE("project validation reports identity, reference and Modbus range violations",
          "[contract][project][validation]") {
    constexpr std::string_view kJson = R"({
      "schemaVersion": 1,
      "name": "bounds",
      "endpoints": [
        {"id": "ep", "host": "127.0.0.1", "port": 502, "transport": "tcp",
         "connectTimeoutMs": 0},
        {"id": "ep", "host": "127.0.0.2", "port": 502, "transport": "tcp"}
      ],
      "devices": [
        {"id": "d", "endpointId": "ep", "unitId": -1, "profileId": "missing", "tags": [
          {"name": "Negative", "area": "holding", "address": -1, "type": "uint16",
           "byteOrder": "AB", "quantity": 0}
        ]},
        {"id": "d", "endpointId": "ep", "unitId": 256, "tags": [
          {"name": "Other", "area": "coil", "address": 0, "type": "bool"}
        ]}
      ],
      "pollGroups": [
        {"id": "g", "periodMs": 100, "priority": "normal", "deviceId": "d",
         "tagNames": ["Absent"],
         "blocks": [
           {"area": "holding", "start": -1, "count": 1},
           {"area": "holding", "start": 0, "count": 126}
         ]},
        {"id": "g", "periodMs": 100, "priority": "normal", "deviceId": "missing",
         "tagNames": ["Anything"]}
      ]
    })";

    const auto result = opc::project::load_json_text(kJson, "bounds.json");
    REQUIRE_FALSE(result.ok);
    CHECK(has_diagnostic(result, "endpoints[0]", "timeouts must be >= 1"));
    CHECK(has_diagnostic(result, "endpoints[1].id", "duplicate endpoint id"));
    CHECK(has_diagnostic(result, "devices[0].unitId", "unitId must be in [0, 255]"));
    CHECK(has_diagnostic(result, "devices[0].profileId", "unknown profileId"));
    CHECK(has_diagnostic(result, "devices[0].tags[0].address", "address must be >= 0"));
    CHECK(has_diagnostic(result, "devices[0].tags[0].quantity", "quantity must be >= 1"));
    CHECK(has_diagnostic(result, "devices[1].id", "duplicate device id"));
    CHECK(has_diagnostic(result, "devices[1].unitId", "unitId must be in [0, 255]"));
    CHECK(has_diagnostic(result, "pollGroups[0].tagNames", "not found on device"));
    CHECK(has_diagnostic(result, "pollGroups[0].blocks[0].start", "start must be >= 0"));
    CHECK(has_diagnostic(result, "pollGroups[0].blocks[1].count", "count must be in [1, 125]"));
    CHECK(has_diagnostic(result, "pollGroups[1].id", "duplicate poll group id"));
    CHECK(has_diagnostic(result, "pollGroups[1].deviceId", "unknown deviceId"));
}

TEST_CASE("project loader distinguishes malformed JSON from wrong root shape",
          "[contract][project][parser]") {
    const auto malformed = opc::project::load_json_text(R"({"name":)", "truncated.json");
    REQUIRE_FALSE(malformed.ok);
    REQUIRE(malformed.diagnostics.size() == 1);
    CHECK(malformed.diagnostics.front().path == "truncated.json");
    CHECK(malformed.diagnostics.front().message.find("JSON parse error") != std::string::npos);

    const auto array_root = opc::project::load_json_text("[]", "array.json");
    REQUIRE_FALSE(array_root.ok);
    CHECK(has_diagnostic(array_root, "$", "root must be a JSON object"));
}

TEST_CASE("project loader accepts Modbus boundary values", "[contract][project][validation]") {
    constexpr std::string_view kJson = R"({
      "schemaVersion": 1,
      "name": "boundary",
      "addressBase": 1,
      "endpoints": [
        {"id": "ep", "host": "127.0.0.1", "port": 65535, "transport": "tcp",
         "connectTimeoutMs": 1, "responseTimeoutMs": 1}
      ],
      "devices": [
        {"id": "low", "endpointId": "ep", "unitId": 0, "tags": [
          {"name": "First", "area": "holding", "address": 0, "type": "uint16",
           "byteOrder": "AB", "quantity": 1}
        ]},
        {"id": "high", "endpointId": "ep", "unitId": 255, "tags": [
          {"name": "Last", "area": "holding", "address": 65535, "type": "uint16",
           "byteOrder": "AB"}
        ]}
      ],
      "pollGroups": [
        {"id": "g-low", "periodMs": 10, "priority": "fast", "deviceId": "low",
         "blocks": [{"area": "holding", "start": 0, "count": 125}]},
        {"id": "g-high", "periodMs": 10, "priority": "fast", "deviceId": "high",
         "tagNames": ["Last"]}
      ]
    })";

    const auto result = opc::project::load_json_text(kJson, "boundary.json");
    for (const auto& diagnostic : result.diagnostics) {
        INFO(diagnostic.path << ": " << diagnostic.message);
    }
    REQUIRE(result.ok);
    CHECK(result.project.address_base == 1);
    CHECK(result.project.endpoints.front().port == 65535);
    CHECK(result.project.devices.front().unit_id == 0);
    CHECK(result.project.devices.back().unit_id == 255);
}

TEST_CASE("unknown tag group is warning-only and preserves a loadable project",
          "[contract][project][validation]") {
    constexpr std::string_view kJson = R"({
      "schemaVersion": 1,
      "name": "warning-only",
      "endpoints": [
        {"id": "ep", "host": "127.0.0.1", "port": 502, "transport": "tcp"}
      ],
      "devices": [
        {"id": "d", "endpointId": "ep", "unitId": 1, "tags": [
          {"name": "A", "area": "holding", "address": 0, "type": "uint16",
           "byteOrder": "AB", "group": "orphan"}
        ]}
      ],
      "pollGroups": [
        {"id": "g", "periodMs": 100, "priority": "normal", "deviceId": "d",
         "tagNames": ["A"]}
      ]
    })";

    const auto result = opc::project::load_json_text(kJson, "warning.json");
    REQUIRE(result.ok);
    REQUIRE(result.diagnostics.size() == 1);
    CHECK(result.diagnostics.front().severity == opc::project::Diagnostic::Severity::Warning);
    CHECK(result.diagnostics.front().path == "devices[0].tags[0].group");
    CHECK(result.diagnostics.front().message.find("has no matching pollGroups.id") !=
          std::string::npos);
}
