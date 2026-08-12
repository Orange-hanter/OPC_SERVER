# ADR-0015: spdlog and OpenTelemetry adapters

- Status: Accepted
- Date: 2026-08-11

## Context

ADR-0008 requires production logging via spdlog and metrics via OpenTelemetry behind `ILog` / `IMetrics`. Stage 5 shipped `MemoryMetrics` + `StderrLog` stubs.

## Decision

1. **`SpdlogLog`**: async (default) stderr color sink + optional rotating file (`--log-file`). Levels via `--log-level`. Messages include `component=<name>`.
2. **`OtelMetrics`**: OpenTelemetry C++ SDK MeterProvider. Export modes:
   - `ostream` (default) — periodic stdout dump
   - `none` — instruments only (tests / quiet runs)
   - `otlp` — OTLP/HTTP when built with `-DOPC_WITH_OTLP=ON` (`--otlp-endpoint`)
3. Gauges map to UpDownCounter deltas; histograms use `CreateDoubleHistogram`.
4. Dispatcher records `modbus_poll_rtt_ms` histogram on each tag poll.
5. `MemoryMetrics` remains for unit tests that need in-process assertions.

## Consequences

- Core still only sees ports; no spdlog/OTel includes in `core/`.
- CI installs `libsqlite3-dev`; OTLP stays opt-in to avoid protobuf/curl in default builds.
- Traces remain future work (poll/write spans) on the same SDK dependency.
