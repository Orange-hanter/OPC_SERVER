file(WRITE "${CMAKE_BINARY_DIR}/generated/app/version.hpp"
"#pragma once
#define OPC_SERVER_VERSION_STRING \"${OPC_SERVER_VERSION}\"
")

add_subdirectory(project)
add_subdirectory(core)
add_subdirectory(adapters)
add_subdirectory(app)

add_executable(OPC_SERVER main.cpp)
target_link_libraries(OPC_SERVER PRIVATE opc::app)
target_include_directories(OPC_SERVER PRIVATE "${CMAKE_BINARY_DIR}/generated")
opc_apply_project_options(OPC_SERVER)

install(TARGETS OPC_SERVER
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  COMPONENT opc-server
)
