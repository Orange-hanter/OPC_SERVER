#pragma once

#include "project/types.hpp"

namespace opc::project {

/// Semantic validation (required fields, enums, cross-references).
void validate(Project& project, std::vector<Diagnostic>& diagnostics);

}  // namespace opc::project
