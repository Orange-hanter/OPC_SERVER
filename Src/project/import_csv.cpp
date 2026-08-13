#include "project/import_csv.hpp"

#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace opc::project {
namespace {

using json = nlohmann::json;

std::string trim(std::string_view in) {
    std::size_t begin = 0;
    while (begin < in.size() && std::isspace(static_cast<unsigned char>(in[begin])) != 0) {
        ++begin;
    }
    std::size_t end = in.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(in[end - 1])) != 0) {
        --end;
    }
    return std::string{in.substr(begin, end - begin)};
}

std::string strip_utf8_bom(std::string_view in) {
    if (in.size() >= 3 && static_cast<unsigned char>(in[0]) == 0xEF &&
        static_cast<unsigned char>(in[1]) == 0xBB && static_cast<unsigned char>(in[2]) == 0xBF) {
        return std::string{in.substr(3)};
    }
    return std::string{in};
}

// RFC4180-ish: comma-separated, quoted fields, "" escapes a quote. No multiline fields.
std::vector<std::string> parse_csv_line(std::string_view line) {
    std::vector<std::string> fields;
    std::string current;
    bool in_quotes = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (in_quotes) {
            if (c == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') {
                    current.push_back('"');
                    ++i;
                } else {
                    in_quotes = false;
                }
            } else {
                current.push_back(c);
            }
            continue;
        }
        if (c == '"') {
            in_quotes = true;
            continue;
        }
        if (c == ',') {
            fields.push_back(std::move(current));
            current.clear();
            continue;
        }
        if (c == '\r') {
            continue;
        }
        current.push_back(c);
    }
    if (in_quotes) {
        throw std::runtime_error("CSV: unterminated quoted field");
    }
    fields.push_back(std::move(current));
    return fields;
}

bool parse_bool(std::string_view raw, std::string_view column) {
    const auto v = trim(raw);
    if (v == "true" || v == "TRUE" || v == "True" || v == "1" || v == "yes" || v == "YES") {
        return true;
    }
    if (v == "false" || v == "FALSE" || v == "False" || v == "0" || v == "no" || v == "NO" || v.empty()) {
        return false;
    }
    throw std::runtime_error("CSV: invalid boolean in '" + std::string(column) + "': " + std::string(v));
}

int parse_int(std::string_view raw, std::string_view column) {
    const auto v = trim(raw);
    try {
        std::size_t idx = 0;
        const int n = std::stoi(v, &idx);
        if (idx != v.size()) {
            throw std::runtime_error("trailing junk");
        }
        return n;
    } catch (const std::exception&) {
        throw std::runtime_error("CSV: invalid integer in '" + std::string(column) + "': " + std::string(v));
    }
}

double parse_double(std::string_view raw, std::string_view column) {
    const auto v = trim(raw);
    if (v.empty()) {
        return 0.0;
    }
    try {
        std::size_t idx = 0;
        const double n = std::stod(v, &idx);
        if (idx != v.size()) {
            throw std::runtime_error("trailing junk");
        }
        return n;
    } catch (const std::exception&) {
        throw std::runtime_error("CSV: invalid number in '" + std::string(column) + "': " + std::string(v));
    }
}

const std::unordered_set<std::string> kAreas{"holding", "input", "coil", "discrete"};
const std::unordered_set<std::string> kTypes{"bool", "uint16", "int16", "uint32", "int32", "float32",
                                             "float64"};
const std::unordered_set<std::string> kByteOrders{"", "ABCD", "CDAB", "BADC", "DCBA", "AB", "BA"};

std::string field(const std::vector<std::string>& row,
                  const std::unordered_map<std::string, std::size_t>& cols,
                  const std::string& name,
                  bool required) {
    const auto it = cols.find(name);
    if (it == cols.end()) {
        if (required) {
            throw std::runtime_error("CSV: missing required column '" + name + "'");
        }
        return {};
    }
    if (it->second >= row.size()) {
        return {};
    }
    return row[it->second];
}

json tag_json_from_row(const std::vector<std::string>& row,
                       const std::unordered_map<std::string, std::size_t>& cols,
                       const ImportCsvOptions& options) {
    const std::string name = trim(field(row, cols, "name", true));
    if (name.empty()) {
        throw std::runtime_error("CSV: tag name is required");
    }
    const std::string area = trim(field(row, cols, "area", true));
    if (!kAreas.contains(area)) {
        throw std::runtime_error("CSV: unknown area '" + area + "' for tag '" + name + "'");
    }
    const std::string type = trim(field(row, cols, "type", true));
    if (!kTypes.contains(type)) {
        throw std::runtime_error("CSV: unknown type '" + type + "' for tag '" + name + "'");
    }
    const std::string byte_order = trim(field(row, cols, "byteOrder", true));
    if (!kByteOrders.contains(byte_order)) {
        throw std::runtime_error("CSV: invalid byteOrder '" + byte_order + "' for tag '" + name + "'");
    }

    json tag = {
        {"name", name},
        {"area", area},
        {"address", parse_int(field(row, cols, "address", true), "address")},
        {"type", type},
        {"writable", parse_bool(field(row, cols, "writable", true), "writable")},
    };

    std::string node_path = trim(field(row, cols, "nodePath", false));
    if (node_path.empty()) {
        node_path = "Plant/" + name;
    }
    tag["nodePath"] = node_path;

    if (!byte_order.empty()) {
        tag["byteOrder"] = byte_order;
    }

    const std::string scale_raw = field(row, cols, "scale", true);
    tag["scale"] = trim(scale_raw).empty() ? 1.0 : parse_double(scale_raw, "scale");
    tag["offset"] = parse_double(field(row, cols, "offset", true), "offset");

    const std::string unit = trim(field(row, cols, "unit", true));
    if (!unit.empty()) {
        tag["unit"] = unit;
    }

    std::string group = trim(field(row, cols, "group", false));
    if (group.empty()) {
        group = options.group;
    }
    if (!group.empty()) {
        tag["group"] = group;
    }

    const std::string description = trim(field(row, cols, "description", false));
    if (!description.empty()) {
        tag["description"] = description;
    }

    const std::string quantity_raw = trim(field(row, cols, "quantity", false));
    if (!quantity_raw.empty()) {
        tag["quantity"] = parse_int(quantity_raw, "quantity");
    }

    return tag;
}

}  // namespace

nlohmann::json import_csv_text(std::string_view csv, const ImportCsvOptions& options) {
    if (options.unit_id < 0 || options.unit_id > 255) {
        throw std::runtime_error("import-csv: unit-id must be in [0, 255]");
    }
    if (options.device_id.empty() || options.endpoint_id.empty()) {
        throw std::runtime_error("import-csv: device-id and endpoint-id must be non-empty");
    }

    const std::string text = strip_utf8_bom(csv);
    std::istringstream in(text);
    std::string line;
    std::unordered_map<std::string, std::size_t> cols;
    bool have_header = false;
    json tags = json::array();
    std::unordered_set<std::string> names;
    int row_no = 0;

    while (std::getline(in, line)) {
        ++row_no;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const auto stripped = trim(line);
        if (stripped.empty() || stripped.front() == '#') {
            continue;
        }
        auto fields = parse_csv_line(line);
        if (!have_header) {
            for (std::size_t i = 0; i < fields.size(); ++i) {
                const auto header = trim(fields[i]);
                if (!header.empty()) {
                    cols[header] = i;
                }
            }
            for (const char* required :
                 {"name", "area", "address", "type", "byteOrder", "scale", "offset", "unit", "writable"}) {
                if (!cols.contains(required)) {
                    throw std::runtime_error(std::string("CSV: missing required header column '") +
                                             required + "'");
                }
            }
            have_header = true;
            continue;
        }
        try {
            json tag = tag_json_from_row(fields, cols, options);
            const auto name = tag["name"].get<std::string>();
            if (!names.insert(name).second) {
                throw std::runtime_error("duplicate tag name '" + name + "'");
            }
            tags.push_back(std::move(tag));
        } catch (const std::exception& ex) {
            throw std::runtime_error(std::string("CSV row ") + std::to_string(row_no) + ": " + ex.what());
        }
    }

    if (!have_header) {
        throw std::runtime_error("CSV: missing header row");
    }
    if (tags.empty()) {
        throw std::runtime_error("CSV: no data rows");
    }

    json tag_names = json::array();
    for (const auto& tag : tags) {
        tag_names.push_back(tag["name"]);
    }

    json out;
    out["schemaVersion"] = 1;
    out["name"] = options.project_name;
    out["description"] =
        "Imported from CSV register map. TODO: set endpoints host/port and poll periods.";
    out["addressBase"] = 0;
    out["opcua"] = {
        {"endpointUrl", "opc.tcp://0.0.0.0:4840"},
        {"applicationName", "OPC_SERVER CSV Import"},
        {"securityPolicy", "None"},
        {"securityMode", "None"},
        {"namespaceUri", "urn:opc-server:csv-import"},
    };
    out["endpoints"] = json::array({
        {
            {"id", options.endpoint_id},
            {"host", options.host},
            {"port", options.port},
            {"transport", "tcp"},
            {"connectTimeoutMs", 3000},
            {"responseTimeoutMs", 1000},
            {"reconnectDelayMs", 2000},
        },
    });
    out["deviceProfiles"] = json::array();
    out["devices"] = json::array({
        {
            {"id", options.device_id},
            {"endpointId", options.endpoint_id},
            {"unitId", options.unit_id},
            {"description", "Imported from CSV"},
            {"tags", std::move(tags)},
        },
    });
    out["pollGroups"] = json::array({
        {
            {"id", options.group},
            {"periodMs", 1000},
            {"priority", "normal"},
            {"deviceId", options.device_id},
            {"tagNames", std::move(tag_names)},
        },
    });
    return out;
}

std::string import_csv_file_to_string(const std::string& csv_path, const ImportCsvOptions& options) {
    std::ifstream in(csv_path);
    if (!in) {
        throw std::runtime_error("cannot open CSV file: " + csv_path);
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return import_csv_text(ss.str(), options).dump(2);
}

}  // namespace opc::project
