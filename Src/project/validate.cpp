#include "project/validate.hpp"

#include <unordered_map>
#include <unordered_set>

namespace opc::project {
namespace {

void add_error(std::vector<Diagnostic>& diags, std::string path, std::string message) {
    diags.push_back(Diagnostic{Diagnostic::Severity::Error, std::move(path), std::move(message)});
}

void add_warning(std::vector<Diagnostic>& diags, std::string path, std::string message) {
    diags.push_back(Diagnostic{Diagnostic::Severity::Warning, std::move(path), std::move(message)});
}

bool valid_byte_order(const std::string& order, TagType type) {
    if (order.empty()) {
        return type == TagType::Bool || type == TagType::UInt16 || type == TagType::Int16;
    }
    static const std::unordered_set<std::string> kAll{"ABCD", "CDAB", "BADC", "DCBA", "AB", "BA"};
    if (!kAll.contains(order)) {
        return false;
    }
    if (type == TagType::UInt16 || type == TagType::Int16) {
        return order == "AB" || order == "BA";
    }
    if (type == TagType::Float32 || type == TagType::UInt32 || type == TagType::Int32) {
        return order.size() == 4;
    }
    if (type == TagType::Float64) {
        return order.size() == 4;  // word-swap variants expressed as 4-char codes in v1
    }
    return true;
}

}  // namespace

void validate(Project& project, std::vector<Diagnostic>& diagnostics) {
    if (project.schema_version < 1) {
        add_error(diagnostics, "schemaVersion", "must be >= 1");
    }

    std::unordered_set<std::string> endpoint_ids;
    for (std::size_t i = 0; i < project.endpoints.size(); ++i) {
        const auto& ep = project.endpoints[i];
        const std::string path = "endpoints[" + std::to_string(i) + "]";
        if (ep.id.empty()) {
            continue;
        }
        if (!endpoint_ids.insert(ep.id).second) {
            add_error(diagnostics, path + ".id", "duplicate endpoint id '" + ep.id + "'");
        }
        if (ep.connect_timeout_ms < 1 || ep.response_timeout_ms < 1) {
            add_error(diagnostics, path, "timeouts must be >= 1");
        }
    }

    std::unordered_set<std::string> profile_ids;
    for (const auto& profile : project.device_profiles) {
        if (!profile.id.empty()) {
            profile_ids.insert(profile.id);
        }
    }

    std::unordered_map<std::string, const Device*> devices_by_id;
    std::unordered_set<std::string> device_ids;
    std::unordered_set<std::string> global_tag_names;
    for (std::size_t i = 0; i < project.devices.size(); ++i) {
        const auto& device = project.devices[i];
        const std::string path = "devices[" + std::to_string(i) + "]";
        if (!device.id.empty()) {
            if (!device_ids.insert(device.id).second) {
                add_error(diagnostics, path + ".id", "duplicate device id '" + device.id + "'");
            } else {
                devices_by_id.emplace(device.id, &device);
            }
        }
        if (!device.endpoint_id.empty() && !endpoint_ids.contains(device.endpoint_id)) {
            add_error(diagnostics, path + ".endpointId",
                      "unknown endpointId '" + device.endpoint_id + "'");
        }
        if (!device.profile_id.empty() && !profile_ids.contains(device.profile_id)) {
            add_error(diagnostics, path + ".profileId", "unknown profileId '" + device.profile_id + "'");
        }
        if (device.unit_id < 0 || device.unit_id > 255) {
            add_error(diagnostics, path + ".unitId", "unitId must be in [0, 255]");
        }

        std::unordered_set<std::string> tag_names;
        for (std::size_t ti = 0; ti < device.tags.size(); ++ti) {
            const auto& tag = device.tags[ti];
            const std::string tpath = path + ".tags[" + std::to_string(ti) + "]";
            if (!tag.name.empty() && !tag_names.insert(tag.name).second) {
                add_error(diagnostics, tpath + ".name", "duplicate tag name '" + tag.name + "'");
            } else if (!tag.name.empty() && !global_tag_names.insert(tag.name).second) {
                add_error(diagnostics, tpath + ".name",
                          "duplicate tag name '" + tag.name + "' across devices");
            }
            if (tag.address < 0) {
                add_error(diagnostics, tpath + ".address", "address must be >= 0");
            }
            if (!valid_byte_order(tag.byte_order, tag.type)) {
                add_error(diagnostics, tpath + ".byteOrder",
                          "invalid byteOrder '" + tag.byte_order + "' for type");
            }
            if (tag.quantity.has_value() && tag.quantity.value() < 1) {
                add_error(diagnostics, tpath + ".quantity", "quantity must be >= 1");
            }
            if (tag.writable &&
                (tag.area == Area::Input || tag.area == Area::Discrete)) {
                add_error(diagnostics, tpath + ".writable",
                          "input/discrete areas cannot be writable");
            }
        }
    }

    std::unordered_set<std::string> group_ids;
    for (std::size_t i = 0; i < project.poll_groups.size(); ++i) {
        const auto& group = project.poll_groups[i];
        const std::string path = "pollGroups[" + std::to_string(i) + "]";
        if (!group.id.empty() && !group_ids.insert(group.id).second) {
            add_error(diagnostics, path + ".id", "duplicate poll group id '" + group.id + "'");
        }
        if (group.period_ms < 10) {
            add_error(diagnostics, path + ".periodMs", "periodMs must be >= 10");
        }
        if (!group.device_id.empty() && !devices_by_id.contains(group.device_id)) {
            add_error(diagnostics, path + ".deviceId", "unknown deviceId '" + group.device_id + "'");
        }
        if (group.blocks.empty() && group.tag_names.empty()) {
            add_warning(diagnostics, path, "poll group has neither blocks nor tagNames");
        }
        for (std::size_t bi = 0; bi < group.blocks.size(); ++bi) {
            const auto& block = group.blocks[bi];
            const std::string bpath = path + ".blocks[" + std::to_string(bi) + "]";
            if (block.start < 0) {
                add_error(diagnostics, bpath + ".start", "start must be >= 0");
            }
            if (block.count < 1 || block.count > 125) {
                add_error(diagnostics, bpath + ".count", "count must be in [1, 125]");
            }
        }
        if (devices_by_id.contains(group.device_id)) {
            const Device* device = devices_by_id[group.device_id];
            std::unordered_set<std::string> names;
            for (const auto& tag : device->tags) {
                names.insert(tag.name);
            }
            for (const auto& tag_name : group.tag_names) {
                if (!names.contains(tag_name)) {
                    add_error(diagnostics, path + ".tagNames",
                              "tag '" + tag_name + "' not found on device '" + group.device_id + "'");
                }
            }
        }
    }

    // Tags referencing unknown groups -> warning
    for (std::size_t i = 0; i < project.devices.size(); ++i) {
        const auto& device = project.devices[i];
        for (std::size_t ti = 0; ti < device.tags.size(); ++ti) {
            const auto& tag = device.tags[ti];
            if (!tag.group.empty() && !group_ids.contains(tag.group)) {
                add_warning(diagnostics,
                            "devices[" + std::to_string(i) + "].tags[" + std::to_string(ti) + "].group",
                            "tag group '" + tag.group + "' has no matching pollGroups.id");
            }
        }
    }
}

}  // namespace opc::project
