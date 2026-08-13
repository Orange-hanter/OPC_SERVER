#pragma once

#include "project/types.hpp"

#include <json.hpp>
#include <string_view>

namespace opc::project {

/// Validate `instance` against the bundled JSON Schema (draft 2020-12).
/// Schema engine is nlohmann_json_schema_validator (Draft 7) with `$defs` mapped
/// to `definitions`; the subset used by the project schema is compatible.
void append_json_schema_diagnostics(const nlohmann::json& instance,
                                    std::string_view source_name,
                                    std::vector<Diagnostic>& diagnostics);

}  // namespace opc::project
