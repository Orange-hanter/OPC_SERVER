# ADR-0008: Observability

- **Status:** Accepted
- **Date:** 2026-08-11

## Context

Диспетчеризация и отладка — продуктовые требования, не «потом логи добавим». Без единой схемы полей логи бесполезны на объекте.

## Decision

### Порт `IMetrics` + structured logging

- Логи: spdlog (adapter), через тонкий `ILog` или согласованный pattern.
- Метрики: OpenTelemetry metrics (adapter), за `IMetrics`.
- Трассы: OTel traces на транзакцию poll/write (фаза 5+; интерфейс заложить раньше).

### Обязательные поля лога (где применимо)

`component`, `endpoint_id`, `device_id`, `tag`, `fc`, `rtt_ms`, `error_code`, `session_id` (UA)

### Минимальные метрики v1/v2

| Имя | Тип | Смысл |
|-----|-----|------|
| `modbus_poll_rtt_ms` | histogram | RTT опроса |
| `modbus_poll_errors_total` | counter | ошибки по причине |
| `modbus_write_queue_depth` | gauge | глубина write queue |
| `tag_quality` | gauge/event | доля Bad/Uncertain |
| `ua_sessions` | gauge | активные сессии |

### Debug channels

См. [06](../06-dispatch-debug-store.md): frame log, tag watch. Реализуются как adapters, подписанные на ports, не как `printf` в core.

## Alternatives

| Вариант | Почему отклонён |
|---------|-----------------|
| Только stdout | Недостаточно для промышленной эксплуатации |
| Vendor APM only | Нужен vendor-neutral OTel |

## Consequences

- Core принимает `IMetrics*` (nullable no-op в тестах).
- Запрещены логи с PII/секретами сертификатов.
