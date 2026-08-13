#pragma once

#include "core/runtime_index.hpp"
#include "ports/i_tag_store.hpp"

#include <string>
#include <vector>

namespace opc::core {

struct RuntimeDoctorFinding {
    enum class Severity { Error, Warning } severity{Severity::Error};
    std::string tag_name;
    std::string message;
};

struct RuntimeDoctorReport {
    std::vector<RuntimeDoctorFinding> findings;
    int warning_count{0};
    int error_count{0};
};

/// Snapshot TagStore against the project index: missing tags, never-Good quality,
/// NoCommunication / Timeout / ModbusException. Distinct from static `opc-map doctor`.
[[nodiscard]] RuntimeDoctorReport runtime_doctor(const RuntimeIndex& index,
                                                 const ports::ITagStore& store);

}  // namespace opc::core
