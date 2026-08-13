add_library(opc_monitor_client
  monitor_client.cpp
)
add_library(opc::monitor_client ALIAS opc_monitor_client)

target_include_directories(opc_monitor_client PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
  "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/Lib/Json>"
)
target_link_libraries(opc_monitor_client PRIVATE
  "${OPC_OPEN62541_TARGET}"
)
opc_apply_project_options(opc_monitor_client)

add_executable(opc-monitor main.cpp)
target_link_libraries(opc-monitor PRIVATE opc::monitor_client)
opc_apply_project_options(opc-monitor)

install(TARGETS opc-monitor
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  COMPONENT opc-server
)
