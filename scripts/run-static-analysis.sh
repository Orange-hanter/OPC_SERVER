#!/usr/bin/env bash
set -euo pipefail

if command -v clang++-19 >/dev/null 2>&1; then
  clang_c="clang-19"
  clang_cxx="clang++-19"
  clang_tidy="clang-tidy-19"
else
  clang_c="clang"
  clang_cxx="clang++"
  clang_tidy="clang-tidy"
fi

for tool in "${clang_c}" "${clang_cxx}" "${clang_tidy}" cppcheck; do
  command -v "${tool}" >/dev/null 2>&1 || {
    echo "${tool} is required" >&2
    exit 2
  }
done

if command -v g++-14 >/dev/null 2>&1; then
  libstdcpp_driver="g++-14"
elif command -v g++ >/dev/null 2>&1; then
  libstdcpp_driver="g++"
else
  echo "a recent g++/libstdc++ development package is required" >&2
  exit 2
fi

gcc_install_dir="$(dirname "$("${libstdcpp_driver}" -print-libgcc-file-name)")"
cmake --fresh --preset static-analysis \
  -DCMAKE_C_COMPILER="${clang_c}" \
  -DCMAKE_CXX_COMPILER="${clang_cxx}" \
  -DOPC_CLANG_TIDY_EXECUTABLE="$(command -v "${clang_tidy}")" \
  -DCMAKE_CXX_FLAGS="--gcc-install-dir=${gcc_install_dir}" \
  -DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=lld"
cmake --build --preset static-analysis
