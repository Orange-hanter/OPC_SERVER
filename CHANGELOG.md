# Changelog

All notable changes to this project are documented in this file.

Format based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning follows [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added

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

### Fixed

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
