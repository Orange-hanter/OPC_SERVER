# ADR-0015: spdlog and OpenTelemetry adapters

- Status: Accepted
- Date: 2026-08-11
- Updated: 2026-08-14 (poll/write traces + OTLP in CI)

## Context

ADR-0008 requires production logging via spdlog and metrics via OpenTelemetry behind `ILog` / `IMetrics`. Stage 5 shipped `MemoryMetrics` + `StderrLog` stubs.

## Decision

1. **`SpdlogLog`**: async (default) stderr color sink + optional rotating file (`--log-file`). Levels via `--log-level`. Messages include `component=<name>`.
2. **`OtelMetrics`**: OpenTelemetry C++ SDK MeterProvider. Export modes:
   - `none` — instruments only (tests / quiet runs; CLI default)
   - `ostream` — periodic stdout dump
   - `otlp` — OTLP/HTTP when built with `-DOPC_WITH_OTLP=ON` (`--otlp-endpoint`)
3. **`ITracer` / `OtelTracer`**: poll/write spans (`modbus.poll`, `modbus.write`) from Dispatcher. Same export modes via `--traces-export`. OTLP traces URL is derived from `--otlp-endpoint` (`/v1/metrics` → `/v1/traces`).
4. Gauges map to UpDownCounter deltas; histograms use `CreateDoubleHistogram`.
5. Dispatcher records `modbus_poll_rtt_ms` histogram on each tag poll.
6. `MemoryMetrics` and `RecordingTracer` remain for unit tests that need in-process assertions.
7. CMake preset `ci` sets `OPC_WITH_OTLP=ON` (protobuf + libcurl). Local `dev`/`release` stay OFF unless requested.
8. **Live collector smoke** in CI (`scripts/ci/otlp_smoke.sh`): prefer Docker
   `otel/opentelemetry-collector-contrib` with `ci/otel-collector.yaml` (file exporters under
   `/output`); fall back to `scripts/ci/otlp_http_receiver.py`. Catch2 `[otlp][live]` runs only
   when `OPC_OTLP_SMOKE=1`.

## Consequences

- Core still only sees ports; no spdlog/OTel includes in `core/`.
- Default developer builds avoid protobuf/curl; CI compiles OTLP exporters **and** exercises a live OTLP/HTTP sink.
- Unit tests keep using `none` / RecordingTracer; live export is gated behind `OPC_OTLP_SMOKE`.
