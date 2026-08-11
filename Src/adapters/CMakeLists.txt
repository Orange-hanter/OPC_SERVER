add_library(opc_adapters
  modbus_tcp_transport.cpp
  opc_ua_server.cpp
)
add_library(opc::adapters ALIAS opc_adapters)

target_link_libraries(opc_adapters
  PUBLIC opc::core
  PRIVATE "${OPC_OPEN62541_TARGET}"
)
target_include_directories(opc_adapters PUBLIC
  "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/Src>"
)
opc_apply_project_options(opc_adapters)
