#include <catch2/catch_test_macros.hpp>

#include "project/doctor.hpp"
#include "project/load.hpp"

TEST_CASE("doctor flags unpolled tags and register overlap", "[project][doctor]") {
    constexpr std::string_view kJson = R"({
      "schemaVersion": 1,
      "name": "doc",
      "endpoints": [
        {"id": "ep1", "host": "127.0.0.1", "port": 502, "transport": "tcp"}
      ],
      "devices": [
        {"id": "d1", "endpointId": "ep1", "unitId": 1, "tags": [
          {"name": "A", "area": "holding", "address": 0, "type": "float32",
           "byteOrder": "ABCD", "group": "g1"},
          {"name": "B", "area": "holding", "address": 1, "type": "uint16",
           "byteOrder": "AB"},
          {"name": "Orphan", "area": "holding", "address": 10, "type": "uint16",
           "byteOrder": "AB"}
        ]}
      ],
      "pollGroups": [
        {"id": "g1", "periodMs": 100, "priority": "fast", "deviceId": "d1", "tagNames": ["A"]}
      ]
    })";
    auto loaded = opc::project::load_json_text(kJson, "doc.json");
    REQUIRE(loaded.ok);
    auto report = opc::project::doctor(loaded.project);
    REQUIRE(report.error_count == 0);
    REQUIRE(report.warning_count >= 2);

    bool saw_orphan = false;
    bool saw_overlap = false;
    for (const auto& d : report.findings) {
        if (d.message.find("Orphan") != std::string::npos) {
            saw_orphan = true;
        }
        if (d.message.find("overlap") != std::string::npos) {
            saw_overlap = true;
        }
    }
    CHECK(saw_orphan);
    CHECK(saw_overlap);
}

TEST_CASE("doctor flags sparse poll blocks", "[project][doctor]") {
    constexpr std::string_view kJson = R"({
      "schemaVersion": 1,
      "name": "sparse",
      "endpoints": [
        {"id": "ep1", "host": "127.0.0.1", "port": 502, "transport": "tcp"}
      ],
      "devices": [
        {"id": "d1", "endpointId": "ep1", "unitId": 1, "tags": [
          {"name": "A", "area": "holding", "address": 0, "type": "uint16",
           "byteOrder": "AB", "group": "g1"}
        ]}
      ],
      "pollGroups": [
        {"id": "g1", "periodMs": 200, "priority": "normal", "deviceId": "d1",
         "tagNames": ["A"],
         "blocks": [{"area": "holding", "start": 0, "count": 20}]}
      ]
    })";
    auto loaded = opc::project::load_json_text(kJson, "sparse.json");
    REQUIRE(loaded.ok);
    auto report = opc::project::doctor(loaded.project);
    bool saw_sparse = false;
    for (const auto& d : report.findings) {
        if (d.message.find("sparse block") != std::string::npos) {
            saw_sparse = true;
        }
    }
    CHECK(saw_sparse);
}
