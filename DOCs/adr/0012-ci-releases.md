# ADR-0012: CI artifacts and SemVer releases

- **Status:** Accepted
- **Date:** 2026-08-11

## Context

Нужны воспроизводимые сборки и понятная публикация бинарников без ручной «сборки с ноутбука».

## Decision

1. **CI** на каждый PR/`master`: GCC + Clang, `ctest`; GCC публикует Actions artifact (tarball + sha256).
2. **Releases** только с SemVer-тегов `vMAJOR.MINOR.PATCH[-prerelease]` через `release.yml`.
3. Артефакт релиза: `opc-server-<ver>-linux-x64.tar.gz` (`OPC_SERVER`, `opc-map`, examples).
4. CHANGELOG ведётся по Keep a Changelog; тег без записи в CHANGELOG — процессный дефект.

## Consequences

- Документация: [11-ci-and-releases.md](../11-ci-and-releases.md).
- Админ репозитория включает write permission для Actions (публикация Releases).
