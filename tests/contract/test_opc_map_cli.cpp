#include <catch2/catch_test_macros.hpp>

#include "support/repo_root.hpp"

#include <array>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <sys/wait.h>

namespace {

int run_cmd(const std::string& command) {
    const int status = std::system(command.c_str());
    if (status == -1) {
        return 127;
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return 127;
}

}  // namespace

TEST_CASE("opc-map validate exit codes", "[contract][cli][opc-map]") {
#ifndef OPC_MAP_EXECUTABLE
    SKIP("OPC_MAP_EXECUTABLE is not defined");
#else
    const auto root = opc_repo_root();
    const auto demo = root / "DOCs" / "examples" / "demo-plant.modbusproj.json";
    const auto bad = root / "tests" / "fixtures" / "invalid" / "missing-required.json";
    const std::string bin = OPC_MAP_EXECUTABLE;

    REQUIRE(run_cmd(bin + " validate " + demo.string() + " >/dev/null") == 0);
    REQUIRE(run_cmd(bin + " validate " + bad.string() + " >/dev/null 2>/dev/null") == 1);
    REQUIRE(run_cmd(bin + " >/dev/null 2>/dev/null") == 2);
#endif
}

TEST_CASE("opc-map doctor on demo-plant is warning-only", "[contract][cli][opc-map]") {
#ifndef OPC_MAP_EXECUTABLE
    SKIP("OPC_MAP_EXECUTABLE is not defined");
#else
    const auto demo = opc_repo_root() / "DOCs" / "examples" / "demo-plant.modbusproj.json";
    const std::string bin = OPC_MAP_EXECUTABLE;
    REQUIRE(run_cmd(bin + " doctor " + demo.string() + " >/dev/null") == 0);
#endif
}
