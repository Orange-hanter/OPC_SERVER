#include "project/load.hpp"
#include "project/schema.hpp"
#include "project/validate.hpp"

#include <json.hpp>

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace opc::project {
namespace {

using json = nlohmann::json;

void add_error(std::vector<Diagnostic>& diags, std::string path, std::string message) {
    diags.push_back(Diagnostic{Diagnostic::Severity::Error, std::move(path), std::move(message)});
}

template <typename Enum>
Enum parse_enum(const json& value,
                const std::unordered_map<std::string, Enum>& map,
                std::string_view path,
                std::vector<Diagnostic>& diags,
                Enum fallback) {
    if (!value.is_string()) {
        add_error(diags, std::string(path), "expected string enum");
        return fallback;
    }
    const auto it = map.find(value.get<std::string>());
    if (it == map.end()) {
        add_error(diags, std::string(path), "unknown enum value '" + value.get<std::string>() + "'");
        return fallback;
    }
    return it->second;
}

const std::unordered_map<std::string, Transport> kTransport{
    {"tcp", Transport::Tcp},
    {"udp", Transport::Udp},
};

const std::unordered_map<std::string, SecurityPolicy> kSecurityPolicy{
    {"None", SecurityPolicy::None},
    {"Basic256Sha256", SecurityPolicy::Basic256Sha256},
};

const std::unordered_map<std::string, SecurityMode> kSecurityMode{
    {"None", SecurityMode::None},
    {"Sign", SecurityMode::Sign},
    {"SignAndEncrypt", SecurityMode::SignAndEncrypt},
};

const std::unordered_map<std::string, Area> kArea{
    {"holding", Area::Holding},
    {"input", Area::Input},
    {"coil", Area::Coil},
    {"discrete", Area::Discrete},
};

const std::unordered_map<std::string, TagType> kTagType{
    {"bool", TagType::Bool},
    {"uint16", TagType::UInt16},
    {"int16", TagType::Int16},
    {"uint32", TagType::UInt32},
    {"int32", TagType::Int32},
    {"float32", TagType::Float32},
    {"float64", TagType::Float64},
};

const std::unordered_map<std::string, Priority> kPriority{
    {"fast", Priority::Fast},
    {"normal", Priority::Normal},
    {"slow", Priority::Slow},
};

Tag parse_tag(const json& j, std::string_view path, std::vector<Diagnostic>& diags) {
    Tag tag;
    if (!j.is_object()) {
        add_error(diags, std::string(path), "tag must be an object");
        return tag;
    }
    if (!j.contains("name") || !j["name"].is_string()) {
        add_error(diags, std::string(path) + ".name", "required string");
    } else {
        tag.name = j["name"].get<std::string>();
    }
    if (j.contains("nodePath") && j["nodePath"].is_string()) {
        tag.node_path = j["nodePath"].get<std::string>();
    }
    if (!j.contains("area")) {
        add_error(diags, std::string(path) + ".area", "required");
    } else {
        tag.area = parse_enum(j["area"], kArea, std::string(path) + ".area", diags, Area::Holding);
    }
    if (!j.contains("address") || !j["address"].is_number_integer()) {
        add_error(diags, std::string(path) + ".address", "required integer");
    } else {
        tag.address = j["address"].get<int>();
    }
    if (!j.contains("type")) {
        add_error(diags, std::string(path) + ".type", "required");
    } else {
        tag.type = parse_enum(j["type"], kTagType, std::string(path) + ".type", diags, TagType::UInt16);
    }
    if (j.contains("quantity") && j["quantity"].is_number_integer()) {
        tag.quantity = j["quantity"].get<int>();
    }
    if (j.contains("byteOrder") && j["byteOrder"].is_string()) {
        tag.byte_order = j["byteOrder"].get<std::string>();
    }
    if (j.contains("scale") && j["scale"].is_number()) {
        tag.scale = j["scale"].get<double>();
    }
    if (j.contains("offset") && j["offset"].is_number()) {
        tag.offset = j["offset"].get<double>();
    }
    if (j.contains("unit") && j["unit"].is_string()) {
        tag.unit = j["unit"].get<std::string>();
    }
    if (j.contains("writable") && j["writable"].is_boolean()) {
        tag.writable = j["writable"].get<bool>();
    }
    if (j.contains("group") && j["group"].is_string()) {
        tag.group = j["group"].get<std::string>();
    }
    if (j.contains("description") && j["description"].is_string()) {
        tag.description = j["description"].get<std::string>();
    }
    return tag;
}

Endpoint parse_endpoint(const json& j, std::string_view path, std::vector<Diagnostic>& diags) {
    Endpoint ep;
    if (!j.is_object()) {
        add_error(diags, std::string(path), "endpoint must be an object");
        return ep;
    }
    if (!j.contains("id") || !j["id"].is_string()) {
        add_error(diags, std::string(path) + ".id", "required string");
    } else {
        ep.id = j["id"].get<std::string>();
    }
    if (!j.contains("host") || !j["host"].is_string()) {
        add_error(diags, std::string(path) + ".host", "required string");
    } else {
        ep.host = j["host"].get<std::string>();
    }
    if (!j.contains("port") || !j["port"].is_number_integer()) {
        add_error(diags, std::string(path) + ".port", "required integer");
    } else {
        const int port = j["port"].get<int>();
        if (port < 1 || port > 65535) {
            add_error(diags, std::string(path) + ".port", "port out of range");
        } else {
            ep.port = static_cast<std::uint16_t>(port);
        }
    }
    if (!j.contains("transport")) {
        add_error(diags, std::string(path) + ".transport", "required");
    } else {
        ep.transport =
            parse_enum(j["transport"], kTransport, std::string(path) + ".transport", diags, Transport::Tcp);
    }
    if (j.contains("connectTimeoutMs") && j["connectTimeoutMs"].is_number_integer()) {
        ep.connect_timeout_ms = j["connectTimeoutMs"].get<int>();
    }
    if (j.contains("responseTimeoutMs") && j["responseTimeoutMs"].is_number_integer()) {
        ep.response_timeout_ms = j["responseTimeoutMs"].get<int>();
    }
    if (j.contains("reconnectDelayMs") && j["reconnectDelayMs"].is_number_integer()) {
        ep.reconnect_delay_ms = j["reconnectDelayMs"].get<int>();
    }
    return ep;
}

RegisterBlock parse_block(const json& j, std::string_view path, std::vector<Diagnostic>& diags) {
    RegisterBlock block;
    if (!j.is_object()) {
        add_error(diags, std::string(path), "block must be an object");
        return block;
    }
    if (!j.contains("area")) {
        add_error(diags, std::string(path) + ".area", "required");
    } else {
        block.area = parse_enum(j["area"], kArea, std::string(path) + ".area", diags, Area::Holding);
    }
    if (!j.contains("start") || !j["start"].is_number_integer()) {
        add_error(diags, std::string(path) + ".start", "required integer");
    } else {
        block.start = j["start"].get<int>();
    }
    if (!j.contains("count") || !j["count"].is_number_integer()) {
        add_error(diags, std::string(path) + ".count", "required integer");
    } else {
        block.count = j["count"].get<int>();
    }
    if (j.contains("description") && j["description"].is_string()) {
        block.description = j["description"].get<std::string>();
    }
    return block;
}

Project parse_project(const json& root, std::vector<Diagnostic>& diags) {
    Project project;
    if (!root.is_object()) {
        add_error(diags, "$", "root must be a JSON object");
        return project;
    }

    if (!root.contains("schemaVersion") || !root["schemaVersion"].is_number_integer()) {
        add_error(diags, "schemaVersion", "required integer");
    } else {
        project.schema_version = root["schemaVersion"].get<int>();
    }

    if (!root.contains("name") || !root["name"].is_string() || root["name"].get<std::string>().empty()) {
        add_error(diags, "name", "required non-empty string");
    } else {
        project.name = root["name"].get<std::string>();
    }

    if (root.contains("description") && root["description"].is_string()) {
        project.description = root["description"].get<std::string>();
    }
    if (root.contains("addressBase") && root["addressBase"].is_number_integer()) {
        project.address_base = root["addressBase"].get<int>();
        if (project.address_base != 0 && project.address_base != 1) {
            add_error(diags, "addressBase", "must be 0 or 1");
        }
    }

    if (root.contains("opcua") && root["opcua"].is_object()) {
        const auto& o = root["opcua"];
        if (o.contains("endpointUrl") && o["endpointUrl"].is_string()) {
            project.opcua.endpoint_url = o["endpointUrl"].get<std::string>();
        }
        if (o.contains("applicationName") && o["applicationName"].is_string()) {
            project.opcua.application_name = o["applicationName"].get<std::string>();
        }
        if (o.contains("securityPolicy")) {
            project.opcua.security_policy = parse_enum(
                o["securityPolicy"], kSecurityPolicy, "opcua.securityPolicy", diags, SecurityPolicy::None);
        }
        if (o.contains("securityMode")) {
            project.opcua.security_mode =
                parse_enum(o["securityMode"], kSecurityMode, "opcua.securityMode", diags, SecurityMode::None);
        }
        if (o.contains("namespaceUri") && o["namespaceUri"].is_string()) {
            project.opcua.namespace_uri = o["namespaceUri"].get<std::string>();
        }
        if (o.contains("users") && o["users"].is_array()) {
            std::size_t ui = 0;
            for (const auto& item : o["users"]) {
                const std::string path = "opcua.users[" + std::to_string(ui++) + "]";
                OpcUaUser user;
                if (!item.is_object()) {
                    add_error(diags, path, "must be object with username/password");
                    continue;
                }
                if (!item.contains("username") || !item["username"].is_string() ||
                    item["username"].get<std::string>().empty()) {
                    add_error(diags, path + ".username", "required non-empty string");
                } else {
                    user.username = item["username"].get<std::string>();
                }
                if (!item.contains("password") || !item["password"].is_string()) {
                    add_error(diags, path + ".password", "required string");
                } else {
                    user.password = item["password"].get<std::string>();
                }
                if (!user.username.empty()) {
                    project.opcua.users.push_back(std::move(user));
                }
            }
        }
        if (o.contains("allowCertificateIdentity") && o["allowCertificateIdentity"].is_boolean()) {
            project.opcua.allow_certificate_identity = o["allowCertificateIdentity"].get<bool>();
        }
        if (o.contains("allowNoneCertificate") && o["allowNoneCertificate"].is_boolean()) {
            project.opcua.allow_none_certificate = o["allowNoneCertificate"].get<bool>();
        }
        if (o.contains("allowAnonymous") && o["allowAnonymous"].is_boolean()) {
            project.opcua.allow_anonymous = o["allowAnonymous"].get<bool>();
        } else if (!project.opcua.users.empty() || project.opcua.allow_certificate_identity) {
            // Fail-closed when identity is configured: anonymous off unless explicitly enabled.
            project.opcua.allow_anonymous = false;
        }
        if (o.contains("allowNonePassword") && o["allowNonePassword"].is_boolean()) {
            project.opcua.allow_none_password = o["allowNonePassword"].get<bool>();
        }
    }

    if (!root.contains("endpoints") || !root["endpoints"].is_array() || root["endpoints"].empty()) {
        add_error(diags, "endpoints", "required non-empty array");
    } else {
        std::size_t i = 0;
        for (const auto& item : root["endpoints"]) {
            project.endpoints.push_back(parse_endpoint(item, "endpoints[" + std::to_string(i++) + "]", diags));
        }
    }

    if (root.contains("deviceProfiles") && root["deviceProfiles"].is_array()) {
        std::size_t i = 0;
        for (const auto& item : root["deviceProfiles"]) {
            DeviceProfile profile;
            const std::string path = "deviceProfiles[" + std::to_string(i++) + "]";
            if (!item.is_object()) {
                add_error(diags, path, "profile must be an object");
                continue;
            }
            if (!item.contains("id") || !item["id"].is_string()) {
                add_error(diags, path + ".id", "required string");
            } else {
                profile.id = item["id"].get<std::string>();
            }
            if (!item.contains("name") || !item["name"].is_string()) {
                add_error(diags, path + ".name", "required string");
            } else {
                profile.name = item["name"].get<std::string>();
            }
            if (item.contains("vendor") && item["vendor"].is_string()) {
                profile.vendor = item["vendor"].get<std::string>();
            }
            if (item.contains("description") && item["description"].is_string()) {
                profile.description = item["description"].get<std::string>();
            }
            if (item.contains("tags") && item["tags"].is_array()) {
                std::size_t ti = 0;
                for (const auto& tag : item["tags"]) {
                    profile.tags.push_back(parse_tag(tag, path + ".tags[" + std::to_string(ti++) + "]", diags));
                }
            }
            project.device_profiles.push_back(std::move(profile));
        }
    }

    if (!root.contains("devices") || !root["devices"].is_array() || root["devices"].empty()) {
        add_error(diags, "devices", "required non-empty array");
    } else {
        std::size_t i = 0;
        for (const auto& item : root["devices"]) {
            Device device;
            const std::string path = "devices[" + std::to_string(i++) + "]";
            if (!item.is_object()) {
                add_error(diags, path, "device must be an object");
                continue;
            }
            if (!item.contains("id") || !item["id"].is_string()) {
                add_error(diags, path + ".id", "required string");
            } else {
                device.id = item["id"].get<std::string>();
            }
            if (!item.contains("endpointId") || !item["endpointId"].is_string()) {
                add_error(diags, path + ".endpointId", "required string");
            } else {
                device.endpoint_id = item["endpointId"].get<std::string>();
            }
            if (!item.contains("unitId") || !item["unitId"].is_number_integer()) {
                add_error(diags, path + ".unitId", "required integer");
            } else {
                device.unit_id = item["unitId"].get<int>();
            }
            if (item.contains("profileId") && item["profileId"].is_string()) {
                device.profile_id = item["profileId"].get<std::string>();
            }
            if (item.contains("description") && item["description"].is_string()) {
                device.description = item["description"].get<std::string>();
            }
            if (item.contains("tags") && item["tags"].is_array()) {
                std::size_t ti = 0;
                for (const auto& tag : item["tags"]) {
                    device.tags.push_back(parse_tag(tag, path + ".tags[" + std::to_string(ti++) + "]", diags));
                }
            }
            project.devices.push_back(std::move(device));
        }
    }

    if (!root.contains("pollGroups") || !root["pollGroups"].is_array() || root["pollGroups"].empty()) {
        add_error(diags, "pollGroups", "required non-empty array");
    } else {
        std::size_t i = 0;
        for (const auto& item : root["pollGroups"]) {
            PollGroup group;
            const std::string path = "pollGroups[" + std::to_string(i++) + "]";
            if (!item.is_object()) {
                add_error(diags, path, "pollGroup must be an object");
                continue;
            }
            if (!item.contains("id") || !item["id"].is_string()) {
                add_error(diags, path + ".id", "required string");
            } else {
                group.id = item["id"].get<std::string>();
            }
            if (!item.contains("periodMs") || !item["periodMs"].is_number_integer()) {
                add_error(diags, path + ".periodMs", "required integer");
            } else {
                group.period_ms = item["periodMs"].get<int>();
            }
            if (!item.contains("priority")) {
                add_error(diags, path + ".priority", "required");
            } else {
                group.priority =
                    parse_enum(item["priority"], kPriority, path + ".priority", diags, Priority::Normal);
            }
            if (!item.contains("deviceId") || !item["deviceId"].is_string()) {
                add_error(diags, path + ".deviceId", "required string");
            } else {
                group.device_id = item["deviceId"].get<std::string>();
            }
            if (item.contains("blocks") && item["blocks"].is_array()) {
                std::size_t bi = 0;
                for (const auto& block : item["blocks"]) {
                    group.blocks.push_back(
                        parse_block(block, path + ".blocks[" + std::to_string(bi++) + "]", diags));
                }
            }
            if (item.contains("tagNames") && item["tagNames"].is_array()) {
                for (const auto& name : item["tagNames"]) {
                    if (name.is_string()) {
                        group.tag_names.push_back(name.get<std::string>());
                    } else {
                        add_error(diags, path + ".tagNames", "elements must be strings");
                    }
                }
            }
            project.poll_groups.push_back(std::move(group));
        }
    }

    return project;
}

void expand_device_profiles(Project& project) {
    std::unordered_map<std::string, const DeviceProfile*> by_id;
    for (const auto& profile : project.device_profiles) {
        if (!profile.id.empty()) {
            by_id.emplace(profile.id, &profile);
        }
    }

    for (auto& device : project.devices) {
        if (device.profile_id.empty()) {
            continue;
        }
        const auto it = by_id.find(device.profile_id);
        if (it == by_id.end()) {
            continue;  // unknown profileId: validate() reports the error
        }
        const DeviceProfile& profile = *it->second;
        if (profile.tags.empty()) {
            continue;
        }

        std::unordered_map<std::string, Tag> overlay;
        for (const auto& tag : device.tags) {
            if (!tag.name.empty()) {
                overlay[tag.name] = tag;
            }
        }
        std::unordered_set<std::string> from_profile;
        std::vector<Tag> merged;
        merged.reserve(profile.tags.size() + device.tags.size());
        for (const auto& profile_tag : profile.tags) {
            if (!profile_tag.name.empty()) {
                from_profile.insert(profile_tag.name);
            }
            if (!profile_tag.name.empty() && overlay.contains(profile_tag.name)) {
                merged.push_back(overlay[profile_tag.name]);
            } else {
                merged.push_back(profile_tag);
            }
        }
        for (const auto& tag : device.tags) {
            if (tag.name.empty() || !from_profile.contains(tag.name)) {
                merged.push_back(tag);
            }
        }
        device.tags = std::move(merged);
    }
}

}  // namespace

LoadResult load_json_text(std::string_view text, std::string_view source_name) {
    LoadResult result;
    json root;
    try {
        root = json::parse(text);
    } catch (const json::parse_error& ex) {
        add_error(result.diagnostics, std::string(source_name), std::string("JSON parse error: ") + ex.what());
        result.ok = false;
        return result;
    }

    result.project = parse_project(root, result.diagnostics);
    expand_device_profiles(result.project);
    append_json_schema_diagnostics(root, source_name, result.diagnostics);
    validate(result.project, result.diagnostics);

    result.ok = true;
    for (const auto& d : result.diagnostics) {
        if (d.severity == Diagnostic::Severity::Error) {
            result.ok = false;
            break;
        }
    }
    return result;
}

LoadResult load_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        LoadResult result;
        add_error(result.diagnostics, path, "cannot open file");
        result.ok = false;
        return result;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return load_json_text(ss.str(), path);
}

}  // namespace opc::project
