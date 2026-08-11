#pragma once

#include <cstddef>
#include <iosfwd>
#include <string>
#include <vector>

namespace opc::app {

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
    bool help{false};
    bool version{false};
    std::vector<std::string> errors;
};

[[nodiscard]] CliOptions parse_cli(int argc, char const* argv[]);
void print_usage(std::ostream& out);

}  // namespace opc::app
