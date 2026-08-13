add_library(opc_app
  cli_options.cpp
  server_runtime.cpp
  application.cpp
)
add_library(opc::app ALIAS opc_app)

target_link_libraries(opc_app PUBLIC
  opc::adapters
  opc::core
  opc::project
)
target_include_directories(opc_app PUBLIC
  "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/Src>"
  "$<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated>"
)
opc_apply_project_options(opc_app)
