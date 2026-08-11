#include "project/load.hpp"
#include "project/migrate_legacy.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

namespace {

void print_usage() {
    std::cerr
        << "opc-map — tooling for Modbus project maps\n\n"
        << "Usage:\n"
        << "  opc-map validate <project.modbusproj.json>\n"
        << "  opc-map migrate-legacy <config.json> [-o out.modbusproj.json]\n"
        << "  opc-map help\n\n"
        << "Exit codes: 0 ok, 1 validation errors, 2 I/O or usage error\n";
}

int cmd_validate(const std::string& path) {
    const auto result = opc::project::load_file(path);
    for (const auto& d : result.diagnostics) {
        const char* level = d.severity == opc::project::Diagnostic::Severity::Error ? "error" : "warning";
        std::cerr << level << ": " << d.path << ": " << d.message << '\n';
    }
    if (!result.ok) {
        std::cerr << "validate failed: " << path << '\n';
        return 1;
    }
    std::cout << "OK: " << path << " (" << result.project.devices.size() << " devices, "
              << result.project.poll_groups.size() << " poll groups)\n";
    return 0;
}

int cmd_migrate_legacy(const std::string& legacy_path, const std::string& out_path) {
    try {
        const std::string text = opc::project::migrate_legacy_file_to_string(legacy_path);
        if (out_path.empty()) {
            std::cout << text << '\n';
            return 0;
        }
        std::ofstream out(out_path);
        if (!out) {
            std::cerr << "cannot write: " << out_path << '\n';
            return 2;
        }
        out << text << '\n';
        std::cout << "wrote " << out_path << '\n';

        // Validate draft; endpoint host is placeholder — still should parse.
        const auto result = opc::project::load_json_text(text, out_path);
        for (const auto& d : result.diagnostics) {
            const char* level =
                d.severity == opc::project::Diagnostic::Severity::Error ? "error" : "warning";
            std::cerr << level << ": " << d.path << ": " << d.message << '\n';
        }
        return result.ok ? 0 : 1;
    } catch (const std::exception& ex) {
        std::cerr << "migrate-legacy failed: " << ex.what() << '\n';
        return 2;
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage();
        return 2;
    }
    const std::string_view cmd = argv[1];
    if (cmd == "help" || cmd == "-h" || cmd == "--help") {
        print_usage();
        return 0;
    }
    if (cmd == "validate") {
        if (argc != 3) {
            print_usage();
            return 2;
        }
        return cmd_validate(argv[2]);
    }
    if (cmd == "migrate-legacy") {
        if (argc < 3) {
            print_usage();
            return 2;
        }
        std::string out_path;
        if (argc >= 5 && std::string_view(argv[3]) == "-o") {
            out_path = argv[4];
        } else if (argc != 3) {
            print_usage();
            return 2;
        }
        return cmd_migrate_legacy(argv[2], out_path);
    }

    std::cerr << "unknown command: " << cmd << '\n';
    print_usage();
    return 2;
}
