#include "app.h"

#include "project/load.hpp"

#include <iostream>

App::App() = default;
App::~App() = default;

bool App::init() {
    // Prefer new project format; fall back to legacy config.json for the stub.
    const char* candidates[] = {
        "project.modbusproj.json",
        "DOCs/examples/demo-plant.modbusproj.json",
        "config.json",
        "DOCs/config.json",
    };

    for (const char* path : candidates) {
        auto result = opc::project::load_file(path);
        if (result.diagnostics.size() == 1 &&
            result.diagnostics.front().message.find("cannot open") != std::string::npos) {
            continue;
        }
        for (const auto& d : result.diagnostics) {
            const char* level =
                d.severity == opc::project::Diagnostic::Severity::Error ? "error" : "warning";
            std::cerr << level << ": " << d.path << ": " << d.message << '\n';
        }
        if (!result.ok) {
            // Legacy config.json is not a modbusproj — try next / allow startup for stub.
            if (std::string(path).find("config.json") != std::string::npos) {
                std::cerr << "note: legacy " << path
                          << " detected; use `opc-map migrate-legacy` (stage 1 tooling)\n";
                continue;
            }
            return false;
        }
        std::cout << "loaded project '" << result.project.name << "' from " << path << " with "
                  << result.project.devices.size() << " devices\n";
        return true;
    }

    std::cerr << "no project file found (looked for *.modbusproj.json)\n";
    return false;
}

void App::start() {
    // Stage 2 will replace this with poller + UA server loop.
    std::cout << "OPC_SERVER stub running (stage 1: project load only). Ctrl+C to exit.\n";
    while (true) {
    }
}
