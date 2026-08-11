add_executable(opc-map main.cpp)
target_link_libraries(opc-map PRIVATE opc::project)
opc_apply_project_options(opc-map)

install(TARGETS opc-map
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  COMPONENT Runtime
)
