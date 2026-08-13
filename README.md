# OPC_SERVER

Промышленный программный шлюз **Modbus → OPC UA → SCADA** для диспетчеризации, отладки, трансляции и накопления данных.

## Описание проекта

`OPC_SERVER` собирает данные с полевых устройств по **Modbus TCP**, приводит их к инженерным тегам (типы, byte order, scale/offset) и отдаёт верхнему уровню через **OPC UA**. Цель — прозрачный, тестируемый контур между PLC/IoT и SCADA без смешивания Classic/DA в ядре.

Проект ориентирован на:

- **удобную разметку карт** Modbus в формате `*.modbusproj.json` и CLI `opc-map`;
- **предсказуемый опрос** с группами fast/normal/slow и изоляцией по endpoint;
- **качество и время** на каждом теге (`Good` / `Uncertain` / `Bad`);
- **архитектуру Ports & Adapters** (C++26): домен и core независимы от Asio/open62541.

| Слой | Роль |
|------|------|
| Southbound | Modbus TCP (UDP позже) |
| Core | Dispatcher, Translator, TagStore |
| Northbound | OPC UA Server (Read / Write / Subscriptions, security None) |
| Engineering | Tauri Studio, `opc-map`, схемы и примеры карт |

Норматив: [DOCs/08-engineering-standards.md](DOCs/08-engineering-standards.md), [ADR](DOCs/adr/README.md).

> Документация: **[DOCs/README.md](DOCs/README.md)** · Contributing: **[CONTRIBUTING.md](CONTRIBUTING.md)**

## Возможности

**Сейчас (лабораторный MVP + reactor):** проекты карт + `opc-map` (validate/doctor/migrate-legacy),
TagStore/Translator/Dispatcher, sync Modbus TCP за Asio strand-per-endpoint,
`ServerRuntime`, **OPC UA Read/Write/Subscriptions** (DataSource, security None),
Diagnostics, historian/frame-log, spdlog/OTel metrics, **OPC Engineering Studio**.

**Следующее:** `opc-map import-csv` / `gen-nodeset` (инкремент C в [roadmap](DOCs/07-roadmap.md)),
затем SignAndEncrypt.

OPC Classic / DA не входят в ядро; граница — [DOCs/01-overview.md](DOCs/01-overview.md).

## Стек

- **C++26** (fallback C++23), CMake 3.28, Ninja, CMake Presets
- open62541 (Conan 2 или FetchContent), nlohmann/json, standalone Asio 1.32 (FetchContent)
  Подробнее: [DOCs/05-tech-stack.md](DOCs/05-tech-stack.md)

## Документация

| Раздел | Ссылка |
|--------|--------|
| Оглавление | [DOCs/README.md](DOCs/README.md) |
| Обзор | [DOCs/01-overview.md](DOCs/01-overview.md) |
| Архитектура | [DOCs/02-architecture.md](DOCs/02-architecture.md) |
| Проекты карт | [DOCs/03-modbus-projects.md](DOCs/03-modbus-projects.md) |
| Пример | [DOCs/examples/demo-plant.modbusproj.json](DOCs/examples/demo-plant.modbusproj.json) |
| Roadmap | [DOCs/07-roadmap.md](DOCs/07-roadmap.md) |
| Современный CMake и Conan | [DOCs/12-modern-cmake.md](DOCs/12-modern-cmake.md) |

## Сборка

```bash
git clone https://github.com/Orange-hanter/OPC_SERVER
cd OPC_SERVER
cmake --workflow --preset dev
```

```bash
./build/dev/OPC_SERVER --version
./build/dev/tools/opc-map/opc-map validate DOCs/examples/demo-plant.modbusproj.json
./build/dev/tools/opc-map/opc-map doctor DOCs/examples/demo-plant.modbusproj.json
./build/dev/tools/opc-map/opc-map migrate-legacy DOCs/config.json -o /tmp/migrated.modbusproj.json
```

### Установка

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$PWD/stage
cmake --build build
cmake --install build --component opc-server
./stage/bin/OPC_SERVER --version
./stage/bin/opc-map validate ./stage/share/opc-server/examples/demo-plant.modbusproj.json
```

(`--component opc-server` excludes FetchContent open62541 headers/libs from the prefix.)

Без C++26 CMake откатывается на **C++23** с предупреждением.
Режимы ASan/UBSan, unity build, PCH, IPO/CPack и сборка через Conan 2 описаны
в [практикуме по CMake](DOCs/12-modern-cmake.md).

## Engineering Studio

Studio — локальный редактор `*.modbusproj.json` и read-only клиент удалённого
OPC UA сервера. UI написан на React/TypeScript и поставляется через Tauri 2 для
Windows, Linux и macOS. Сетевой мониторинг использует `opc.tcp` через
`opc-monitor`; HTTP/WebSocket API серверу не требуется.

```bash
cd frontend/apps/studio
npm ci
npm run dev       # browser preview с mock-монитором
npm test
npm run build
```

Инструкции нативной сборки и границы безопасности описаны в
[frontend/apps/studio/README.md](frontend/apps/studio/README.md).

## CI и релизы

- **CI** (push/PR на `master`): GCC + `ctest` + Conan 2 + Studio quality/package, артефакт Linux x64 
- **Релизы**: тег `vMAJOR.MINOR.PATCH` → GitHub Release + tarball + SHA256  

Подробности: [DOCs/11-ci-and-releases.md](DOCs/11-ci-and-releases.md), [CHANGELOG.md](CHANGELOG.md).

```bash
git tag -a v0.1.0 -m "OPC_SERVER v0.1.0"
git push origin v0.1.0
```
