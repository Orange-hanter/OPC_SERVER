# OPC_SERVER

Промышленный шлюз **Modbus → OPC UA → SCADA**: диспетчеризация опроса, отладка, трансляция данных и накопление истории для диспетчерских задач.

> Документация целевой архитектуры: **[DOCs/README.md](DOCs/README.md)**  
> Стандарты и ADR: **[DOCs/08-engineering-standards.md](DOCs/08-engineering-standards.md)**, **[DOCs/adr/](DOCs/adr/README.md)**  
> Как контрибьютить: **[CONTRIBUTING.md](CONTRIBUTING.md)**

## Возможности (целевые)

- Опрос устройств по **Modbus TCP** (UDP — позже) по удобным **проектам карт** `*.modbusproj.json`
- Публикация тегов в **OPC UA** (Read / Write / Subscriptions) для SCADA
- Диспетчеризация групп опроса, write-down, качество и временные метки
- Отладка кадров и сессий, локальный historian

OPC Classic / DA не входят в ядро; граница описана в [DOCs/01-overview.md](DOCs/01-overview.md).

## Стек (целевой)

- **C++26**, CMake, Ninja
- Asio, open62541, nlohmann/json, spdlog, OpenTelemetry  
  Подробности: [DOCs/05-tech-stack.md](DOCs/05-tech-stack.md)

## Документация

| Раздел | Ссылка |
|--------|--------|
| Оглавление | [DOCs/README.md](DOCs/README.md) |
| Обзор | [DOCs/01-overview.md](DOCs/01-overview.md) |
| Архитектура | [DOCs/02-architecture.md](DOCs/02-architecture.md) |
| Проекты карт Modbus | [DOCs/03-modbus-projects.md](DOCs/03-modbus-projects.md) |
| Пример проекта | [DOCs/examples/demo-plant.modbusproj.json](DOCs/examples/demo-plant.modbusproj.json) |
| Roadmap | [DOCs/07-roadmap.md](DOCs/07-roadmap.md) |

## Сборка (текущий каркас + этап 1)

```bash
git clone --recursive https://github.com/Orange-hanter/OPC_SERVER
cd OPC_SERVER
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Инструмент карт:

```bash
./build/opc-map validate DOCs/examples/demo-plant.modbusproj.json
./build/opc-map migrate-legacy DOCs/config.json -o /tmp/migrated.modbusproj.json
```

На компиляторах без C++26 (например GCC 13) CMake автоматически откатывается на **C++23** с предупреждением; целевой стандарт проекта — **C++26**.

На Windows генератор по умолчанию может создать проект Visual Studio:

```bash
cmake -S . -B build
cmake --build build
```