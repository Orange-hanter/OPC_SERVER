#!/usr/bin/env bash
# Live OTLP smoke: start a collector (Python OTLP/HTTP by default; Docker optional),
# export metrics+traces from opc_tests, assert the sink received both signals.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

BUILD_DIR="${OPC_BUILD_DIR:-}"
if [[ -z "${BUILD_DIR}" ]]; then
  if [[ -x "${ROOT}/build/ci/tests/opc_tests" ]]; then
    BUILD_DIR="${ROOT}/build/ci"
  elif [[ -x "${ROOT}/build/tests/opc_tests" ]]; then
    BUILD_DIR="${ROOT}/build"
  else
    echo "error: opc_tests not found; set OPC_BUILD_DIR or build preset ci" >&2
    exit 1
  fi
fi

TESTS_BIN="${BUILD_DIR}/tests/opc_tests"
if [[ ! -x "${TESTS_BIN}" ]]; then
  echo "error: missing executable ${TESTS_BIN}" >&2
  exit 1
fi

OUT_DIR="$(mktemp -d "${TMPDIR:-/tmp}/opc-otlp-smoke.XXXXXX")"
RECEIPT="${OUT_DIR}/receipt.jsonl"
METRICS_FILE="${OUT_DIR}/metrics.json"
TRACES_FILE="${OUT_DIR}/traces.json"
READY_FILE="${OUT_DIR}/ready"
COLLECTOR_CID=""
RECEIVER_PID=""
MODE=""
OTLP_PORT=""

cleanup() {
  if [[ -n "${RECEIVER_PID}" ]] && kill -0 "${RECEIVER_PID}" 2>/dev/null; then
    kill "${RECEIVER_PID}" 2>/dev/null || true
    wait "${RECEIVER_PID}" 2>/dev/null || true
  fi
  if [[ -n "${COLLECTOR_CID}" ]]; then
    docker rm -f "${COLLECTOR_CID}" >/dev/null 2>&1 || true
  fi
}
trap cleanup EXIT

pick_free_port() {
  python3 - <<'PY'
import socket
s = socket.socket()
s.bind(("127.0.0.1", 0))
print(s.getsockname()[1])
s.close()
PY
}

wait_ready() {
  local deadline=$((SECONDS + 30))
  while (( SECONDS < deadline )); do
    if [[ -f "${READY_FILE}" ]]; then
      return 0
    fi
    if [[ -n "${RECEIVER_PID}" ]] && ! kill -0 "${RECEIVER_PID}" 2>/dev/null; then
      echo "error: OTLP receiver exited before becoming ready" >&2
      return 1
    fi
    sleep 0.1
  done
  echo "error: timed out waiting for OTLP receiver readiness" >&2
  return 1
}

wait_port() {
  local host="$1" port="$2" deadline=$((SECONDS + 30))
  while (( SECONDS < deadline )); do
    if (echo >/dev/tcp/"${host}"/"${port}") >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.2
  done
  echo "error: timed out waiting for ${host}:${port}" >&2
  return 1
}

start_python_receiver() {
  OTLP_PORT="$(pick_free_port)"
  : >"${RECEIPT}"
  rm -f "${READY_FILE}"
  python3 "${ROOT}/scripts/ci/otlp_http_receiver.py" \
    --host 127.0.0.1 --port "${OTLP_PORT}" --receipt "${RECEIPT}" --ready "${READY_FILE}" &
  RECEIVER_PID=$!
  MODE="python"
  wait_ready
  wait_port 127.0.0.1 "${OTLP_PORT}"
  echo "otlp smoke: python OTLP/HTTP receiver pid=${RECEIVER_PID} port=${OTLP_PORT}"
}

start_docker_collector() {
  local image="${OPC_OTEL_COLLECTOR_IMAGE:-otel/opentelemetry-collector-contrib:0.114.1}"
  OTLP_PORT="$(pick_free_port)"
  mkdir -p "${OUT_DIR}"
  : >"${METRICS_FILE}"
  : >"${TRACES_FILE}"
  docker pull -q "${image}" >/dev/null
  COLLECTOR_CID="$(docker run -d --rm \
    -p "${OTLP_PORT}:4318" \
    -v "${ROOT}/ci/otel-collector.yaml:/etc/otelcol-contrib/config.yaml:ro" \
    -v "${OUT_DIR}:/output" \
    "${image}")"
  MODE="docker"
  wait_port 127.0.0.1 "${OTLP_PORT}"
  echo "otlp smoke: docker collector ${image} (${COLLECTOR_CID:0:12}) port=${OTLP_PORT}"
}

# Prefer the lightweight Python sink in CI (no Docker daemon required).
# Set OPC_OTLP_PREFER_DOCKER=1 to try the contrib collector first.
if [[ "${OPC_OTLP_PREFER_DOCKER:-0}" == "1" ]] && command -v docker >/dev/null 2>&1 &&
  docker info >/dev/null 2>&1; then
  if ! start_docker_collector; then
    echo "warning: docker collector failed; falling back to python receiver" >&2
    cleanup
    COLLECTOR_CID=""
    RECEIVER_PID=""
    start_python_receiver
  fi
else
  start_python_receiver
fi

export OPC_OTLP_SMOKE=1
export OPC_OTLP_ENDPOINT="${OPC_OTLP_ENDPOINT:-http://127.0.0.1:${OTLP_PORT}/v1/metrics}"
export OPC_OTLP_RECEIPT="${RECEIPT}"
export OPC_OTLP_METRICS_FILE="${METRICS_FILE}"
export OPC_OTLP_TRACES_FILE="${TRACES_FILE}"
export OPC_OTLP_MODE="${MODE}"

echo "otlp smoke: endpoint=${OPC_OTLP_ENDPOINT} mode=${MODE}"
"${TESTS_BIN}" "[otlp][live]" --reporter compact

assert_contains() {
  local file="$1" needle="$2" label="$3"
  local deadline=$((SECONDS + 20))
  while (( SECONDS < deadline )); do
    if [[ -s "${file}" ]] && grep -q -- "${needle}" "${file}"; then
      echo "otlp smoke: ok ${label} contains '${needle}'"
      return 0
    fi
    sleep 0.5
  done
  echo "error: ${label} missing '${needle}' in ${file}" >&2
  ls -la "${OUT_DIR}" >&2 || true
  [[ -f "${file}" ]] && head -c 2000 "${file}" >&2 || true
  return 1
}

if [[ "${MODE}" == "docker" ]]; then
  assert_contains "${METRICS_FILE}" "modbus_poll" "metrics.json"
  assert_contains "${TRACES_FILE}" "modbus.poll" "traces.json"
else
  assert_contains "${RECEIPT}" '"signal": "metrics"' "receipt metrics"
  assert_contains "${RECEIPT}" '"signal": "traces"' "receipt traces"
fi

echo "otlp smoke: PASS (${MODE})"
