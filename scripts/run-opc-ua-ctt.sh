#!/usr/bin/env bash
# Wrapper around OPC Foundation CTT. The tool itself is proprietary/GUI;
# this script only prepares a lab process and prints the operator checklist.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
echo "OPC UA CTT lab helper"
echo "Docs: $ROOT/DOCs/testing/opc-ua-ctt.md"
echo
echo "1. Build Release and start OPC_SERVER with a loopback Modbus map."
echo "2. Install CTT from https://opcfoundation.org/developer-tools/certification-test-tools/opc-ua-compliance-test-tool-ua-ctt/"
echo "3. Point CTT at opc.tcp://127.0.0.1:4840 (security None, then SignAndEncrypt on stage 7)."
echo "4. File Failed Read/Write/MonitoredItems as product defects with Catch2 regressions."
echo
if [[ -n "${OPC_CTT_BIN:-}" && -x "${OPC_CTT_BIN}" ]]; then
  exec "$OPC_CTT_BIN" "$@"
fi
exit 0
