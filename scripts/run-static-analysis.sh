#!/usr/bin/env bash
set -euo pipefail

for tool in clang++ clang-tidy cppcheck; do
  if ! command -v "${tool}" >/dev/null 2>&1; then
    echo "${tool} is required" >&2
    exit 2
  fi
done

cmake --preset static-analysis
cmake --build --preset static-analysis
