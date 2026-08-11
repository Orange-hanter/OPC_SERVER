# ADR-0006: TagStore and data model

- **Status:** Accepted
- **Date:** 2026-08-11

## Context

Всем потребителям (UA, historian, debug, write confirm) нужен единый актуальный снимок тега с качеством и временем. Без строгой модели появятся расхождения «в UA одно, в логе другое».

## Decision

### Идентичность

- `TagId` — стабильный opaque id (числовой), назначаемый при построении runtime из project.
- Логическое имя (`Tank1.Level`) и `nodePath` — метаданные, не ключ гонок.

### Значение

```text
TagValue {
  variant value,          // bool/int/float/...
  Quality quality,        // Good / Uncertain / Bad (+ reason)
  Timestamp source_ts,    // device/response time when known
  Timestamp server_ts,    // store commit time (IClock)
  uint64_t epoch          // project/runtime epoch for reload safety
}
```

### Семантика обновлений

1. Poller/Translator коммитят через `ITagStore::publish(tag_id, TagValue)`.
2. Устаревание: фоновый/ленивый `stale` если `now - server_ts > k * period`.
3. Write path: `pending` → `confirmed`/`rejected` с отдельным quality transition (документировать в коде).
4. UA **читает только TagStore**, не кэширует параллельную правду.

### Потокобезопасность

- `publish` / `get` / `subscribe_changes` потокобезопасны.
- Внутренности: mutex per shard или concurrent hashmap; запрещено держать lock на время I/O.
- Change notifications — через executor callback (не re-entrant в publish).

## Alternatives

| Вариант | Почему отклонён |
|---------|-----------------|
| Каждый модуль хранит свои копии | Расхождение данных |
| Только OPC UA как store | Сильная связность с northbound; хуже тестировать |

## Consequences

- Translator не пишет в UA напрямую.
- Historian подписывается на store или получает hook из publish.
- Модель Quality ↔ UA StatusCode — единая таблица в core/adapter UA ([04](../04-opcua-information-model.md)).
