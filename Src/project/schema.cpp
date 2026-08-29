#include "project/schema.hpp"
#include "project/schema_json.hpp"

#include <nlohmann/json-schema.hpp>

#include <string>
#include <string_view>

namespace opc::project {
namespace {

using json = nlohmann::json;

void rewrite_refs(json& node) {
    if (node.is_object()) {
        if (auto it = node.find("$ref"); it != node.end() && it->is_string()) {
            auto ref = it->get<std::string>();
            constexpr std::string_view kFrom = "#/$defs/";
            constexpr std::string_view kTo = "#/definitions/";
            if (ref.starts_with(kFrom)) {
                ref.replace(0, kFrom.size(), kTo);
                *it = std::move(ref);
            }
        }
        for (auto& [key, child] : node.items()) {
            (void)key;
            rewrite_refs(child);
        }
    } else if (node.is_array()) {
        for (auto& child : node) {
            rewrite_refs(child);
        }
    }
}

json draft7_schema() {
    json schema = json::parse(kEmbeddedModbusProjectSchema);
    schema["$schema"] = "http://json-schema.org/draft-07/schema#";
    if (schema.contains("$defs")) {
        schema["definitions"] = schema["$defs"];
        schema.erase("$defs");
    }
    rewrite_refs(schema);
    return schema;
}

class DiagnosticErrorHandler : public nlohmann::json_schema::basic_error_handler {
public:
    DiagnosticErrorHandler(std::string_view source, std::vector<Diagnostic>& out)
        : source_(source), out_(out) {}

    void error(const json::json_pointer& pointer,
               const json&,
               const std::string& message) override {
        std::string path = source_;
        const auto ptr = pointer.to_string();
        if (!ptr.empty()) {
            path += ptr;
        }
        out_.push_back(Diagnostic{Diagnostic::Severity::Error, std::move(path),
                                  "json schema: " + message});
    }

private:
    std::string source_;
    std::vector<Diagnostic>& out_;
};

}  // namespace

void append_json_schema_diagnostics(const nlohmann::json& instance,
                                    std::string_view source_name,
                                    std::vector<Diagnostic>& diagnostics) {
    try {
        static const json schema = draft7_schema();
        nlohmann::json_schema::json_validator validator;
        validator.set_root_schema(schema);
        DiagnosticErrorHandler handler{source_name, diagnostics};
        validator.validate(instance, handler);
    } catch (const std::exception& ex) {
        diagnostics.push_back(Diagnostic{Diagnostic::Severity::Error, std::string(source_name),
                                         std::string("json schema engine: ") + ex.what()});
    }
}

}  // namespace opc::project
