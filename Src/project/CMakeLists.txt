add_library(opc_project
  load.cpp
  validate.cpp
  migrate_legacy.cpp
)
add_library(opc::project ALIAS opc_project)

# load.cpp and validate.cpp intentionally have same-named anonymous-namespace
# helpers, so they cannot share one unity translation unit.
set_source_files_properties(validate.cpp PROPERTIES
  SKIP_UNITY_BUILD_INCLUSION ON
)

target_include_directories(opc_project PUBLIC
  "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/Src>"
  "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/Lib/Json>"
)
opc_apply_project_options(opc_project)
