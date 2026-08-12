#pragma once

#include "project/types.hpp"

namespace opc::project {

struct DoctorReport {
    std::vector<Diagnostic> findings;
    int warning_count{0};
    int error_count{0};
};

/// Static map diagnostics beyond `validate()`: overlaps, holes, unpolled tags, sparse blocks.
[[nodiscard]] DoctorReport doctor(const Project& project);

}  // namespace opc::project
