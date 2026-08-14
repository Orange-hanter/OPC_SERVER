#!/usr/bin/env bash
# Live OTLP smoke: start a collector (Docker contrib preferred, Python fallback),
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
COLLECTOR_CID=""
RECEIVER_PID=""
MODE=""

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

wait_port() {
  local host="$1" port="$2" deadline=$((SECONDS + 45))
  while (( SECONDS < deadline )); do
    if (echo >/dev/tcp/"${host}"/"${port}") >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.5
  done
  echo "error: timed out waiting for ${host}:${port}" >&2
  return 1
}

start_docker_collector() {
  local image="${OPC_OTEL_COLLECTOR_IMAGE:-otel/opentelemetry-collector-contrib:0.114.1}"
  mkdir -p "${OUT_DIR}"
  : >"${METRICS_FILE}"
  : >"${TRACES_FILE}"
  docker pull -q "${image}" >/dev/null
  COLLECTOR_CID="$(docker run -d --rm \
    -p 4318:4318 \
    -p 13133:13133 \
    -v "${ROOT}/ci/otel-collector.yaml:/etc/otelcol-contrib/config.yaml:ro" \
    -v "${OUT_DIR}:/output" \
    "${image}")"
  MODE="docker"
  wait_port 127.0.0.1 4318
  echo "otlp smoke: docker collector ${image} (${COLLECTOR_CID:0:12})"
}

start_python_receiver() {
  python3 "${ROOT}/scripts/ci/otlp_http_receiver.py" \
    --host 127.0.0.1 --port 4318 --receipt "${RECEIPT}" &
  RECEIVER_PID=$!
  MODE="python"
  wait_port 127.0.0.1 4318
  echo "otlp smoke: python OTLP/HTTP receiver pid=${RECEIVER_PID}"
}

if command -v docker >/dev/null 2>&1 && docker info >/dev/null 2>&1; then
  if ! start_docker_collector; then
    echo "warning: docker collector failed; falling back to python receiver" >&2
    cleanup
    COLLECTOR_CID=""
    start_python_receiver
  fi
else
  start_python_receiver
fi

export OPC_OTLP_SMOKE=1
export OPC_OTLP_ENDPOINT="${OPC_OTLP_ENDPOINT:-http://127.0.0.1:4318/v1/metrics}"
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
  assert_contains "${METRICS_FILE}" "opc-otlp-smoke" "metrics.json"
  assert_contains "${METRICS_FILE}" "otlp_smoke_counter" "metrics.json"
  assert_contains "${TRACES_FILE}" "opc-otlp-smoke" "traces.json"
  assert_contains "${TRACES_FILE}" "otlp.smoke" "traces.json"
else
  assert_contains "${RECEIPT}" '"signal": "metrics"' "receipt"
  assert_contains "${RECEIPT}" '"signal": "traces"' "receipt"
fi

echo "otlp smoke: PASS (${MODE})"
