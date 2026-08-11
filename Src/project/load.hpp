#pragma once

#include "project/types.hpp"

#include <string>
#include <string_view>

namespace opc::project {

LoadResult load_file(const std::string& path);
LoadResult load_json_text(std::string_view text, std::string_view source_name = "<memory>");

}  // namespace opc::project
