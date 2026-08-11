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
    endif()
  endif()

  if(NOT _open62541_target)
    message(FATAL_ERROR "No usable open62541 CMake target was found.")
  endif()

  set(OPC_OPEN62541_TARGET "${_open62541_target}" PARENT_SCOPE)
  set(OPC_RESOLVED_DEPENDENCY_PROVIDER "${_provider}" PARENT_SCOPE)
endfunction()
