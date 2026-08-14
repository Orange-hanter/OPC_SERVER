add_executable(opc_tests
  test_project.cpp
  test_doctor.cpp
  test_import_csv.cpp
  test_gen_nodeset.cpp
  test_runtime_doctor.cpp
  test_tag_store.cpp
  test_fake_modbus.cpp
  test_translator.cpp
  test_dispatcher.cpp
  test_server_runtime.cpp
  test_asio_reactor.cpp
  test_opc_ua_read.cpp
  test_opc_ua_write_subs.cpp
  test_historian.cpp
  test_frame_log.cpp
  test_spdlog_otel.cpp
    test_frame_replay.cpp
    test_modbus_udp.cpp
    test_modbus_tcp.cpp
    test_modbus_async.cpp
    test_ua_encryption.cpp
    test_ua_username.cpp
    test_ua_x509.cpp
    test_otlp_live.cpp
    test_load_stand.cpp
)

if(TARGET opc::monitor_client)
  target_sources(opc_tests PRIVATE test_opc_monitor.cpp)
endif()

target_link_libraries(opc_tests PRIVATE
  opc::app
  opc::adapters
  opc::core
  opc::project
  Catch2::Catch2WithMain
  "${OPC_OPEN62541_TARGET}"
)
if(TARGET opc::monitor_client)
  target_link_libraries(opc_tests PRIVATE opc::monitor_client)
endif()
target_include_directories(opc_tests PRIVATE
  "${CMAKE_BINARY_DIR}/generated"
)
target_compile_definitions(opc_tests PRIVATE
  OPC_SERVER_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)
opc_apply_project_options(opc_tests)

if(OPC_CATCH_DISCOVERY_MODULE)
  include("${OPC_CATCH_DISCOVERY_MODULE}")
else()
  include(Catch)
endif()
catch_discover_tests(opc_tests)
