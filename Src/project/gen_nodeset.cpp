#include "project/gen_nodeset.hpp"

#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace opc::project {
namespace {

std::string xml_escape(std::string_view in) {
    std::string out;
    out.reserve(in.size());
    for (const char c : in) {
        switch (c) {
        case '&':
            out += "&amp;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        case '"':
            out += "&quot;";
            break;
        case '\'':
            out += "&apos;";
            break;
        default:
            out.push_back(c);
            break;
        }
    }
    return out;
}

std::vector<std::string> split_path(std::string_view path) {
    std::vector<std::string> parts;
    std::string current;
    for (const char c : path) {
        if (c == '/') {
            if (!current.empty()) {
                parts.push_back(std::move(current));
                current.clear();
            }
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) {
        parts.push_back(std::move(current));
    }
    return parts;
}

std::string resolved_node_path(const Tag& tag) {
    if (!tag.node_path.empty()) {
        auto parts = split_path(tag.node_path);
        if (parts.size() >= 2) {
            return tag.node_path;
        }
        if (parts.size() == 1) {
            return "Plant/" + parts[0];
        }
    }
    return "Plant/" + tag.name;
}

const char* ua_data_type(TagType type) {
    switch (type) {
    case TagType::Bool:
        return "Boolean";
    case TagType::UInt16:
        return "UInt16";
    case TagType::Int16:
        return "Int16";
    case TagType::UInt32:
        return "UInt32";
    case TagType::Int32:
        return "Int32";
    case TagType::Float32:
        return "Float";
    case TagType::Float64:
        return "Double";
    }
    return "Float";
}

std::string parent_path(std::string_view path) {
    const auto slash = path.rfind('/');
    if (slash == std::string_view::npos) {
        return {};
    }
    return std::string{path.substr(0, slash)};
}

std::string browse_name(std::string_view path) {
    const auto slash = path.rfind('/');
    if (slash == std::string_view::npos) {
        return std::string{path};
    }
    return std::string{path.substr(slash + 1)};
}

}  // namespace

std::string generate_nodeset(const Project& project) {
    std::set<std::string> folders;
    struct VariableRow {
        std::string path;
        const Tag* tag;
    };
    std::vector<VariableRow> variables;

    for (const auto& device : project.devices) {
        for (const auto& tag : device.tags) {
            const std::string path = resolved_node_path(tag);
            const auto parts = split_path(path);
            std::string cumulative;
            for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
                if (!cumulative.empty()) {
                    cumulative.push_back('/');
                }
                cumulative += parts[i];
                folders.insert(cumulative);
            }
            variables.push_back(VariableRow{path, &tag});
        }
    }

    const std::string ns_uri =
        project.opcua.namespace_uri.empty() ? "urn:opc-server:" + project.name : project.opcua.namespace_uri;

    std::ostringstream out;
    out << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        << "<UANodeSet xmlns=\"http://opcfoundation.org/UA/2011/03/UANodeSet.xsd\"\n"
        << "           xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"\n"
        << "           xmlns:uax=\"http://opcfoundation.org/UA/2008/02/Types.xsd\">\n"
        << "  <NamespaceUris>\n"
        << "    <Uri>" << xml_escape(ns_uri) << "</Uri>\n"
        << "  </NamespaceUris>\n"
        << "  <Aliases>\n"
        << "    <Alias Alias=\"Boolean\">i=1</Alias>\n"
        << "    <Alias Alias=\"Int16\">i=4</Alias>\n"
        << "    <Alias Alias=\"UInt16\">i=5</Alias>\n"
        << "    <Alias Alias=\"Int32\">i=6</Alias>\n"
        << "    <Alias Alias=\"UInt32\">i=7</Alias>\n"
        << "    <Alias Alias=\"Float\">i=10</Alias>\n"
        << "    <Alias Alias=\"Double\">i=11</Alias>\n"
        << "    <Alias Alias=\"Organizes\">i=35</Alias>\n"
        << "    <Alias Alias=\"HasComponent\">i=47</Alias>\n"
        << "    <Alias Alias=\"HasTypeDefinition\">i=40</Alias>\n"
        << "    <Alias Alias=\"FolderType\">i=61</Alias>\n"
        << "    <Alias Alias=\"BaseDataVariableType\">i=63</Alias>\n"
        << "  </Aliases>\n";

    for (const auto& folder : folders) {
        const std::string parent = parent_path(folder);
        const std::string parent_id = parent.empty() ? "i=85" : "ns=1;s=" + parent;
        const std::string name = browse_name(folder);
        out << "  <UAObject NodeId=\"ns=1;s=" << xml_escape(folder) << "\" BrowseName=\"1:"
            << xml_escape(name) << "\">\n"
            << "    <DisplayName>" << xml_escape(name) << "</DisplayName>\n"
            << "    <References>\n"
            << "      <Reference ReferenceType=\"Organizes\" IsForward=\"false\">" << xml_escape(parent_id)
            << "</Reference>\n"
            << "      <Reference ReferenceType=\"HasTypeDefinition\">i=61</Reference>\n"
            << "    </References>\n"
            << "  </UAObject>\n";
    }

    for (const auto& row : variables) {
        const std::string parent = parent_path(row.path);
        const std::string parent_id = parent.empty() ? "i=85" : "ns=1;s=" + parent;
        const std::string name = browse_name(row.path);
        const int access = row.tag->writable ? 3 : 1;
        const std::string desc = row.tag->description.empty() ? row.tag->name : row.tag->description;
        out << "  <UAVariable NodeId=\"ns=1;s=" << xml_escape(row.path) << "\" BrowseName=\"1:"
            << xml_escape(name) << "\" DataType=\"" << ua_data_type(row.tag->type)
            << "\" ValueRank=\"-1\" AccessLevel=\"" << access << "\" UserAccessLevel=\"" << access
            << "\">\n"
            << "    <DisplayName>" << xml_escape(name) << "</DisplayName>\n"
            << "    <Description>" << xml_escape(desc) << "</Description>\n"
            << "    <References>\n"
            << "      <Reference ReferenceType=\"HasComponent\" IsForward=\"false\">"
            << xml_escape(parent_id) << "</Reference>\n"
            << "      <Reference ReferenceType=\"HasTypeDefinition\">i=63</Reference>\n"
            << "    </References>\n"
            << "  </UAVariable>\n";
    }

    out << "</UANodeSet>\n";
    return out.str();
}

}  // namespace opc::project
