#include "project/migrate_legacy.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace opc::project {
namespace {

using json = nlohmann::json;

std::string map_legacy_type(const json& type_node) {
    if (type_node.is_string()) {
        const auto t = type_node.get<std::string>();
        if (t == "float") {
            return "float32";
        }
        if (t == "word" || t == "uint_16") {
            return "uint16";
        }
        return t;
    }
    if (type_node.is_object() && type_node.contains("type") && type_node["type"].is_string()) {
        return map_legacy_type(type_node["type"]);
    }
    return "uint16";
}

std::string map_legacy_byte_order(const json& type_node, const std::string& type) {
    std::string raw;
    if (type_node.is_object() && type_node.contains("byteorder") && type_node["byteorder"].is_string()) {
        raw = type_node["byteorder"].get<std::string>();
    }
    // Legacy used digit permutations like "01234567" / "01325476".
    if (raw == "01234567" || raw == "0123") {
        return (type == "uint16" || type == "int16") ? "AB" : "ABCD";
    }
    if (raw == "01325476" || raw == "1032") {
        return (type == "uint16" || type == "int16") ? "BA" : "CDAB";
    }
    if (type == "uint16" || type == "int16") {
        return "AB";
    }
    if (type == "float32" || type == "uint32" || type == "int32") {
        return "ABCD";
    }
    return {};
}

std::string tag_name_from(const json& type_node, const std::string& device_id, int offset) {
    if (type_node.is_object() && type_node.contains("name") && type_node["name"].is_string()) {
        return type_node["name"].get<std::string>();
    }
    return device_id + ".reg" + std::to_string(offset);
}

}  // namespace

nlohmann::json migrate_legacy_nodes(const nlohmann::json& legacy) {
    json out;
    out["schemaVersion"] = 1;
    out["name"] = "migrated-legacy";
    out["description"] =
        "Migrated from legacy config.json Nodes format. TODO: set endpoints host/port.";
    out["addressBase"] = 0;
    out["opcua"] = {
        {"endpointUrl", "opc.tcp://0.0.0.0:4840"},
        {"applicationName", "OPC_SERVER Migrated"},
        {"securityPolicy", "None"},
        {"securityMode", "None"},
        {"namespaceUri", "urn:opc-server:migrated"},
    };

    json endpoints = json::array();
    endpoints.push_back({
        {"id", "legacy-endpoint-1"},
        {"host", "127.0.0.1"},
        {"port", 502},
        {"transport", "tcp"},
        {"connectTimeoutMs", 3000},
        {"responseTimeoutMs", 1000},
        {"reconnectDelayMs", 2000},
    });
    out["endpoints"] = endpoints;

    json devices = json::array();
    json poll_groups = json::array();

    if (!legacy.contains("Nodes") || !legacy["Nodes"].is_object()) {
        throw std::runtime_error("legacy config: missing object 'Nodes'");
    }

    for (auto it = legacy["Nodes"].begin(); it != legacy["Nodes"].end(); ++it) {
        const std::string device_id = it.key();
        const json& node = it.value();
        const int unit_id = node.value("id", 1);
        const int shift = node.value("shift", 0);
        const int registers = node.value("registers", 0);

        json device = {
            {"id", device_id},
            {"endpointId", "legacy-endpoint-1"},
            {"unitId", unit_id},
            {"description", "Migrated legacy node"},
            {"tags", json::array()},
        };

        if (node.contains("API") && node["API"].is_object()) {
            for (auto api = node["API"].begin(); api != node["API"].end(); ++api) {
                const int offset = std::stoi(api.key());
                const json& type_node = api.value();
                const std::string type = map_legacy_type(type_node);
                const std::string name = tag_name_from(type_node, device_id, offset);
                json tag = {
                    {"name", name},
                    {"nodePath", "Plant/" + device_id + "/" + name},
                    {"area", "holding"},
                    {"address", shift + offset},
                    {"type", type},
                    {"byteOrder", map_legacy_byte_order(type_node, type)},
                    {"writable", false},
                    {"group", device_id + "-migrated"},
                };
                if (type_node.is_object() && type_node.contains("description")) {
                    tag["description"] = type_node["description"];
                }
                device["tags"].push_back(std::move(tag));
            }
        }

        json group = {
            {"id", device_id + "-migrated"},
            {"periodMs", 500},
            {"priority", "normal"},
            {"deviceId", device_id},
            {"blocks",
             json::array({
                 {{"area", "holding"},
                  {"start", shift},
                  {"count", registers > 0 ? registers : 1},
                  {"description", "Migrated block from legacy code/shift/registers"}},
             })},
        };

        devices.push_back(std::move(device));
        poll_groups.push_back(std::move(group));
    }

    out["devices"] = std::move(devices);
    out["pollGroups"] = std::move(poll_groups);
    out["deviceProfiles"] = json::array();
    return out;
}

std::string migrate_legacy_file_to_string(const std::string& legacy_path) {
    std::ifstream in(legacy_path);
    if (!in) {
        throw std::runtime_error("cannot open legacy file: " + legacy_path);
    }
    json legacy;
    in >> legacy;
    return migrate_legacy_nodes(legacy).dump(2);
}

}  // namespace opc::project
