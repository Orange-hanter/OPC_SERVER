#pragma once

#include <filesystem>

#ifdef OPC_SERVER_SOURCE_DIR
inline std::filesystem::path opc_repo_root() {
    return std::filesystem::path{OPC_SERVER_SOURCE_DIR};
}
#else
inline std::filesystem::path opc_repo_root() {
    auto p = std::filesystem::current_path();
    for (int i = 0; i < 8; ++i) {
        if (std::filesystem::exists(p / "DOCs" / "examples" / "demo-plant.modbusproj.json")) {
            return p;
        }
        if (!p.has_parent_path() || p.parent_path() == p) {
            break;
        }
        p = p.parent_path();
    }
    return std::filesystem::current_path();
}
#endif
