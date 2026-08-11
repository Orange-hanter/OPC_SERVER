# ADR-0007: Modbus transport port

- **Status:** Accepted
- **Date:** 2026-08-11

## Context

Нужен опрос TCP сейчас, UDP позже, и fake для тестов. Вшивание MBAP/семантики FC прямо в Dispatcher убивает тестируемость.

## Decision

Порт `IModbusTransport` (async-friendly):

```text
connect(endpoint) -> Result<void>
close()
read_holding(unit, addr, qty)  -> Result<vector<uint16_t>>
read_input(...)
read_coils(...)
read_discrete(...)
write_register / write_registers / write_coil / write_coils
```

Требования:

1. **Один экземпляр transport на endpoint**, вызывается только из endpoint strand ([ADR-0002](0002-concurrency-model.md)).
2. Таймауты — параметр запроса или свойства endpoint; transport возвращает `ErrorCode::Timeout`.
3. Modbus exception code → `ErrorCode::ModbusException` + деталь в `Error`.
4. PDU encode/decode может жить в `adapters/modbus` или `core/modbus_codec` (pure); framing MBAP — в TCP adapter.
5. Реализация по умолчанию: **Asio-native** (не libmodbus как жёсткая зависимость ядра). `Lib/modbuspp` не использовать.

Асинхронный вариант API (предпочтительный для Asio): completion token / `async_` методы, но порт описывается так, чтобы Fake был синхронно-детерминированным под virtual clock.

## Alternatives

| Вариант | Почему отклонён |
|---------|-----------------|
| libmodbus везде | Синхронная модель хуже стыкуется со strand; сложнее fake |
| Callback hell без порта | Нет component-тестов |

## Consequences

- Stage 2 deliverable: `FakeModbusTransport` + `AsioModbusTcpTransport` за одним портом.
- UDP — новый adapter, тот же порт.
