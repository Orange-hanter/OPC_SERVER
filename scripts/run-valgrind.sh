#!/usr/bin/env bash
set -euo pipefail

build_dir="${1:-build/valgrind}"
test_binary="${build_dir}/tests/opc_tests"

if ! command -v valgrind >/dev/null 2>&1; then
  echo "valgrind is required (Ubuntu: sudo apt-get install valgrind)" >&2
  exit 2
fi

if [[ ! -x "${test_binary}" ]]; then
  echo "missing ${test_binary}; run cmake --preset valgrind && cmake --build --preset valgrind" >&2
  exit 2
fi

exec valgrind \
  --tool=memcheck \
  --leak-check=full \
  --show-leak-kinds=definite,indirect \
  --errors-for-leak-kinds=definite,indirect \
  --track-origins=yes \
  --track-fds=yes \
  --trace-children=yes \
  --error-exitcode=99 \
  "${test_binary}" "~[e2e]~[soak]~[benchmark]" \
  --order rand \
  --rng-seed 12648430
