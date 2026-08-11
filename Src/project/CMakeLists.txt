add_library(opc_project
  load.cpp
  validate.cpp
  migrate_legacy.cpp
)
add_library(opc::project ALIAS opc_project)

target_include_directories(opc_project PUBLIC
  "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/Src>"
  "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/Lib/Json>"
)
opc_apply_project_options(opc_project)
