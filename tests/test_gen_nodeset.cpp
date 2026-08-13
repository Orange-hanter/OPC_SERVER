#include <catch2/catch_test_macros.hpp>

#include "project/gen_nodeset.hpp"
#include "project/load.hpp"

#include <filesystem>
#include <string>

namespace fs = std::filesystem;

TEST_CASE("gen-nodeset emits FolderType and BaseDataVariableType from nodePath", "[project][gen-nodeset]") {
#ifdef OPC_SERVER_SOURCE_DIR
    const auto path = fs::path{OPC_SERVER_SOURCE_DIR} / "DOCs" / "examples" / "demo-plant.modbusproj.json";
#else
    const auto path = fs::current_path() / "DOCs" / "examples" / "demo-plant.modbusproj.json";
#endif
    REQUIRE(fs::exists(path));
    const auto loaded = opc::project::load_file(path.string());
    REQUIRE(loaded.ok);

    const std::string xml = opc::project::generate_nodeset(loaded.project);
    CHECK(xml.find("i=85") != std::string::npos);
    CHECK(xml.find("HasTypeDefinition\">i=61") != std::string::npos);
    CHECK(xml.find("HasTypeDefinition\">i=63") != std::string::npos);
    CHECK(xml.find("FolderType\">i=61") != std::string::npos);
    CHECK(xml.find("BaseDataVariableType\">i=63") != std::string::npos);
    CHECK(xml.find("ns=1;s=Plant/Tank1/Level") != std::string::npos);
    CHECK(xml.find("ns=1;s=Plant/Pump1/Vfd") != std::string::npos);
    CHECK(xml.find("DataType=\"Float\"") != std::string::npos);
    CHECK(xml.find("DataType=\"UInt16\"") != std::string::npos);
    CHECK(xml.find("DataType=\"Boolean\"") != std::string::npos);
    CHECK(xml.find("AccessLevel=\"3\"") != std::string::npos);  // writable Setpoint
    CHECK(xml.find("AccessLevel=\"1\"") != std::string::npos);
    CHECK(xml.find("urn:opc-server:demo-plant") != std::string::npos);
}

TEST_CASE("gen-nodeset defaults empty nodePath to Plant/<name>", "[project][gen-nodeset]") {
    constexpr std::string_view kJson = R"({
      "schemaVersion": 1,
      "name": "ns",
      "endpoints": [
        {"id": "ep1", "host": "127.0.0.1", "port": 502, "transport": "tcp"}
      ],
      "devices": [
        {"id": "d1", "endpointId": "ep1", "unitId": 1, "tags": [
          {"name": "Temp", "area": "holding", "address": 0, "type": "float32",
           "byteOrder": "ABCD", "group": "g1"}
        ]}
      ],
      "pollGroups": [
        {"id": "g1", "periodMs": 100, "priority": "fast", "deviceId": "d1", "tagNames": ["Temp"]}
      ]
    })";
    const auto loaded = opc::project::load_json_text(kJson, "ns.json");
    REQUIRE(loaded.ok);
    const std::string xml = opc::project::generate_nodeset(loaded.project);
    CHECK(xml.find("ns=1;s=Plant/Temp") != std::string::npos);
    CHECK(xml.find("ns=1;s=Plant\"") != std::string::npos);
}
