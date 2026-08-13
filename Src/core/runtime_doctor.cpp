#include "core/runtime_doctor.hpp"

namespace opc::core {
namespace {

const char* quality_name(domain::Quality q) {
    switch (q) {
    case domain::Quality::Good:
        return "Good";
    case domain::Quality::Uncertain:
        return "Uncertain";
    case domain::Quality::Bad:
        return "Bad";
    }
    return "Bad";
}

const char* reason_name(domain::QualityReason r) {
    switch (r) {
    case domain::QualityReason::None:
        return "None";
    case domain::QualityReason::NoCommunication:
        return "NoCommunication";
    case domain::QualityReason::DeviceFailure:
        return "DeviceFailure";
    case domain::QualityReason::Timeout:
        return "Timeout";
    case domain::QualityReason::ModbusException:
        return "ModbusException";
    case domain::QualityReason::DecodingError:
        return "DecodingError";
    case domain::QualityReason::Stale:
        return "Stale";
    case domain::QualityReason::WritePending:
        return "WritePending";
    case domain::QualityReason::WriteRejected:
        return "WriteRejected";
    case domain::QualityReason::OutOfRange:
        return "OutOfRange";
    }
    return "Unknown";
}

void add_finding(RuntimeDoctorReport& report,
                 RuntimeDoctorFinding::Severity severity,
                 std::string tag_name,
                 std::string message) {
    if (severity == RuntimeDoctorFinding::Severity::Error) {
        ++report.error_count;
    } else {
        ++report.warning_count;
    }
    report.findings.push_back(
        RuntimeDoctorFinding{severity, std::move(tag_name), std::move(message)});
}

}  // namespace

RuntimeDoctorReport runtime_doctor(const RuntimeIndex& index, const ports::ITagStore& store) {
    RuntimeDoctorReport report;
    for (const auto& binding : index.tags()) {
        const auto value = store.get(binding.id);
        if (!value) {
            add_finding(report, RuntimeDoctorFinding::Severity::Error, binding.tag.name,
                        "tag never published (missing from TagStore)");
            continue;
        }
        if (value->quality == domain::Quality::Good) {
            continue;
        }
        const auto severity = value->quality == domain::Quality::Uncertain
                                  ? RuntimeDoctorFinding::Severity::Warning
                                  : RuntimeDoctorFinding::Severity::Error;
        std::string message = "quality ";
        message += quality_name(value->quality);
        message += " (";
        message += reason_name(value->reason);
        message += ")";
        add_finding(report, severity, binding.tag.name, std::move(message));
    }
    return report;
}

}  // namespace opc::core
