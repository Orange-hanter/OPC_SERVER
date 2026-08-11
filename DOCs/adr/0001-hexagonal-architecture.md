# ADR-0001: Hexagonal architecture (Ports & Adapters)

- **Status:** Accepted
- **Date:** 2026-08-11

## Context

Система соединяет три мира: полевой Modbus, домен тегов/диспетчеризации, OPC UA / SCADA.  
Если смешать сокеты, JSON, UA и планировщик в одном классе, система не тестируется и не масштабируется.

Нужна структура, где:

- доменная логика не знает про Asio/open62541;
- можно подменить transport на fake в тестах;
- engineering CLI не тянет runtime-сервер.

## Decision

Принимаем **Ports & Adapters (hexagonal)**:

1. **`domain`** — чистые типы (`TagId`, `Quality`, `DataValue`, …).
2. **`ports`** — интерфейсы: `IModbusTransport`, `ITagStore`, `IClock`, `IMetrics`, `IOpcUaFacade`, `IHistorian`.
3. **`core`** — `Translator`, `PollScheduler`/`Dispatcher`, реализация TagStore; зависит только от `domain` + `ports`.
4. **`adapters`** — Asio Modbus TCP, open62541, SQLite historian, spdlog/OTel.
5. **`app`** — composition root (wiring).
6. **`project`** — парсинг карт; используется и CLI, и app; не является adapter’ом поля.

Правило: зависимости направлены **внутрь** к domain/ports. Core **никогда** не `#include` адаптеры.

```text
adapters → ports ← core → domain
                ↖ project (engineering)
app wires adapters + core
```

## Alternatives

| Вариант | Почему отклонён |
|---------|-----------------|
| Классический «layered» без портов | Слои быстро протекают; сложно подменить I/O |
| Microservices сразу | Операционная сложность не нужна на v1; границы важнее процессов |
| Plugin DLL на каждый протокол | Рано; сначала стабильные порты в одном процессе |

## Consequences

- Этап 2 начинается с портов и fake-transport тестов, не с «сокет в App».
- Больше файлов/интерфейсов на старте — меньше стоимости изменений позже.
- Code review обязан проверять таблицу зависимостей ([08](../08-engineering-standards.md)).
