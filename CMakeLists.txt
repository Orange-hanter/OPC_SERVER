cmake_minimum_required(VERSION 3.28)

project(OPC_SERVER VERSION 0.1.0 LANGUAGES CXX)

include(CheckCXXCompilerFlag)
include(FetchContent)
include(CTest)

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

if(OPC_CXX_STANDARD EQUAL 23)
  target_compile_features(opc_project PUBLIC cxx_std_23)
endif()

add_executable(OPC_SERVER
  Src/main.cpp
  Src/app.cpp
)
target_link_libraries(OPC_SERVER PRIVATE opc::project)
target_include_directories(OPC_SERVER PRIVATE ${CMAKE_SOURCE_DIR}/Src ${CMAKE_SOURCE_DIR}/Lib/Json)

add_executable(opc-map
  tools/opc-map/main.cpp
)
target_link_libraries(opc-map PRIVATE opc::project)

if(BUILD_TESTING)
  FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.7.1
  )
  FetchContent_MakeAvailable(Catch2)

  add_executable(opc_tests
    tests/test_project.cpp
  )
  target_link_libraries(opc_tests PRIVATE opc::project Catch2::Catch2WithMain)
  target_compile_definitions(opc_tests PRIVATE OPC_SERVER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

  include(Catch)
  catch_discover_tests(opc_tests)
endif()
