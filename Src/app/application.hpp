#pragma once

#include "app/cli_options.hpp"
#include "app/server_runtime.hpp"
#include "ports/i_frame_log.hpp"
#include "ports/i_historian.hpp"

#include <memory>

namespace opc::app {

class Application {
public:
    Application();
    ~Application();

    bool init(const CliOptions& options);
    int run();

private:
    CliOptions options_;
    std::unique_ptr<ports::ILog> log_;
    std::unique_ptr<ports::IClock> clock_;
    std::unique_ptr<ports::IMetrics> metrics_;
    std::unique_ptr<ports::IHistorian> historian_;
    std::unique_ptr<ports::IFrameLog> frame_log_;
    std::unique_ptr<ServerRuntime> runtime_;
};

}  // namespace opc::app
