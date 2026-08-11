cmake_minimum_required(VERSION 3.28)

# Allow release workflow to inject SemVer without editing this file.
set(OPC_SERVER_VERSION "0.1.0" CACHE STRING "OPC_SERVER package version")
project(OPC_SERVER VERSION ${OPC_SERVER_VERSION} LANGUAGES CXX)

include(CheckCXXCompilerFlag)
include(FetchContent)
include(CTest)
include(GNUInstallDirs)

check_cxx_compiler_flag("-std=c++26" OPC_HAS_CXX26)
if(OPC_HAS_CXX26)
  set(OPC_CXX_STANDARD 26)
else()
  set(OPC_CXX_STANDARD 23)
  message(WARNING "C++26 is not supported by this compiler; building with C++${OPC_CXX_STANDARD}. Target remains C++26.")
endif()

set(CMAKE_CXX_STANDARD ${OPC_CXX_STANDARD})
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(opc_project
  Src/project/load.cpp
  Src/project/validate.cpp
  Src/project/migrate_legacy.cpp
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
)
add_library(opc::adapters ALIAS opc_adapters)
target_link_libraries(opc_adapters PUBLIC opc::core)
target_include_directories(opc_adapters PUBLIC ${CMAKE_SOURCE_DIR}/Src)

add_library(opc_app
  Src/app/cli_options.cpp
  Src/app/server_runtime.cpp
  Src/app/application.cpp
)
add_library(opc::app ALIAS opc_app)
target_link_libraries(opc_app PUBLIC opc::adapters opc::core opc::project)
target_include_directories(opc_app PUBLIC ${CMAKE_SOURCE_DIR}/Src)

if(OPC_CXX_STANDARD EQUAL 23)
  target_compile_features(opc_project PUBLIC cxx_std_23)
  target_compile_features(opc_core PUBLIC cxx_std_23)
  target_compile_features(opc_adapters PUBLIC cxx_std_23)
  target_compile_features(opc_app PUBLIC cxx_std_23)
endif()

add_executable(OPC_SERVER
  Src/main.cpp
)
target_link_libraries(OPC_SERVER PRIVATE opc::app)

add_executable(opc-map
  tools/opc-map/main.cpp
)
target_link_libraries(opc-map PRIVATE opc::project)

install(TARGETS OPC_SERVER opc-map
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES
  ${CMAKE_SOURCE_DIR}/DOCs/examples/demo-plant.modbusproj.json
  ${CMAKE_SOURCE_DIR}/DOCs/schemas/modbus-project.schema.json
  DESTINATION ${CMAKE_INSTALL_DATADIR}/opc-server/examples
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
    tests/test_tag_store.cpp
    tests/test_fake_modbus.cpp
    tests/test_translator.cpp
    tests/test_dispatcher.cpp
    tests/test_server_runtime.cpp
  )
  target_link_libraries(opc_tests PRIVATE opc::app opc::adapters opc::core opc::project Catch2::Catch2WithMain)
  target_compile_definitions(opc_tests PRIVATE OPC_SERVER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

  include(Catch)
  catch_discover_tests(opc_tests)
endif()
