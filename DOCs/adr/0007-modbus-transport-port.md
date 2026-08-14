# ADR-0007: Modbus transport port

- **Status:** Accepted
- **Date:** 2026-08-11
- **Updated:** 2026-08-14 (async completion-token API)

## Context

Нужен опрос TCP сейчас, UDP позже, и fake для тестов. Вшивание MBAP/семантики FC прямо в Dispatcher убивает тестируемость. Sync facade блокировал endpoint strand на время `io_context::run()`.

## Decision

Порт `IModbusTransport`:

```text
# Sync (Fake / Replay / --once)
connect(endpoint) -> Result<void>
close()
read_holding / read_input / read_coils / read_discrete
write_register / write_registers / write_coil / write_coils

# Async completion tokens (reactor path)
async_connect(endpoint, handler)
async_read_* / async_write_*(..., handler)
set_completion_executor(IExecutor*)  # post completions onto endpoint strand
```

`ModbusCompletion<T> = move_only_function<void(Result<T>)>`.

Требования:

1. **Один экземпляр transport на endpoint**; sync вызовы — со strand; async completions — через `IExecutor` (= strand reactor).
2. Default async-реализации вызывают sync API inline (Fake/UDP/Replay без изменений).
3. `ModbusTcpTransport` / `AsioModbusTcpTransport`: private `io_context` + worker thread; async I/O не блокирует reactor strand; sync ждёт promise с `inline_completion` (без post на strand → нет deadlock).
4. `Dispatcher::poll_due_async` — цепочка connect → flush_writes → async reads; `ServerRuntime` tick использует async + `poll_inflight_` (skip overlapping ticks).
5. Таймауты / Modbus exception — как раньше (`ErrorCode::Timeout` / `ModbusException`).
6. `Lib/modbuspp` не использовать.

## Alternatives

| Вариант | Почему отклонён |
|---------|-----------------|
| libmodbus везде | Синхронная модель хуже стыкуется со strand; сложнее fake |
| Asio completion tokens в `ports/` | Ломает hexagon (Asio только в adapters) |
| Только sync + private run | Блокирует strand на I/O |

## Consequences

- Stage 2 deliverable: `FakeModbusTransport` + Asio TCP за одним портом (sync + async).
- UDP — тот же порт (async по умолчанию = sync bridge).
- Reactor path не держит strand на TCP round-trip.
