add_subdirectory(project)
add_subdirectory(core)
add_subdirectory(adapters)
add_subdirectory(app)

add_executable(OPC_SERVER main.cpp)
target_link_libraries(OPC_SERVER PRIVATE opc::app)
opc_apply_project_options(OPC_SERVER)

install(TARGETS OPC_SERVER
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  COMPONENT Runtime
)
