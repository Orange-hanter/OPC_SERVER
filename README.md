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
| Northbound | OPC UA Server (Read; Write/Subscriptions — этапы 4+) |
| Engineering | `opc-map`, схемы и примеры карт |

Норматив: [DOCs/08-engineering-standards.md](DOCs/08-engineering-standards.md), [ADR](DOCs/adr/README.md).

> Документация: **[DOCs/README.md](DOCs/README.md)** · Contributing: **[CONTRIBUTING.md](CONTRIBUTING.md)**

## Возможности

**Сейчас:** проекты карт + `opc-map`, TagStore/Translator/Dispatcher, Modbus TCP, `ServerRuntime`, **OPC UA Read/Write/Subscriptions** (open62541, security None).

**Целевые:**

- Опрос Holding/Input/Coils по проектам карт
- Historian, frame debug, метрики
- Промышленный security (SignAndEncrypt)

OPC Classic / DA не входят в ядро; граница — [DOCs/01-overview.md](DOCs/01-overview.md).

## Стек

- **C++26** (fallback C++23), CMake 3.28, Ninja, CMake Presets
- open62541 (Conan 2 или FetchContent), nlohmann/json; Asio reactor — следующий инкремент  
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

## CI и релизы

- **CI** (push/PR на `master`): сборка GCC+Clang, `ctest`, артефакт Linux x64  
- **Релизы**: тег `vMAJOR.MINOR.PATCH` → GitHub Release + tarball + SHA256  

Подробности: [DOCs/11-ci-and-releases.md](DOCs/11-ci-and-releases.md), [CHANGELOG.md](CHANGELOG.md).

```bash
git tag -a v0.1.0 -m "OPC_SERVER v0.1.0"
git push origin v0.1.0
```
