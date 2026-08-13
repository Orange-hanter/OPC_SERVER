#include "app/cli_options.hpp"

#include <iostream>
#include <string_view>

namespace opc::app {
namespace {

bool parse_log_level(std::string_view text, LogLevelOption& out) {
    if (text == "trace") {
        out = LogLevelOption::Trace;
        return true;
    }
    if (text == "debug") {
        out = LogLevelOption::Debug;
        return true;
    }
    if (text == "info") {
        out = LogLevelOption::Info;
        return true;
    }
    if (text == "warn" || text == "warning") {
        out = LogLevelOption::Warn;
        return true;
    }
    if (text == "error") {
        out = LogLevelOption::Error;
        return true;
    }
    return false;
}

bool parse_metrics_export(std::string_view text, MetricsExportOption& out) {
    if (text == "none") {
        out = MetricsExportOption::None;
        return true;
    }
    if (text == "ostream" || text == "stdout") {
        out = MetricsExportOption::OStream;
        return true;
    }
    if (text == "otlp" || text == "otlp-http") {
        out = MetricsExportOption::OtlpHttp;
        return true;
    }
    return false;
}

}  // namespace

void print_usage(std::ostream& out) {
    out << "OPC_SERVER — Modbus→OPC UA industrial gateway\n\n"
        << "Usage:\n"
        << "  OPC_SERVER [--project <file.modbusproj.json>] [--once] [--watch] [--period-ms N]\n"
        << "             [--no-opcua] [--no-historian] [--historian-capacity N]\n"
        << "             [--historian-db <path.sqlite>] [--frame-log <path>]\n"
        << "             [--log-level LEVEL] [--log-file <path>]\n"
        << "             [--metrics-export none|ostream|otlp] [--otlp-endpoint URL]\n"
        << "             [--runtime-doctor]\n"
        << "  OPC_SERVER --version\n"
        << "  OPC_SERVER --help\n\n"
        << "Options:\n"
        << "  --project <path>            Path to Modbus project map (default: search common paths)\n"
        << "  --once                      Run one poll cycle across endpoints and exit\n"
        << "  --watch                     Print tag watchlist on a timer (see --period-ms)\n"
        << "  --period-ms <n>             Watchlist print interval in --watch mode (default 1000);\n"
        << "                              poll periods come from pollGroups[].periodMs\n"
        << "  --no-opcua                  Disable OPC UA northbound server\n"
        << "  --no-historian              Disable TagStore historian subscription\n"
        << "  --historian-capacity <n>    Hot ring sample capacity (default 4096)\n"
        << "  --historian-db <path>       Cold SQLite path (enables flush of hot samples)\n"
        << "  --frame-log <path>          Append Modbus TX/RX frame journal to file\n"
        << "  --log-level <level>         trace|debug|info|warn|error (default info)\n"
        << "  --log-file <path>           Also write rotating spdlog file sink\n"
        << "  --metrics-export <mode>     none|ostream|otlp (default none)\n"
        << "  --otlp-endpoint <url>       OTLP/HTTP metrics URL (requires -DOPC_WITH_OTLP=ON)\n"
        << "  --runtime-doctor            After start+poll, print TagStore quality findings to stderr;\n"
        << "                              with --once, exit 1 if any tag is missing or not Good\n"
        << "  --version                   Print version and exit\n";
}

CliOptions parse_cli(int argc, char const* argv[]) {
    CliOptions opts;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            opts.help = true;
            continue;
        }
        if (arg == "--version" || arg == "-V") {
            opts.version = true;
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
        if (arg == "--runtime-doctor") {
            opts.runtime_doctor = true;
            continue;
        }
        if (arg == "--no-opcua") {
            opts.enable_opcua = false;
            continue;
        }
        if (arg == "--no-historian") {
            opts.enable_historian = false;
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
        if (arg == "--historian-capacity") {
            if (i + 1 >= argc) {
                opts.errors.emplace_back("--historian-capacity requires a number");
                break;
            }
            try {
                const int n = std::stoi(argv[++i]);
                if (n < 1) {
                    opts.errors.emplace_back("--historian-capacity must be >= 1");
                } else {
                    opts.historian_capacity = static_cast<std::size_t>(n);
                }
            } catch (...) {
                opts.errors.emplace_back("invalid --historian-capacity value");
            }
            continue;
        }
        if (arg == "--historian-db") {
            if (i + 1 >= argc) {
                opts.errors.emplace_back("--historian-db requires a path");
                break;
            }
            opts.historian_db = argv[++i];
            continue;
        }
        if (arg == "--frame-log") {
            if (i + 1 >= argc) {
                opts.errors.emplace_back("--frame-log requires a path");
                break;
            }
            opts.frame_log_path = argv[++i];
            continue;
        }
        if (arg == "--log-level") {
            if (i + 1 >= argc) {
                opts.errors.emplace_back("--log-level requires a value");
                break;
            }
            if (!parse_log_level(argv[++i], opts.log_level)) {
                opts.errors.emplace_back("invalid --log-level (use trace|debug|info|warn|error)");
            }
            continue;
        }
        if (arg == "--log-file") {
            if (i + 1 >= argc) {
                opts.errors.emplace_back("--log-file requires a path");
                break;
            }
            opts.log_file = argv[++i];
            continue;
        }
        if (arg == "--metrics-export") {
            if (i + 1 >= argc) {
                opts.errors.emplace_back("--metrics-export requires a value");
                break;
            }
            if (!parse_metrics_export(argv[++i], opts.metrics_export)) {
                opts.errors.emplace_back("invalid --metrics-export (use none|ostream|otlp)");
            }
            continue;
        }
        if (arg == "--otlp-endpoint") {
            if (i + 1 >= argc) {
                opts.errors.emplace_back("--otlp-endpoint requires a URL");
                break;
            }
            opts.otlp_endpoint = argv[++i];
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
