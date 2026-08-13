include(CheckCXXSourceCompiles)
include(CMakePushCheckState)

function(_opc_check_expected_with_standard standard result)
  cmake_push_check_state(RESET)
  if(MSVC)
    set(CMAKE_REQUIRED_FLAGS "/std:c++latest")
  else()
    set(CMAKE_REQUIRED_FLAGS "-std=c++${standard}")
  endif()

  check_cxx_source_compiles([=[
    #include <expected>
    int main() {
      std::expected<int, int> value{42};
      return *value == 42 ? 0 : 1;
    }
  ]=] "${result}")
  cmake_pop_check_state()

  set("${result}" "${${result}}" PARENT_SCOPE)
endfunction()

function(opc_detect_cxx_standard output_variable)
  if(MSVC)
    # MSVC exposes the latest draft through /std:c++latest; CMake's numeric
    # CXX_STANDARD remains 23 until a portable C++26 mode is available.
    _opc_check_expected_with_standard(23 OPC_HAS_STD_EXPECTED_CXX23)
    if(NOT OPC_HAS_STD_EXPECTED_CXX23)
      message(FATAL_ERROR
        "OPC_SERVER requires std::expected and a C++23-capable standard library.")
    endif()
    set("${output_variable}" 23 PARENT_SCOPE)
    return()
  endif()

  _opc_check_expected_with_standard(26 OPC_HAS_STD_EXPECTED_CXX26)
  if(OPC_HAS_STD_EXPECTED_CXX26)
    set("${output_variable}" 26 PARENT_SCOPE)
    return()
  endif()

  _opc_check_expected_with_standard(23 OPC_HAS_STD_EXPECTED_CXX23)
  if(NOT OPC_HAS_STD_EXPECTED_CXX23)
    message(FATAL_ERROR
      "OPC_SERVER requires std::expected and a C++23-capable standard library. "
      "With Clang, select a recent libstdc++ using -DCMAKE_CXX_FLAGS=-stdlib=libstdc++.")
  endif()

  message(WARNING
    "C++26 with std::expected is unavailable; using C++23. "
    "The project's target language level remains C++26.")
  set("${output_variable}" 23 PARENT_SCOPE)
endfunction()
