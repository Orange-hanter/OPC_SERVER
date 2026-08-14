#include <catch2/catch_test_macros.hpp>

#include "project/load.hpp"
#include "support/repo_root.hpp"

#include <filesystem>

TEST_CASE("shared fixtures: valid maps load", "[contract][project][fixtures]") {
    const auto root = opc_repo_root();
    const auto minimal = root / "tests" / "fixtures" / "valid" / "minimal.modbusproj.json";
    REQUIRE(std::filesystem::exists(minimal));
    auto loaded = opc::project::load_file(minimal.string());
    REQUIRE(loaded.ok);
    CHECK(loaded.project.name == "minimal-valid");
}

TEST_CASE("shared fixtures: missing required fields fail", "[contract][project][fixtures]") {
    const auto path =
        opc_repo_root() / "tests" / "fixtures" / "invalid" / "missing-required.json";
    auto loaded = opc::project::load_file(path.string());
    REQUIRE_FALSE(loaded.ok);
}

TEST_CASE("shared fixtures: unknown endpointId fails semantic validation",
          "[contract][project][fixtures]") {
    const auto path =
        opc_repo_root() / "tests" / "fixtures" / "invalid" / "unknown-endpoint.json";
    auto loaded = opc::project::load_file(path.string());
    REQUIRE_FALSE(loaded.ok);
}

TEST_CASE("shared fixtures: writable input and duplicate names fail semantic validation",
          "[contract][project][fixtures]") {
    const auto root = opc_repo_root();
    auto writable = opc::project::load_file(
        (root / "tests" / "fixtures" / "invalid" / "writable-input.json").string());
    REQUIRE_FALSE(writable.ok);
    bool saw_writable = false;
    for (const auto& d : writable.diagnostics) {
        if (d.message.find("cannot be writable") != std::string::npos) {
            saw_writable = true;
        }
    }
    CHECK(saw_writable);

    auto dup = opc::project::load_file(
        (root / "tests" / "fixtures" / "invalid" / "duplicate-tag-name.json").string());
    REQUIRE_FALSE(dup.ok);
    bool saw_dup = false;
    for (const auto& d : dup.diagnostics) {
        if (d.message.find("across devices") != std::string::npos) {
            saw_dup = true;
        }
    }
    CHECK(saw_dup);
}
