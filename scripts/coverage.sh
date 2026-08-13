#!/usr/bin/env bash
# Collect line coverage for Src/domain, Src/core, Src/project after a coverage preset run.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${OPC_COVERAGE_BUILD:-$ROOT/build/coverage}"
FILTER='Src/domain|Src/core|Src/project'
THRESHOLD="${OPC_COVERAGE_MIN:-70}"

if [[ ! -d "$BUILD" ]]; then
  echo "Coverage build directory not found: $BUILD" >&2
  echo "Run: cmake --preset coverage && cmake --build --preset coverage && ctest --preset coverage" >&2
  exit 1
fi

if command -v gcovr >/dev/null 2>&1; then
  gcovr --root "$ROOT" --filter "$ROOT/Src/domain" --filter "$ROOT/Src/core" --filter "$ROOT/Src/project" \
    --exclude-throw-branches --print-summary --fail-under-line "$THRESHOLD" \
    --object-directory "$BUILD"
  exit $?
fi

if command -v llvm-cov >/dev/null 2>&1 && command -v llvm-profdata >/dev/null 2>&1; then
  echo "llvm-cov path is available; prefer gcovr for GCC coverage presets." >&2
fi

# Fallback: parse gcov notes if present
mapfile -t files < <(find "$BUILD" -name '*.gcda' | grep -E "$FILTER" || true)
if [[ ${#files[@]} -eq 0 ]]; then
  echo "No .gcda files under $BUILD for domain/core/project. Build with OPC_ENABLE_COVERAGE=ON and run tests." >&2
  exit 1
fi

echo "gcovr is not installed; install it for a percentage gate (apt install gcovr)." >&2
echo "Found ${#files[@]} coverage data files — treat this as a smoke pass."
exit 0
