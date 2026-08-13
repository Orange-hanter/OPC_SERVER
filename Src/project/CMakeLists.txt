add_library(opc_project
  load.cpp
  validate.cpp
  schema.cpp
  migrate_legacy.cpp
  doctor.cpp
)
add_library(opc::project ALIAS opc_project)

# load.cpp and validate.cpp intentionally have same-named anonymous-namespace
# helpers, so they cannot share one unity translation unit.
set_source_files_properties(validate.cpp PROPERTIES
  SKIP_UNITY_BUILD_INCLUSION ON
)

file(READ "${PROJECT_SOURCE_DIR}/DOCs/schemas/modbus-project.schema.json" OPC_MODBUS_PROJECT_SCHEMA)
configure_file(
  "${PROJECT_SOURCE_DIR}/cmake/modbus_project_schema.hpp.in"
  "${CMAKE_BINARY_DIR}/generated/project/schema_json.hpp"
  @ONLY
)

target_link_libraries(opc_project PUBLIC nlohmann_json_schema_validator)
target_include_directories(opc_project PUBLIC
  "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/Src>"
  "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/Lib/Json>"
  "$<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated>"
)
opc_apply_project_options(opc_project)
