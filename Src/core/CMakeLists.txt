add_library(opc_core
  tag_store.cpp
  translator.cpp
  dispatcher.cpp
  runtime_index.cpp
  runtime_doctor.cpp
)
add_library(opc::core ALIAS opc_core)

target_link_libraries(opc_core PUBLIC opc::project)
target_include_directories(opc_core PUBLIC
  "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/Src>"
)
opc_apply_project_options(opc_core)
