include(CheckIPOSupported)

function(opc_configure_project_options)
  add_library(opc_project_options INTERFACE)
  add_library(opc::project_options ALIAS opc_project_options)

  target_compile_features(opc_project_options INTERFACE "cxx_std_${OPC_CXX_STANDARD}")

  if(OPC_ENABLE_WARNINGS)
    target_compile_options(opc_project_options INTERFACE
      "$<$<CXX_COMPILER_ID:MSVC>:/W4;/permissive->"
      "$<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Wsign-conversion>"
    )
  endif()

  if(OPC_WARNINGS_AS_ERRORS)
    target_compile_options(opc_project_options INTERFACE
      "$<$<CXX_COMPILER_ID:MSVC>:/WX>"
      "$<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Werror>"
    )
  endif()

  if(OPC_ENABLE_SANITIZERS AND OPC_ENABLE_TSAN)
    message(FATAL_ERROR "OPC_ENABLE_SANITIZERS and OPC_ENABLE_TSAN cannot be combined")
  endif()

  if(OPC_ENABLE_SANITIZERS)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
      target_compile_options(opc_project_options INTERFACE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
      )
      target_link_options(opc_project_options INTERFACE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
      )
    else()
      message(FATAL_ERROR
        "OPC_ENABLE_SANITIZERS currently supports GCC and Clang only.")
    endif()
  endif()

  if(OPC_ENABLE_TSAN)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
      target_compile_options(opc_project_options INTERFACE
        -fsanitize=thread
        -fno-omit-frame-pointer
      )
      target_link_options(opc_project_options INTERFACE
        -fsanitize=thread
        -fno-omit-frame-pointer
      )
    else()
      message(FATAL_ERROR "OPC_ENABLE_TSAN currently supports GCC and Clang only.")
    endif()
  endif()

  if(OPC_ENABLE_COVERAGE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
      target_compile_options(opc_project_options INTERFACE --coverage -O0 -g)
      target_link_options(opc_project_options INTERFACE --coverage)
    else()
      message(FATAL_ERROR "OPC_ENABLE_COVERAGE currently supports GCC and Clang only.")
    endif()
  endif()

  if(OPC_ENABLE_IPO)
    check_ipo_supported(RESULT _opc_ipo_supported OUTPUT _opc_ipo_error)
    if(NOT _opc_ipo_supported)
      message(FATAL_ERROR "IPO was requested but is unavailable: ${_opc_ipo_error}")
    endif()
  endif()

  if(OPC_ENABLE_CLANG_TIDY)
    find_program(OPC_CLANG_TIDY_EXECUTABLE NAMES clang-tidy REQUIRED)
  endif()
endfunction()

function(opc_apply_project_options target)
  target_link_libraries("${target}" PRIVATE opc::project_options)

  set_target_properties("${target}" PROPERTIES
    CXX_EXTENSIONS OFF
    INTERPROCEDURAL_OPTIMIZATION "${OPC_ENABLE_IPO}"
    UNITY_BUILD "${OPC_ENABLE_UNITY_BUILD}"
  )

  if(OPC_ENABLE_PCH)
    target_precompile_headers("${target}" PRIVATE
      <chrono>
      <cstdint>
      <expected>
      <memory>
      <string>
      <vector>
    )
  endif()

  if(OPC_ENABLE_CLANG_TIDY)
    set_property(TARGET "${target}" PROPERTY CXX_CLANG_TIDY
      "${OPC_CLANG_TIDY_EXECUTABLE};--use-color")
  endif()
endfunction()
