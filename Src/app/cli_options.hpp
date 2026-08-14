#pragma once

#include <cstddef>
#include <iosfwd>
#include <string>
#include <vector>

namespace opc::app {

enum class LogLevelOption { Trace, Debug, Info, Warn, Error };
enum class MetricsExportOption { None, OStream, OtlpHttp };

struct CliOptions {
    std::string project_path;
    bool once{false};
    bool watch{false};
    int watch_period_ms{1000};
    bool enable_opcua{true};
    bool enable_historian{true};
    std::size_t historian_capacity{4096};
    std::string historian_db;  // empty = hot ring only
    std::string frame_log_path;  // empty = disabled
    LogLevelOption log_level{LogLevelOption::Info};
    std::string log_file;  // empty = stderr only
    MetricsExportOption metrics_export{MetricsExportOption::None};
    MetricsExportOption traces_export{MetricsExportOption::None};
    std::string otlp_endpoint;  // used when metrics or traces export == OtlpHttp
    bool runtime_doctor{false};
    std::string ua_cert_path;
    std::string ua_key_path;
    std::vector<std::string> ua_trust_paths;
    bool ua_strict_certs{false};
    bool help{false};
    bool version{false};
    std::vector<std::string> errors;
};

[[nodiscard]] CliOptions parse_cli(int argc, char const* argv[]);
void print_usage(std::ostream& out);

}  // namespace opc::app
