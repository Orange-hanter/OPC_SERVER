# ADR-0010: Scalability model

- **Status:** Accepted
- **Date:** 2026-08-11

## Context

Нужно понимать, как система растёт: 10 устройств ≠ 1000; 100 тегов ≠ 100k. Без модели «масштабирования» оптимизируют не то.

## Decision

### Оси масштаба

| Ось | v1 target (design) | Механизм |
|-----|--------------------|----------|
| Endpoints (TCP connections) | десятки–сотни | strand per endpoint, пул I/O threads |
| Tags | тысячи–десятки тысяч | TagStore shards; poll blocks не per-tag PDU |
| Poll rate | группы 50–1000 ms | scheduler; overrun metrics |
| UA sessions / monitored items | десятки сессий, тысячи items | open62541 + read from store |
| Historian | short hot buffer | ring; cold async writer |

### Принципы

1. **Масштабируем I/O и блоки регистров**, не число потоков.
2. **Горизонтальное** масштабирование (несколько процессов/шардов площадки) — этап после стабильного single-process; конфиг уже делит endpoints логически.
3. Backpressure: ограниченная write queue; reject/UA Bad при переполнении (политика).
4. Бюджет latency: poll RTT и UA publish не блокируют друг друга через общий coarse lock.

### Что измерять, прежде чем оптимизировать

- `modbus_poll_overruns`
- p99 `modbus_poll_rtt_ms`
- `write_queue_depth`
- CPU/reactor lag

## Alternatives

| Вариант | Почему отклонён |
|---------|-----------------|
| Сразу Kafka/NATS шина | Overkill для шлюза поля |
| Один поток на тег | Не масштабируется |

## Consequences

- Stage 2 проектирует block reads, не read-per-tag.
- Нагрузочные тесты — отдельный milestone после Subscriptions.
