#include <catch2/catch_test_macros.hpp>

#include "project/import_csv.hpp"
#include "project/load.hpp"

#include <filesystem>
#include <stdexcept>
#include <string_view>

namespace fs = std::filesystem;

TEST_CASE("import-csv produces a loadable draft project", "[project][import-csv]") {
    constexpr std::string_view kCsv =
        "name,area,address,type,byteOrder,scale,offset,unit,writable,nodePath,group,description,quantity\n"
        "Tank1.Level,holding,0,float32,ABCD,1.0,0.0,m,false,Plant/Tank1/Level,tank1-fast,Level,2\n"
        "Tank1.Setpoint,holding,5,uint16,AB,0.1,0.0,m,true,Plant/Tank1/Setpoint,tank1-fast,\"Setpoint, writable\",\n"
        "Pump.Running,coil,0,bool,,1,0,,false,,,\n";

    opc::project::ImportCsvOptions options;
    options.project_name = "from-csv";
    options.device_id = "tank1";
    options.endpoint_id = "plc-line-a";
    options.unit_id = 3;
    options.group = "tank1-fast";

    const auto json = opc::project::import_csv_text(kCsv, options);
    CHECK(json["name"] == "from-csv");
    CHECK(json["devices"][0]["id"] == "tank1");
    CHECK(json["devices"][0]["unitId"] == 3);
    REQUIRE(json["devices"][0]["tags"].size() == 3);
    CHECK(json["devices"][0]["tags"][0]["name"] == "Tank1.Level");
    CHECK(json["devices"][0]["tags"][0]["quantity"] == 2);
    CHECK(json["devices"][0]["tags"][1]["writable"] == true);
    CHECK(json["devices"][0]["tags"][1]["description"] == "Setpoint, writable");
    CHECK(json["devices"][0]["tags"][2]["nodePath"] == "Plant/Pump.Running");
    CHECK(json["devices"][0]["tags"][2]["area"] == "coil");
    CHECK_FALSE(json["devices"][0]["tags"][2].contains("byteOrder"));

    const auto loaded = opc::project::load_json_text(json.dump(), "from-csv.json");
    for (const auto& d : loaded.diagnostics) {
        INFO(d.path << ": " << d.message);
    }
    REQUIRE(loaded.ok);
    CHECK(loaded.project.devices.size() == 1);
    CHECK(loaded.project.devices.front().tags.size() == 3);
    CHECK(loaded.project.poll_groups.front().tag_names.size() == 3);
}

TEST_CASE("import-csv rejects missing header columns", "[project][import-csv]") {
    CHECK_THROWS_AS(opc::project::import_csv_text("name,area\nA,holding\n"), std::runtime_error);
}

TEST_CASE("import-csv skips comments and blank lines", "[project][import-csv]") {
    constexpr std::string_view kCsv =
        "# vendor dump\n"
        "\n"
        "name,area,address,type,byteOrder,scale,offset,unit,writable\n"
        "# skip me\n"
        "A,holding,0,uint16,AB,1,0,,false\n";
    const auto json = opc::project::import_csv_text(kCsv);
    REQUIRE(json["devices"][0]["tags"].size() == 1);
    CHECK(json["devices"][0]["tags"][0]["name"] == "A");
}

TEST_CASE("import-csv example tank-registers.csv is loadable", "[project][import-csv]") {
#ifdef OPC_SERVER_SOURCE_DIR
    const auto path = fs::path{OPC_SERVER_SOURCE_DIR} / "DOCs" / "examples" / "tank-registers.csv";
#else
    const auto path = fs::current_path() / "DOCs" / "examples" / "tank-registers.csv";
#endif
    REQUIRE(fs::exists(path));
    opc::project::ImportCsvOptions options;
    options.device_id = "tank1";
    options.group = "tank1-fast";
    const std::string text = opc::project::import_csv_file_to_string(path.string(), options);
    const auto loaded = opc::project::load_json_text(text, "tank-registers.json");
    for (const auto& d : loaded.diagnostics) {
        INFO(d.path << ": " << d.message);
    }
    REQUIRE(loaded.ok);
    CHECK(loaded.project.devices.front().tags.size() == 4);
}
