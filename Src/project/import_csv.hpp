#pragma once

#include <json.hpp>

#include <cstdint>
#include <string>
#include <string_view>

namespace opc::project {

/// Options for turning a register CSV into a loadable draft project.
struct ImportCsvOptions {
    std::string project_name{"csv-import"};
    std::string device_id{"csv-device"};
    std::string endpoint_id{"csv-endpoint"};
    int unit_id{1};
    std::string group{"csv-imported"};
    std::string host{"127.0.0.1"};
    std::uint16_t port{502};
};

/// Parse CSV text (header required). Contract columns:
/// `name,area,address,type,byteOrder,scale,offset,unit,writable`.
/// Optional: `nodePath`, `group`, `description`, `quantity`.
nlohmann::json import_csv_text(std::string_view csv, const ImportCsvOptions& options = {});

/// Read a CSV file and return pretty-printed draft project JSON.
std::string import_csv_file_to_string(const std::string& csv_path,
                                      const ImportCsvOptions& options = {});

}  // namespace opc::project
