# Документация OPC_SERVER

Промышленный шлюз **Modbus → OPC UA → SCADA**: диспетчеризация опроса, отладка, трансляция протоколов/семантики и накопление данных.

Документы описывают целевую архитектуру и проектную спецификацию. Реализация кода идёт по [roadmap](07-roadmap.md). Исторический backlog опроса Modbus: [tasks.md](tasks.md).

## Оглавление

| Документ | Содержание |
|----------|------------|
| [01 — Обзор](01-overview.md) | Цели, роли, OPC UA vs Classic/DA, NFR |
| [02 — Архитектура](02-architecture.md) | Модули, потоки данных, границы ответственности |
| [03 — Проекты карт Modbus](03-modbus-projects.md) | Формат проекта, UX разметки, CLI `opc-map` |
| [04 — Информационная модель OPC UA](04-opcua-information-model.md) | Адресное пространство, Quality, Subscriptions |
| [05 — Технологический стек](05-tech-stack.md) | C++23, библиотеки, toolchain |
| [06 — Диспетчеризация, отладка, трансляция, накопление](06-dispatch-debug-store.md) | Операционные подсистемы |
| [07 — Roadmap](07-roadmap.md) | Этапы реализации |

## Схемы и примеры

| Файл | Назначение |
|------|------------|
| [schemas/modbus-project.schema.json](schemas/modbus-project.schema.json) | JSON Schema формата `*.modbusproj.json` |
| [examples/demo-plant.modbusproj.json](examples/demo-plant.modbusproj.json) | Пример проекта карты Modbus |
| [config.json](config.json) | Устаревший прототип конфигурации (см. миграцию в документе 03) |

## Быстрый старт для читателя

1. Прочитать [обзор](01-overview.md) и [архитектуру](02-architecture.md).
2. Изучить [формат проектов Modbus](03-modbus-projects.md) и пример `examples/demo-plant.modbusproj.json`.
3. Сопоставить northbound с [моделью OPC UA](04-opcua-information-model.md).
4. Ориентироваться на [стек](05-tech-stack.md) и [roadmap](07-roadmap.md) при реализации.
