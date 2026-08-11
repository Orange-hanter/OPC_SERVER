# Документация OPC_SERVER

Промышленный шлюз **Modbus → OPC UA → SCADA**: диспетчеризация опроса, отладка, трансляция протоколов/семантики и накопление данных.

Документы описывают целевую архитектуру и проектную спецификацию. Реализация кода идёт по [roadmap](07-roadmap.md). Исторический backlog опроса Modbus: [tasks.md](tasks.md).

**Сначала читайте:** [engineering standards](08-engineering-standards.md) → [ADR](adr/README.md) → [architecture](02-architecture.md).

## Оглавление

| Документ | Содержание |
|----------|------------|
| [01 — Обзор](01-overview.md) | Цели, роли, OPC UA vs Classic/DA, NFR |
| [02 — Архитектура](02-architecture.md) | Hexagon, модули, потоки данных |
| [03 — Проекты карт Modbus](03-modbus-projects.md) | Формат проекта, UX разметки, CLI `opc-map` |
| [04 — Информационная модель OPC UA](04-opcua-information-model.md) | Адресное пространство, Quality, Subscriptions |
| [05 — Технологический стек](05-tech-stack.md) | C++26, библиотеки, toolchain |
| [06 — Диспетчеризация, отладка, трансляция, накопление](06-dispatch-debug-store.md) | Операционные подсистемы |
| [07 — Roadmap](07-roadmap.md) | Этапы реализации |
| [08 — Engineering standards](08-engineering-standards.md) | Слои, naming, зависимости, DoD |
| [09 — Scalability](09-scalability.md) | Изоляция endpoint, рост тегов/клиентов |
| [10 — Quality gates](10-quality-gates.md) | Ворота merge / CI |
| [11 — CI and releases](11-ci-and-releases.md) | Actions, артефакты, SemVer-релизы |
| [12 — Современный CMake и Conan 2](12-modern-cmake.md) | Presets, target-based настройки, санитайзеры, CPack и package manager |
| [ADR index](adr/README.md) | Architecture Decision Records |
| [CONTRIBUTING](../CONTRIBUTING.md) | Как вносить изменения |
| [CHANGELOG](../CHANGELOG.md) | История версий |

## Схемы и примеры

| Файл | Назначение |
|------|------------|
| [schemas/modbus-project.schema.json](schemas/modbus-project.schema.json) | JSON Schema формата `*.modbusproj.json` |
| [examples/demo-plant.modbusproj.json](examples/demo-plant.modbusproj.json) | Пример проекта карты Modbus |
| [config.json](config.json) | Устаревший прототип конфигурации (см. миграцию в документе 03) |

## Быстрый старт для читателя

1. [Standards](08-engineering-standards.md) + [ADR-0001](adr/0001-hexagonal-architecture.md).
2. [Архитектура](02-architecture.md) и [масштаб](09-scalability.md).
3. [Формат проектов Modbus](03-modbus-projects.md) + `examples/demo-plant.modbusproj.json`.
4. [Стек](05-tech-stack.md) и [roadmap](07-roadmap.md).
