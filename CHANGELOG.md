# Changelog

All notable changes to this project are documented in this file.

Format based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning follows [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added

- GitHub Actions CI (GCC/Clang build + test) with Linux x64 artifacts
- Release workflow on SemVer tags (`v*.*.*`) publishing tarball + SHA256
- CI/release strategy documentation (`DOCs/11-ci-and-releases.md`)
- CMake `install()` rules for `OPC_SERVER` and `opc-map`

## [0.1.0] - 2026-08-11

### Added

- Project documentation (OPC UA gateway architecture, ADR-0001…0011)
- `opc-map` validate / migrate-legacy for `*.modbusproj.json`
- Core: TagStore, Translator, Dispatcher, RuntimeIndex
- Adapters: Modbus TCP transport, FakeModbusTransport
- App: ServerRuntime composition root, CLI (`--project`, `--once`, `--watch`)

[Unreleased]: https://github.com/Orange-hanter/OPC_SERVER/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/Orange-hanter/OPC_SERVER/releases/tag/v0.1.0
