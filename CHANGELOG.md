# Changelog

All notable changes to this project are documented in this file.

Format based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning follows [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added

- Tauri 2 Engineering Studio for Windows, Linux and macOS with local project
  editing, JSON Schema/`opc-map` validation, RU/EN themes and read-only remote
  OPC UA monitoring (ADR-0016)
- `opc-monitor` open62541 sidecar with JSON Lines IPC, recursive Browse,
  Subscriptions, reconnect and bounded desktop event handling
- Stable `Objects/OPC_SERVER/Diagnostics` nodes for server state, quality
  counters and the latest runtime error
- Cross-platform Studio lint/test/package CI matrix
- Stage 5 Historian/Debug (ADR-0014): `RingHistorian`, `SqliteHistorian`, TagStore
  subscription in `ServerRuntime`, CLI `--historian-db` / `--historian-capacity` /
  `--no-historian` / `--frame-log`
- Modbus frame journal (`IFrameLog`, wired into `ModbusTcpTransport::transact`)
- `MemoryMetrics` in-process sink; historian replay helper
- Stage 5 observability (ADR-0015): `SpdlogLog`, `OtelMetrics` (ostream / optional OTLP),
  CLI `--log-level` / `--log-file` / `--metrics-export` / `--otlp-endpoint`;
  dispatcher `modbus_poll_rtt_ms` histogram
- Stage 5 tests (`tests/test_historian.cpp`, `tests/test_frame_log.cpp`)
- Asio increment A (standalone Asio 1.32, strand-per-endpoint, reconnect backoff)
- Increment D: open62541 OpenSSL encryption (`Sign`/`SignAndEncrypt` fail-closed),
  Studio/`opc-monitor` certificate profile, Modbus UDP adapter, Catch2 load stand
- Poll/write OpenTelemetry traces (`ITracer`, `modbus.poll` / `modbus.write`) and
  OTLP/HTTP exporters in the `ci` CMake preset (`-DOPC_WITH_OTLP=ON`)
- Industrial PKI defaults: Sign/Encrypt without AcceptAll unless `--ua-accept-untrusted`;
  `--ua-crl` revocation list; `--ua-strict-certs` still forces reject
- Asio-native Modbus TCP (`AsioModbusTcpTransport`): private `io_context`, async
  connect/read/write with deadline; sync `IModbusTransport` facade for Dispatcher
- OPC UA UsernameIdentityToken: `opcua.users` / `--ua-user`, fail-closed anonymous when
  users are set, `allowNonePassword` / `--ua-allow-none-password` for lab None mode;
  Studio and `opc-monitor` forward username/password
- Frame-log replay (`ReplayModbusTransport`, `load_frame_log_file`) for offline Dispatcher tests
- Historian cold pending no longer drops samples before `flush()`; frame log emits outside the TCP mutex
- `opc-map doctor`: register overlaps, unpolled tags, sparse/gappy poll blocks
- Stage 4.5 hardening (ADR-0013): UA DataSource TagStore reads, write StatusCodes,
  facade `bind_tags`/`set_write_handler`, write-queue bounds, value-preserving quality updates
- `OPC_SERVER --version` and generated version header; install layout unchanged
- Stage 4 OPC UA Write + Subscriptions: UA client write → Dispatcher (`writes_first`),
  TagStore-driven MonitoredItem notifications, write-queue mutex
- Stage 4 smoke tests (`tests/test_opc_ua_write_subs.cpp`)
- Stage 3 OPC UA Read: `OpcUaServer` (open62541), TagStore → UA variables, client smoke tests
- CLI `--no-opcua`; ServerRuntime wires optional northbound facade
- FetchContent open62541 (v1.4.11)
- GitHub Actions CI (GCC build + test) with Linux x64 artifacts
- Release workflow on SemVer tags (`v*.*.*`) publishing tarball + SHA256
- CI/release strategy documentation (`DOCs/11-ci-and-releases.md`)
- CMake `install()` rules for `OPC_SERVER` and `opc-map`

### Changed

- Roadmap snapshot (2026-08-14): этапы 0–7 чеклиста ядра закрыты (100%);
  живой backlog — `DOCs/tasks.md` (вне ядра: async TCP, industrial PKI)
- FetchContent/CI: `libssl-dev`, `UA_ENABLE_ENCRYPTION=OPENSSL`; Conan `open62541:encryption=openssl`
- CLI `--ua-cert` / `--ua-key` / `--ua-trust` / `--ua-crl` / `--ua-strict-certs` /
  `--ua-accept-untrusted`; `--traces-export`
- Sign/Encrypt channel PKI fail-closed by default (lab AcceptAll is opt-in)

### Fixed

- TSan: protect `Dispatcher` transport/`last_poll_ms_` maps across endpoint strands;
  cancel Asio repeat timers on their strand; synchronize UDP slave test map
- Conan CI: FetchContent open62541 (plugin headers); Catch2 remains Conan
- Studio package CI: OpenSSL on Windows; LLVM + OpenSSL on macOS for C++23 sidecars
- Write batch tail no longer dropped when a Modbus write fails mid-flush
- Bad/WriteRejected publishes keep the previous engineering ScalarValue
- Removed adapters→core coupling via `RuntimeIndex` in the OPC UA adapter

## [0.1.0] - 2026-08-11

### Added

- Project documentation (OPC UA gateway architecture, ADR-0001…0011)
- `opc-map` validate / migrate-legacy for `*.modbusproj.json`
- Core: TagStore, Translator, Dispatcher, RuntimeIndex
- Adapters: Modbus TCP transport, FakeModbusTransport
- App: ServerRuntime composition root, CLI (`--project`, `--once`, `--watch`)

[Unreleased]: https://github.com/Orange-hanter/OPC_SERVER/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/Orange-hanter/OPC_SERVER/releases/tag/v0.1.0
