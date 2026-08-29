#pragma once

#include "project/types.hpp"

#include <string>

namespace opc::project {

/// UA NodeSet2 XML fragment: Folders (i=61) + Variables (i=63) from `nodePath`,
/// organized under Objects (i=85). Empty `nodePath` becomes `Plant/<name>`.
[[nodiscard]] std::string generate_nodeset(const Project& project);

}  // namespace opc::project
