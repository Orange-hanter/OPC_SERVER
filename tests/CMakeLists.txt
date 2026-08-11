add_executable(opc_tests
  test_project.cpp
  test_tag_store.cpp
  test_fake_modbus.cpp
  test_translator.cpp
  test_dispatcher.cpp
  test_server_runtime.cpp
  test_opc_ua_read.cpp
)

target_link_libraries(opc_tests PRIVATE
  opc::app
  opc::adapters
  opc::core
  opc::project
  Catch2::Catch2WithMain
  "${OPC_OPEN62541_TARGET}"
)
target_compile_definitions(opc_tests PRIVATE
  OPC_SERVER_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)
opc_apply_project_options(opc_tests)

include(Catch)
catch_discover_tests(opc_tests)
