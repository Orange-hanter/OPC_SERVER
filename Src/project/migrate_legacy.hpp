#pragma once

#include <json.hpp>

#include <string>

namespace opc::project {

/// Convert legacy DOCs/config.json "Nodes" format into a draft *.modbusproj.json object.
nlohmann::json migrate_legacy_nodes(const nlohmann::json& legacy);

/// Convenience: read legacy file and write migrated project JSON text.
std::string migrate_legacy_file_to_string(const std::string& legacy_path);

}  // namespace opc::project
