#include "project/load.hpp"
#include "project/migrate_legacy.hpp"
#include "project/doctor.hpp"
#include "project/import_csv.hpp"
#include "project/gen_nodeset.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void print_usage() {
    std::cerr
        << "opc-map — tooling for Modbus project maps\n\n"
        << "Usage:\n"
        << "  opc-map validate <project.modbusproj.json>\n"
        << "  opc-map doctor <project.modbusproj.json>\n"
        << "  opc-map migrate-legacy <config.json> [-o out.modbusproj.json]\n"
        << "  opc-map import-csv <map.csv> [-o fragment.json]\n"
        << "           [--device-id ID] [--endpoint-id ID] [--unit-id N] [--group ID]\n"
        << "  opc-map gen-nodeset <project.modbusproj.json> [-o out.xml]\n"
        << "  opc-map help\n\n"
        << "Exit codes: 0 ok, 1 validation/doctor errors, 2 I/O or usage error\n";
}

int write_text(const std::string& path, const std::string& text) {
    std::ofstream out(path);
    if (!out) {
        std::cerr << "cannot write: " << path << '\n';
        return 2;
    }
    out << text;
    if (!text.empty() && text.back() != '\n') {
        out << '\n';
    }
    std::cout << "wrote " << path << '\n';
    return 0;
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

int cmd_doctor(const std::string& path) {
    const auto loaded = opc::project::load_file(path);
    for (const auto& d : loaded.diagnostics) {
        const char* level = d.severity == opc::project::Diagnostic::Severity::Error ? "error" : "warning";
        std::cerr << level << ": " << d.path << ": " << d.message << '\n';
    }
    if (!loaded.ok) {
        std::cerr << "doctor: project failed validation: " << path << '\n';
        return 1;
    }
    const auto report = opc::project::doctor(loaded.project);
    for (const auto& d : report.findings) {
        const char* level = d.severity == opc::project::Diagnostic::Severity::Error ? "error" : "warning";
        std::cout << level << ": " << d.path << ": " << d.message << '\n';
    }
    std::cout << "doctor: " << report.error_count << " error(s), " << report.warning_count
              << " warning(s)\n";
    return report.error_count > 0 ? 1 : 0;
}

int cmd_migrate_legacy(const std::string& legacy_path, const std::string& out_path) {
    try {
        const std::string text = opc::project::migrate_legacy_file_to_string(legacy_path);
        if (out_path.empty()) {
            std::cout << text << '\n';
        } else if (const int rc = write_text(out_path, text); rc != 0) {
            return rc;
        }

        const auto result = opc::project::load_json_text(text, out_path.empty() ? legacy_path : out_path);
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

int cmd_import_csv(const std::string& csv_path,
                   const std::string& out_path,
                   const opc::project::ImportCsvOptions& options) {
    try {
        const std::string text = opc::project::import_csv_file_to_string(csv_path, options);
        if (out_path.empty()) {
            std::cout << text << '\n';
        } else if (const int rc = write_text(out_path, text); rc != 0) {
            return rc;
        }

        const auto result = opc::project::load_json_text(text, out_path.empty() ? csv_path : out_path);
        for (const auto& d : result.diagnostics) {
            const char* level =
                d.severity == opc::project::Diagnostic::Severity::Error ? "error" : "warning";
            std::cerr << level << ": " << d.path << ": " << d.message << '\n';
        }
        return result.ok ? 0 : 1;
    } catch (const std::exception& ex) {
        std::cerr << "import-csv failed: " << ex.what() << '\n';
        return 2;
    }
}

int cmd_gen_nodeset(const std::string& project_path, const std::string& out_path) {
    const auto loaded = opc::project::load_file(project_path);
    for (const auto& d : loaded.diagnostics) {
        const char* level = d.severity == opc::project::Diagnostic::Severity::Error ? "error" : "warning";
        std::cerr << level << ": " << d.path << ": " << d.message << '\n';
    }
    if (!loaded.ok) {
        std::cerr << "gen-nodeset: project failed validation: " << project_path << '\n';
        return 1;
    }
    const std::string xml = opc::project::generate_nodeset(loaded.project);
    if (out_path.empty()) {
        std::cout << xml;
        return 0;
    }
    return write_text(out_path, xml);
}

bool take_value(int& i, int argc, char** argv, std::string& out, std::string_view flag) {
    if (i + 1 >= argc) {
        std::cerr << flag << " requires a value\n";
        return false;
    }
    out = argv[++i];
    return true;
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
    if (cmd == "doctor") {
        if (argc != 3) {
            print_usage();
            return 2;
        }
        return cmd_doctor(argv[2]);
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
    if (cmd == "import-csv") {
        if (argc < 3) {
            print_usage();
            return 2;
        }
        const std::string csv_path = argv[2];
        std::string out_path;
        opc::project::ImportCsvOptions options;
        for (int i = 3; i < argc; ++i) {
            const std::string_view arg = argv[i];
            std::string value;
            if (arg == "-o") {
                if (!take_value(i, argc, argv, out_path, "-o")) {
                    return 2;
                }
            } else if (arg == "--device-id") {
                if (!take_value(i, argc, argv, options.device_id, "--device-id")) {
                    return 2;
                }
            } else if (arg == "--endpoint-id") {
                if (!take_value(i, argc, argv, options.endpoint_id, "--endpoint-id")) {
                    return 2;
                }
            } else if (arg == "--group") {
                if (!take_value(i, argc, argv, options.group, "--group")) {
                    return 2;
                }
            } else if (arg == "--unit-id") {
                if (!take_value(i, argc, argv, value, "--unit-id")) {
                    return 2;
                }
                try {
                    options.unit_id = std::stoi(value);
                } catch (...) {
                    std::cerr << "invalid --unit-id\n";
                    return 2;
                }
            } else {
                std::cerr << "unknown argument: " << arg << '\n';
                print_usage();
                return 2;
            }
        }
        return cmd_import_csv(csv_path, out_path, options);
    }
    if (cmd == "gen-nodeset") {
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
        return cmd_gen_nodeset(argv[2], out_path);
    }

    std::cerr << "unknown command: " << cmd << '\n';
    print_usage();
    return 2;
}
