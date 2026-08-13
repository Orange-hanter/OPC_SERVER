cmake_minimum_required(VERSION 3.28)

set(OPC_SERVER_VERSION "0.1.0" CACHE STRING "OPC_SERVER package version")
project(OPC_SERVER VERSION ${OPC_SERVER_VERSION} LANGUAGES CXX C)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(PreventInSourceBuilds)
include(CTest)
include(GNUInstallDirs)
include(CompilerSupport)
include(ProjectOptions)
include(Dependencies)

option(OPC_BUILD_TOOLS "Build engineering command-line tools" ON)
option(OPC_ENABLE_WARNINGS "Enable a strict compiler warning set" ON)
option(OPC_WARNINGS_AS_ERRORS "Treat project warnings as errors" OFF)
option(OPC_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(OPC_ENABLE_TSAN "Enable ThreadSanitizer" OFF)
option(OPC_ENABLE_COVERAGE "Enable gcov/llvm coverage instrumentation" OFF)
option(OPC_ENABLE_FUZZERS "Build libFuzzer targets (Clang)" OFF)
option(OPC_ENABLE_IPO "Enable interprocedural optimization when supported" OFF)
option(OPC_ENABLE_UNITY_BUILD "Batch project sources into unity translation units" OFF)
option(OPC_ENABLE_PCH "Use a precompiled header for project targets" OFF)
option(OPC_ENABLE_CLANG_TIDY "Run clang-tidy while compiling project targets" OFF)
option(OPC_WITH_OTLP "Enable OpenTelemetry OTLP/HTTP metrics exporter" OFF)

set(OPC_DEPENDENCY_PROVIDER "AUTO" CACHE STRING
  "Dependency provider: AUTO, CONAN, or FETCHCONTENT")
set_property(CACHE OPC_DEPENDENCY_PROVIDER PROPERTY STRINGS AUTO CONAN FETCHCONTENT)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

opc_detect_cxx_standard(OPC_CXX_STANDARD)
set(CMAKE_CXX_STANDARD ${OPC_CXX_STANDARD})
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
opc_configure_project_options()
opc_setup_dependencies()

add_subdirectory(Src)

if(OPC_BUILD_TOOLS)
  add_subdirectory(tools)
endif()

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()

install(FILES
  "${PROJECT_SOURCE_DIR}/DOCs/examples/demo-plant.modbusproj.json"
  "${PROJECT_SOURCE_DIR}/DOCs/schemas/modbus-project.schema.json"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/opc-server/examples"
  COMPONENT opc-server
)

include(Packaging)

message(STATUS "OPC_SERVER ${PROJECT_VERSION}: C++${OPC_CXX_STANDARD}, dependencies=${OPC_RESOLVED_DEPENDENCY_PROVIDER}")
