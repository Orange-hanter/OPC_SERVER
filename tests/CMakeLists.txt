add_executable(opc_tests
  unit/test_translator.cpp
  unit/test_translator_property.cpp
  unit/test_tag_store.cpp
  unit/test_tag_store_concurrent.cpp
  unit/test_quality_status.cpp
  unit/test_runtime_index.cpp
  component/test_dispatcher.cpp
  component/test_dispatcher_faults.cpp
  component/test_fake_modbus.cpp
  component/test_server_runtime.cpp
  component/test_historian.cpp
  component/test_frame_log.cpp
  component/test_frame_replay.cpp
  component/test_spdlog_otel.cpp
  contract/test_project.cpp
  contract/test_doctor.cpp
  contract/test_fixtures.cpp
  contract/test_opc_map_cli.cpp
  integration/test_opc_ua_read.cpp
  integration/test_opc_ua_write_subs.cpp
  integration/test_modbus_tcp.cpp
  lab/test_e2e_mvp.cpp
  lab/test_soak.cpp
)

if(TARGET opc::monitor_client)
  target_sources(opc_tests PRIVATE integration/test_opc_monitor.cpp)
endif()

find_package(Threads REQUIRED)

target_link_libraries(opc_tests PRIVATE
  opc::app
  opc::adapters
  opc::core
  opc::project
  Catch2::Catch2WithMain
  Threads::Threads
  "${OPC_OPEN62541_TARGET}"
)
if(TARGET opc::monitor_client)
  target_link_libraries(opc_tests PRIVATE opc::monitor_client)
endif()
target_include_directories(opc_tests PRIVATE
  "${PROJECT_SOURCE_DIR}/tests"
  "${CMAKE_BINARY_DIR}/generated"
)
target_compile_definitions(opc_tests PRIVATE
  OPC_SERVER_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)
if(TARGET opc-map)
  target_compile_definitions(opc_tests PRIVATE
    OPC_MAP_EXECUTABLE="$<TARGET_FILE:opc-map>"
  )
endif()
if(TARGET OPC_SERVER)
  target_compile_definitions(opc_tests PRIVATE
    OPC_SERVER_EXECUTABLE="$<TARGET_FILE:OPC_SERVER>"
  )
endif()
opc_apply_project_options(opc_tests)
# Static analyzers target production code. Catch2 assertion macros intentionally
# model control flow that clang-tidy cannot infer, producing optional-access and
# enum-cast false positives in test translation units. Compiler -Werror still
# applies to all tests.
if(OPC_ENABLE_CLANG_TIDY)
  set_property(TARGET opc_tests PROPERTY CXX_CLANG_TIDY "")
endif()
if(OPC_ENABLE_CPPCHECK)
  set_property(TARGET opc_tests PROPERTY CXX_CPPCHECK "")
endif()

if(OPC_CATCH_DISCOVERY_MODULE)
  include("${OPC_CATCH_DISCOVERY_MODULE}")
else()
  include(Catch)
endif()
catch_discover_tests(opc_tests)

add_test(NAME layer_lint
  COMMAND python3 "${PROJECT_SOURCE_DIR}/scripts/layer-lint.py"
)
set_tests_properties(layer_lint PROPERTIES
  WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
)

add_executable(opc_bench lab/bench_translator.cpp)
target_link_libraries(opc_bench PRIVATE opc::core Catch2::Catch2WithMain)
target_compile_definitions(opc_bench PRIVATE CATCH_CONFIG_ENABLE_BENCHMARKING)
opc_apply_project_options(opc_bench)
if(OPC_ENABLE_CLANG_TIDY)
  set_property(TARGET opc_bench PROPERTY CXX_CLANG_TIDY "")
endif()
if(OPC_ENABLE_CPPCHECK)
  set_property(TARGET opc_bench PROPERTY CXX_CPPCHECK "")
endif()

if(OPC_ENABLE_FUZZERS)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "OPC_ENABLE_FUZZERS requires Clang/libFuzzer")
  endif()
  add_executable(project_load_fuzzer "${PROJECT_SOURCE_DIR}/fuzz/project_load_fuzzer.cpp")
  target_link_libraries(project_load_fuzzer PRIVATE opc::project)
  target_compile_options(project_load_fuzzer PRIVATE -fsanitize=fuzzer,address -fno-omit-frame-pointer)
  target_link_options(project_load_fuzzer PRIVATE -fsanitize=fuzzer,address)
endif()
