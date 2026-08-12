cmake_minimum_required(VERSION 3.28)

# Allow release workflow to inject SemVer without editing this file.
set(OPC_SERVER_VERSION "0.1.0" CACHE STRING "OPC_SERVER package version")
project(OPC_SERVER VERSION ${OPC_SERVER_VERSION} LANGUAGES CXX C)

include(CheckCXXCompilerFlag)
include(CheckCXXSourceCompiles)
include(FetchContent)
include(CTest)
include(GNUInstallDirs)

check_cxx_compiler_flag("-std=c++26" OPC_HAS_CXX26_FLAG)

set(_OPC_SAVED_CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS}")
set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -std=c++26")
check_cxx_source_compiles("
#include <expected>
int main() {
  std::expected<int, int> value{1};
  return *value;
}
" OPC_HAS_STD_EXPECTED_CXX26)
set(CMAKE_REQUIRED_FLAGS "${_OPC_SAVED_CMAKE_REQUIRED_FLAGS}")

if(OPC_HAS_CXX26_FLAG AND OPC_HAS_STD_EXPECTED_CXX26)
  set(OPC_CXX_STANDARD 26)
else()
  set(OPC_CXX_STANDARD 23)
  message(WARNING "C++26 with std::expected is not available; building with C++${OPC_CXX_STANDARD}. Target remains C++26.")
endif()

set(_OPC_SAVED_CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS}")
set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -std=c++23")
check_cxx_source_compiles("
#include <expected>
int main() {
  std::expected<int, int> value{1};
  return *value;
}
" OPC_HAS_STD_EXPECTED_CXX23)
set(CMAKE_REQUIRED_FLAGS "${_OPC_SAVED_CMAKE_REQUIRED_FLAGS}")

if(NOT OPC_HAS_STD_EXPECTED_CXX23)
  message(FATAL_ERROR
    "std::expected is required (C++23). With Clang, configure with -DCMAKE_CXX_FLAGS=-stdlib=libstdc++ "
    "and ensure a recent libstdc++ is installed.")
endif()

set(CMAKE_CXX_STANDARD ${OPC_CXX_STANDARD})
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

configure_file(
  ${CMAKE_SOURCE_DIR}/Src/app/version.hpp.in
  ${CMAKE_BINARY_DIR}/generated/app/version.hpp
  @ONLY
)
# Map cache version into the generated header content.
file(WRITE ${CMAKE_BINARY_DIR}/generated/app/version.hpp
"#pragma once
#define OPC_SERVER_VERSION_STRING \"${OPC_SERVER_VERSION}\"
")

# --- Observability deps (spdlog + OpenTelemetry) ---
option(OPC_WITH_OTLP "Enable OpenTelemetry OTLP/HTTP metrics exporter" OFF)

set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG        v1.15.1
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(spdlog)

set(_OPC_SAVED_BUILD_TESTING "${BUILD_TESTING}")
set(BUILD_TESTING OFF)
set(WITH_OTLP_GRPC OFF CACHE BOOL "" FORCE)
set(WITH_OTLP_HTTP ${OPC_WITH_OTLP} CACHE BOOL "" FORCE)
set(WITH_OTLP_FILE OFF CACHE BOOL "" FORCE)
set(WITH_PROMETHEUS OFF CACHE BOOL "" FORCE)
set(WITH_ZIPKIN OFF CACHE BOOL "" FORCE)
set(WITH_ELASTICSEARCH OFF CACHE BOOL "" FORCE)
set(WITH_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WITH_BENCHMARK OFF CACHE BOOL "" FORCE)
set(WITH_FUNC_TESTS OFF CACHE BOOL "" FORCE)
set(WITH_STL ON CACHE BOOL "" FORCE)
set(WITH_ABSEIL OFF CACHE BOOL "" FORCE)
set(OPENTELEMETRY_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  opentelemetry-cpp
  GIT_REPOSITORY https://github.com/open-telemetry/opentelemetry-cpp.git
  GIT_TAG        v1.20.0
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(opentelemetry-cpp)
set(BUILD_TESTING "${_OPC_SAVED_BUILD_TESTING}")
if(BUILD_TESTING)
  enable_testing()
endif()

# --- open62541 (OPC UA) ---
set(UA_ENABLE_AMALGAMATION OFF CACHE BOOL "" FORCE)
set(UA_ENABLE_ENCRYPTION OFF CACHE BOOL "" FORCE)
set(UA_ENABLE_HISTORIZING OFF CACHE BOOL "" FORCE)
set(UA_ENABLE_PUBSUB OFF CACHE BOOL "" FORCE)
set(UA_ENABLE_PUBSUB_INFORMATIONMODEL OFF CACHE BOOL "" FORCE)
set(UA_ENABLE_DEBUG_SANITIZER OFF CACHE BOOL "" FORCE)
set(UA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(UA_BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(UA_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(UA_NAMESPACE_ZERO "REDUCED" CACHE STRING "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  open62541
  GIT_REPOSITORY https://github.com/open62541/open62541.git
  GIT_TAG        v1.4.11
  GIT_SHALLOW    TRUE
)
FetchContent_GetProperties(open62541)
if(NOT open62541_POPULATED)
  FetchContent_Populate(open62541)
  # Keep open62541 out of `cmake --install` prefix (binaries/examples only).
  add_subdirectory(${open62541_SOURCE_DIR} ${open62541_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()

add_library(opc_project
  Src/project/load.cpp
  Src/project/validate.cpp
  Src/project/migrate_legacy.cpp
  Src/project/doctor.cpp
)
add_library(opc::project ALIAS opc_project)
target_include_directories(opc_project
  PUBLIC
    ${CMAKE_SOURCE_DIR}/Src
    ${CMAKE_SOURCE_DIR}/Lib/Json
)

add_library(opc_core
  Src/core/tag_store.cpp
  Src/core/translator.cpp
  Src/core/dispatcher.cpp
  Src/core/runtime_index.cpp
)
add_library(opc::core ALIAS opc_core)
target_link_libraries(opc_core PUBLIC opc::project)
target_include_directories(opc_core PUBLIC ${CMAKE_SOURCE_DIR}/Src)

add_library(opc_adapters
  Src/adapters/modbus_tcp_transport.cpp
  Src/adapters/opc_ua_server.cpp
  Src/adapters/ring_historian.cpp
  Src/adapters/sqlite_historian.cpp
  Src/adapters/frame_log.cpp
  Src/adapters/frame_replay.cpp
  Src/adapters/memory_metrics.cpp
  Src/adapters/spdlog_log.cpp
  Src/adapters/otel_metrics.cpp
)
add_library(opc::adapters ALIAS opc_adapters)
find_package(SQLite3 REQUIRED)
target_link_libraries(opc_adapters
  PUBLIC
    opc::project
    open62541
    SQLite::SQLite3
    spdlog::spdlog
    opentelemetry_metrics
    opentelemetry_exporter_ostream_metrics
    opentelemetry_resources
    opentelemetry_common
)
if(OPC_WITH_OTLP)
  target_compile_definitions(opc_adapters PUBLIC OPC_WITH_OTLP=1)
  target_link_libraries(opc_adapters PUBLIC opentelemetry_exporter_otlp_http_metric)
endif()
target_include_directories(opc_adapters PUBLIC ${CMAKE_SOURCE_DIR}/Src)

add_library(opc_app
  Src/app/cli_options.cpp
  Src/app/server_runtime.cpp
  Src/app/application.cpp
)
add_library(opc::app ALIAS opc_app)
target_link_libraries(opc_app PUBLIC opc::adapters opc::core opc::project)
target_include_directories(opc_app
  PUBLIC
    ${CMAKE_SOURCE_DIR}/Src
    ${CMAKE_BINARY_DIR}/generated
)

add_executable(OPC_SERVER
  Src/main.cpp
)
target_link_libraries(OPC_SERVER PRIVATE opc::app)
target_include_directories(OPC_SERVER PRIVATE ${CMAKE_BINARY_DIR}/generated)

add_executable(opc-map
  tools/opc-map/main.cpp
)
target_link_libraries(opc-map PRIVATE opc::project)

foreach(_opc_tgt opc_project opc_core opc_adapters opc_app OPC_SERVER opc-map)
  target_compile_options(${_opc_tgt} PRIVATE -Wall -Wextra -Wpedantic)
endforeach()

install(TARGETS OPC_SERVER opc-map
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  COMPONENT opc-server
)

install(FILES
  ${CMAKE_SOURCE_DIR}/DOCs/examples/demo-plant.modbusproj.json
  ${CMAKE_SOURCE_DIR}/DOCs/schemas/modbus-project.schema.json
  DESTINATION ${CMAKE_INSTALL_DATADIR}/opc-server/examples
  COMPONENT opc-server
)

if(BUILD_TESTING)
  FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.7.1
  )
  FetchContent_MakeAvailable(Catch2)

  add_executable(opc_tests
    tests/test_project.cpp
    tests/test_doctor.cpp
    tests/test_tag_store.cpp
    tests/test_fake_modbus.cpp
    tests/test_translator.cpp
    tests/test_dispatcher.cpp
    tests/test_server_runtime.cpp
    tests/test_opc_ua_read.cpp
    tests/test_opc_ua_write_subs.cpp
    tests/test_historian.cpp
    tests/test_frame_log.cpp
    tests/test_spdlog_otel.cpp
    tests/test_frame_replay.cpp
  )
  target_link_libraries(opc_tests PRIVATE opc::app opc::adapters opc::core opc::project Catch2::Catch2WithMain open62541)
  target_include_directories(opc_tests PRIVATE ${CMAKE_BINARY_DIR}/generated)
  target_compile_definitions(opc_tests PRIVATE OPC_SERVER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  target_compile_options(opc_tests PRIVATE -Wall -Wextra -Wpedantic)

  include(Catch)
  catch_discover_tests(opc_tests)
endif()
