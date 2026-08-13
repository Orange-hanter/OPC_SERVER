include(FetchContent)

set(_OPC_OPEN62541_VERSION "1.4.11")
set(_OPC_CATCH2_VERSION "3.7.1")

function(_opc_find_open62541_target output_variable)
  foreach(candidate IN ITEMS open62541::open62541 open62541)
    if(TARGET "${candidate}")
      set("${output_variable}" "${candidate}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  set("${output_variable}" "" PARENT_SCOPE)
endfunction()

function(_opc_fetch_open62541)
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

  FetchContent_Declare(open62541
    GIT_REPOSITORY https://github.com/open62541/open62541.git
    GIT_TAG "v${_OPC_OPEN62541_VERSION}"
    GIT_SHALLOW TRUE
    SYSTEM
    EXCLUDE_FROM_ALL
  )
  FetchContent_MakeAvailable(open62541)
endfunction()

function(_opc_fetch_catch2)
  FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG "v${_OPC_CATCH2_VERSION}"
    GIT_SHALLOW TRUE
    SYSTEM
    EXCLUDE_FROM_ALL
  )
  FetchContent_MakeAvailable(Catch2)
endfunction()

function(_opc_fetch_sqlite)
  FetchContent_Declare(sqlite3
    URL https://www.sqlite.org/2024/sqlite-amalgamation-3460100.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )
  FetchContent_GetProperties(sqlite3)
  if(NOT sqlite3_POPULATED)
    FetchContent_Populate(sqlite3)
  endif()
  add_library(opc_sqlite3 STATIC "${sqlite3_SOURCE_DIR}/sqlite3.c")
  add_library(SQLite::SQLite3 ALIAS opc_sqlite3)
  target_include_directories(opc_sqlite3 PUBLIC "${sqlite3_SOURCE_DIR}")
  target_compile_definitions(opc_sqlite3 PUBLIC SQLITE_THREADSAFE=1)
  find_package(Threads QUIET)
  if(Threads_FOUND)
    target_link_libraries(opc_sqlite3 PUBLIC Threads::Threads)
  endif()
  if(UNIX)
    target_link_libraries(opc_sqlite3 PUBLIC ${CMAKE_DL_LIBS} m)
  endif()
  set_target_properties(opc_sqlite3 PROPERTIES
    C_STANDARD 11
    POSITION_INDEPENDENT_CODE ON
  )
endfunction()

function(_opc_fetch_observability)
  set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
  set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.15.1
    GIT_SHALLOW TRUE
    SYSTEM
    EXCLUDE_FROM_ALL
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
  FetchContent_Declare(opentelemetry-cpp
    GIT_REPOSITORY https://github.com/open-telemetry/opentelemetry-cpp.git
    GIT_TAG v1.20.0
    GIT_SHALLOW TRUE
    SYSTEM
    EXCLUDE_FROM_ALL
  )
  FetchContent_MakeAvailable(opentelemetry-cpp)
  set(BUILD_TESTING "${_OPC_SAVED_BUILD_TESTING}")
  if(BUILD_TESTING)
    enable_testing()
  endif()
endfunction()

function(opc_setup_dependencies)
  string(TOUPPER "${OPC_DEPENDENCY_PROVIDER}" _provider)
  set(_OPC_VALID_PROVIDERS AUTO CONAN FETCHCONTENT)
  if(NOT _provider IN_LIST _OPC_VALID_PROVIDERS)
    message(FATAL_ERROR
      "Unknown OPC_DEPENDENCY_PROVIDER='${OPC_DEPENDENCY_PROVIDER}'. "
      "Choose AUTO, CONAN, or FETCHCONTENT.")
  endif()

  if(NOT _provider STREQUAL "FETCHCONTENT")
    if(_provider STREQUAL "CONAN")
      find_package(open62541 CONFIG REQUIRED)
    else()
      find_package(open62541 CONFIG QUIET)
    endif()
    _opc_find_open62541_target(_open62541_target)
  endif()

  if(NOT _open62541_target)
    if(_provider STREQUAL "CONAN")
      message(FATAL_ERROR
        "Conan's open62541 package did not define a supported CMake target.")
    endif()
    _opc_fetch_open62541()
    _opc_find_open62541_target(_open62541_target)
  endif()

  if(BUILD_TESTING)
    if(NOT _provider STREQUAL "FETCHCONTENT")
      if(_provider STREQUAL "CONAN")
        find_package(Catch2 3 CONFIG REQUIRED)
      else()
        find_package(Catch2 3 CONFIG QUIET)
      endif()
    endif()
    if(NOT TARGET Catch2::Catch2WithMain)
      if(_provider STREQUAL "CONAN")
        message(FATAL_ERROR
          "Conan's Catch2 package did not define Catch2::Catch2WithMain.")
      endif()
      _opc_fetch_catch2()
      FetchContent_GetProperties(Catch2 SOURCE_DIR _catch2_source_dir)
      set(_catch2_discovery_module "${_catch2_source_dir}/extras/Catch.cmake")
    endif()
  endif()

  if(NOT _open62541_target)
    message(FATAL_ERROR "No usable open62541 CMake target was found.")
  endif()

  find_package(SQLite3 QUIET)
  if(NOT SQLite3_FOUND)
    _opc_fetch_sqlite()
  endif()
  _opc_fetch_observability()

  set(OPC_OPEN62541_TARGET "${_open62541_target}" PARENT_SCOPE)
  set(OPC_CATCH_DISCOVERY_MODULE "${_catch2_discovery_module}" PARENT_SCOPE)
  set(OPC_RESOLVED_DEPENDENCY_PROVIDER "${_provider}" PARENT_SCOPE)
endfunction()
