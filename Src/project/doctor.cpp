#include "project/doctor.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace opc::project {
namespace {

int regs_for(const Tag& tag) {
    if (tag.quantity.has_value()) {
        return *tag.quantity;
    }
    switch (tag.type) {
    case TagType::Bool:
    case TagType::UInt16:
    case TagType::Int16:
        return 1;
    case TagType::UInt32:
    case TagType::Int32:
    case TagType::Float32:
        return 2;
    case TagType::Float64:
        return 4;
    }
    return 1;
}

const char* area_name(Area area) {
    switch (area) {
    case Area::Holding:
        return "holding";
    case Area::Input:
        return "input";
    case Area::Coil:
        return "coil";
    case Area::Discrete:
        return "discrete";
    }
    return "area";
}

struct Span {
    int start{0};
    int end{0};  // exclusive
    std::string label;
};

void add(DoctorReport& report, Diagnostic::Severity severity, std::string path, std::string message) {
    if (severity == Diagnostic::Severity::Error) {
        ++report.error_count;
    } else {
        ++report.warning_count;
    }
    report.findings.push_back(Diagnostic{severity, std::move(path), std::move(message)});
}

void check_overlaps(DoctorReport& report, const std::vector<Span>& spans, const std::string& path) {
    for (std::size_t i = 0; i < spans.size(); ++i) {
        for (std::size_t j = i + 1; j < spans.size(); ++j) {
            const bool overlap = spans[i].start < spans[j].end && spans[j].start < spans[i].end;
            if (overlap) {
                add(report, Diagnostic::Severity::Warning, path,
                    "overlap " + spans[i].label + " [" + std::to_string(spans[i].start) + "," +
                        std::to_string(spans[i].end) + ") with " + spans[j].label + " [" +
                        std::to_string(spans[j].start) + "," + std::to_string(spans[j].end) + ")");
            }
        }
    }
}

void check_holes(DoctorReport& report, std::vector<Span> spans, const std::string& path) {
    if (spans.size() < 2) {
        return;
    }
    std::sort(spans.begin(), spans.end(), [](const Span& a, const Span& b) { return a.start < b.start; });
    for (std::size_t i = 1; i < spans.size(); ++i) {
        if (spans[i].start > spans[i - 1].end) {
            const int hole = spans[i].start - spans[i - 1].end;
            if (hole > 0 && hole <= 8) {
                add(report, Diagnostic::Severity::Warning, path,
                    "gap of " + std::to_string(hole) + " registers between " + spans[i - 1].label +
                        " and " + spans[i].label + " (consider coalescing the poll block)");
            }
        }
    }
}

}  // namespace

DoctorReport doctor(const Project& project) {
    DoctorReport report;

    std::unordered_set<std::string> polled_tags;
    for (const auto& group : project.poll_groups) {
        for (const auto& name : group.tag_names) {
            polled_tags.insert(group.device_id + "/" + name);
        }
    }

    std::unordered_map<std::string, const Device*> devices_by_id;
    for (const auto& device : project.devices) {
        devices_by_id.emplace(device.id, &device);
    }

    for (std::size_t di = 0; di < project.devices.size(); ++di) {
        const auto& device = project.devices[di];
        const std::string dpath = "devices[" + std::to_string(di) + "]";
        std::array<std::vector<Span>, 4> by_area{};
        for (std::size_t ti = 0; ti < device.tags.size(); ++ti) {
            const auto& tag = device.tags[ti];
            const std::string tpath = dpath + ".tags[" + std::to_string(ti) + "]";
            const int count = std::max(1, regs_for(tag));
            by_area[static_cast<std::size_t>(tag.area)].push_back(
                Span{tag.address, tag.address + count, tag.name});

            const std::string key = device.id + "/" + tag.name;
            const bool named_in_group = polled_tags.contains(key);
            const bool group_id_set = !tag.group.empty();
            if (!named_in_group && !group_id_set) {
                add(report, Diagnostic::Severity::Warning, tpath,
                    "tag '" + tag.name + "' is not referenced by any poll group");
            }
            if (tag.writable && tag.area != Area::Holding && tag.area != Area::Coil) {
                add(report, Diagnostic::Severity::Warning, tpath + ".writable",
                    "writable tag on " + std::string(area_name(tag.area)) +
                        " cannot be written with FC05/06/15/16");
            }
        }
        for (std::size_t ai = 0; ai < by_area.size(); ++ai) {
            auto& spans = by_area[ai];
            if (spans.empty()) {
                continue;
            }
            const std::string path = dpath + "." + area_name(static_cast<Area>(ai));
            check_overlaps(report, spans, path);
            check_holes(report, spans, path);
        }
    }

    for (std::size_t gi = 0; gi < project.poll_groups.size(); ++gi) {
        const auto& group = project.poll_groups[gi];
        const std::string gpath = "pollGroups[" + std::to_string(gi) + "]";
        if (group.period_ms < 50 && group.priority != Priority::Fast) {
            add(report, Diagnostic::Severity::Warning, gpath + ".periodMs",
                "periodMs < 50 but priority is not fast");
        }
        for (std::size_t bi = 0; bi < group.blocks.size(); ++bi) {
            const auto& block = group.blocks[bi];
            const std::string bpath = gpath + ".blocks[" + std::to_string(bi) + "]";
            if (block.count > 64) {
                add(report, Diagnostic::Severity::Warning, bpath + ".count",
                    "large block (" + std::to_string(block.count) +
                        " registers) may overrun Modbus RTT budget");
            }
            const auto* device = devices_by_id.contains(group.device_id) ? devices_by_id[group.device_id]
                                                                         : nullptr;
            if (device == nullptr || block.count < 1) {
                continue;
            }
            int covered = 0;
            for (const auto& tag : device->tags) {
                if (tag.area != block.area) {
                    continue;
                }
                const int count = std::max(1, regs_for(tag));
                const int start = tag.address;
                const int end = start + count;
                const int bstart = block.start;
                const int bend = block.start + block.count;
                const int overlap = std::max(0, std::min(end, bend) - std::max(start, bstart));
                covered += overlap;
            }
            if (block.count >= 8 && covered * 2 < block.count) {
                add(report, Diagnostic::Severity::Warning, bpath,
                    "sparse block: only " + std::to_string(covered) + " of " +
                        std::to_string(block.count) + " registers map to tags (holes)");
            }
        }
    }

    return report;
}

}  // namespace opc::project
