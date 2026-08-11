# ADR-0013: Post-MVP hardening (Stage 1–4 review)

- **Status:** Accepted
- **Date:** 2026-08-11

## Context

После Stages 1–4 (карты, poller, Runtime, OPC UA Read/Write/Subscriptions) ревью выявило:

- потерю хвоста write-batch при ошибке `flush_writes`;
- стирание engineering value при Bad publish;
- UA Write всегда `Good` (ValueCallback после commit);
- `dynamic_cast` к `OpcUaServer` и зависимость adapters→core через `RuntimeIndex`;
- Read из локального node store, а не из TagStore (дрейф с ADR-0006).

## Decision

1. **`UA_DataSource`** для всех теговых Variable: Read = `ITagStore::get`, Write = validate + `OpcUaWriteHandler` с возвратом StatusCode.
2. **`IOpcUaFacade::bind_tags` / `set_write_handler`** — composition root без `dynamic_cast` и без `core::RuntimeIndex` в adapters.
3. **`domain::with_quality`** — смена Quality/Reason с сохранением последнего ScalarValue.
4. **`flush_writes`**: при ошибке оставшиеся элементы batch возвращаются в очередь; глубина очереди ограничена (`QueueFull`).
5. **`poll_due`** пробрасывает первую ошибку poll-group (группы продолжают обрабатываться).
6. **`ServerRuntime::start`**: любой failure после side-effects → `stop()` rollback.
7. **Install/version**: `OPC_SERVER --version`, generated `version.hpp`, `cmake --install`.

## Consequences

- Subscriptions опираются на sampling DataSource (не на `writeDataValue` mirror).
- adapters линкуются на `opc::project` + open62541, не на `opc::core`.
- Следующий инкремент: Asio reactor (убрать sleep), TSan job, bounded executor для write с UA-thread.
