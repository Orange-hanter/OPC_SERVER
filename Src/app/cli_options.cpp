#include "app/cli_options.hpp"

#include <iostream>
#include <string_view>

namespace opc::app {

void print_usage(std::ostream& out) {
    out << "OPC_SERVER — Modbus→OPC UA industrial gateway\n\n"
        << "Usage:\n"
        << "  OPC_SERVER [--project <file.modbusproj.json>] [--once] [--watch] [--period-ms N]\n"
        << "             [--no-opcua]\n"
        << "  OPC_SERVER --help\n\n"
        << "Options:\n"
        << "  --project <path>   Path to Modbus project map (default: search common paths)\n"
        << "  --once             Run one poll cycle across endpoints and exit\n"
        << "  --watch            Print tag watchlist after each poll\n"
        << "  --period-ms <n>    Loop sleep between polls (default 1000)\n"
        << "  --no-opcua         Disable OPC UA northbound server\n";
}

CliOptions parse_cli(int argc, char const* argv[]) {
    CliOptions opts;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            opts.help = true;
            continue;
        }
        if (arg == "--once") {
            opts.once = true;
            continue;
        }
        if (arg == "--watch") {
            opts.watch = true;
            continue;
        }
        if (arg == "--no-opcua") {
            opts.enable_opcua = false;
            continue;
        }
        if (arg == "--project") {
            if (i + 1 >= argc) {
                opts.errors.emplace_back("--project requires a path");
                break;
            }
            opts.project_path = argv[++i];
            continue;
        }
        if (arg == "--period-ms") {
            if (i + 1 >= argc) {
                opts.errors.emplace_back("--period-ms requires a number");
                break;
            }
            try {
                opts.watch_period_ms = std::stoi(argv[++i]);
            } catch (...) {
                opts.errors.emplace_back("invalid --period-ms value");
            }
            continue;
        }
        opts.errors.emplace_back("unknown argument: " + std::string(arg));
    }
    if (opts.watch_period_ms < 10) {
        opts.errors.emplace_back("--period-ms must be >= 10");
    }
    return opts;
}

}  // namespace opc::app
