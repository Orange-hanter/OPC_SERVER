#!/usr/bin/env bash
# Opt-in lab E2E: Catch2 [e2e] against LoopbackModbusSlave + ServerRuntime + UA client.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${OPC_TESTS_BIN:-$ROOT/build/dev/tests/opc_tests}"
if [[ ! -x "$BIN" ]]; then
  echo "opc_tests not found at $BIN — build preset dev first." >&2
  exit 1
fi
export OPC_E2E=1
exec "$BIN" "[e2e]"
