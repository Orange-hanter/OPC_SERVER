# ADR-0002: Concurrency model

- **Status:** Accepted
- **Date:** 2026-08-11

## Context

Одновременно: циклы опроса Modbus, write-down из UA, Subscriptions, historian, метрики.  
Неправильная модель → data races, priority inversion, «одно устройство блокирует всех».

## Decision

### Базовая модель

1. **Asio `io_context`** (один или пул потоков) — reactor.
2. **`strand` на каждый Modbus endpoint** — сериализация всех операций этого endpoint (read cycle, write, reconnect).
3. **TagStore** — отдельный контракт потокобезопасности ([ADR-0006](0006-tagstore-data-model.md)): fine-grained lock или concurrent map; **не** захватывать mutex TagStore, удерживая сокетный I/O.
4. **UA adapter** — callbacks переводят работу в безопасный контекст (post в strand/executor core), не мутируют TagStore «как получится» из произвольного потока UA.
5. **Никакого `sleep`** в runtime-пути; только `steady_timer`.

### Приоритеты на endpoint strand

```text
1) reconnect / connection state machine
2) write-down queue (policy writes_first по умолчанию)
3) scheduled poll reads
```

### Потоки

- v1: N worker threads на одном `io_context` (N = config, default 2..hardware_concurrency).
- CPU-bound (маловероятно) — не на strand I/O.

## Alternatives

| Вариант | Почему отклонён |
|---------|-----------------|
| Поток на устройство | Взрыв потоков на сотнях endpoints |
| Один большой mutex на всё | Конвой, latency jitter |
| Actor framework (CAF и т.п.) | Лишняя зависимость; Asio достаточно |

## Consequences

- Тесты concurrent-путей — под TSan.
- Документировать для каждого публичного метода TagStore: from which executor.
- Dispatcher ставит работы через `post`/`defer`, не вызывает transport синхронно из UA thread.
