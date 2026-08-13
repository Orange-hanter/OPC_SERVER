#!/usr/bin/env bash
# Short soak (minutes). Full 8h soak belongs on a dedicated lab host (SAT).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${OPC_TESTS_BIN:-$ROOT/build/dev/tests/opc_tests}"
if [[ ! -x "$BIN" ]]; then
  echo "opc_tests not found at $BIN — build preset dev first." >&2
  exit 1
fi
export OPC_SOAK=1
exec "$BIN" "[soak]"
