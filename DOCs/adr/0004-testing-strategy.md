# ADR-0004: Testing strategy

- **Status:** Accepted
- **Date:** 2026-08-11
- **Updated:** 2026-08-13

## Context

Шлюз нельзя «проверить глазами» на объекте как основной метод. Нужна пирамида, где
core тестируется без PLC и без SCADA, плюс отдельные ленты для протокола, санитайзеров,
нагрузки и приёмки ICS.

Полная программа: [13-testing-program.md](../13-testing-program.md).

## Decision

### Пирамида

```text
        E2E / lab (opt-in, simulator or device)
           / \
    Contract (demo-plant, opc-map, Ajv parity)
         /     \
 Component (fake transport + core)
       /         \
  Unit (domain, translator, validate, Quality)
```

Рядом с пирамидой (не вместо неё): static layer-lint, ASan/UBSan, TSan,
fuzz, soak/benchmark, OPC UA CTT, FAT/SAT.

| Уровень | Что | Как |
|---------|-----|-----|
| Unit | byte order, scale/offset, Quality→StatusCode, Error mapping | Catch2, без сети, `GENERATOR` |
| Component | poll cycle, write queue, stale, exception inject | `FakeModbusTransport` + `ManualClock` |
| Contract | demo-plant; migrate-legacy; CLI exit codes; schema fixtures | `tests/contract`, `tests/fixtures` |
| Integration | MBAP/TCP loopback; UA client smoke | `127.0.0.1`, ephemeral ports |
| E2E | UA client ↔ `OPC_SERVER` ↔ Modbus slave | `OPC_E2E=1`, не default CI |
| Lab | CTT, FAT/SAT, HIL PLC, SignAndEncrypt | ручные протоколы |

### Ownership каталогов

- `tests/unit` — domain / translator / TagStore / quality mapping
- `tests/component` — core + fakes + historian/frame/runtime
- `tests/contract` — `DOCs/examples`, `tests/fixtures`, CLI
- `tests/integration` — реальный TCP/UA на loopback
- `tests/lab` — E2E/soak (`SKIP` без env)
- `tests/support` — slave, ports, repo_root
- `frontend/apps/studio` — Vitest / Playwright / `cargo test`

Один таргет `opc_tests`. Теги Catch2: `[unit]|[component]|[contract]|[integration]|[e2e]`.

### Правила

1. Новый порт → сразу fake + хотя бы один component test.
2. Баг из поля → regression test на том уровне, где воспроизводится дёшево.
3. Запрещены flaky sleeps; только virtual clock (`IClock`) в unit/component.
4. Determinism: тесты не зависят от wall-clock кроме явно tagged integration/e2e.
5. Coverage gate — `domain` + `core` + `project` (порог 70% line).
6. ASan/UBSan — default CI; TSan — nightly и блокер до merge Asio reactor.
7. Реальный PLC запрещён в CI (Anti-DoD).

### Стандарты (ориентир, не сертификат)

ISO/IEC 25010, ISO/IEC/IEEE 29119, IEEE 1012, IEC 62443-4-1/4-2 практики,
OPC UA Part 4/8 + CTT (lab), Modbus spec, FAT/SAT, NAMUR NE 107 для Quality.

SIL / IEC 61508 — вне объёма, пока продукт не safety-функция.

## Alternatives

| Вариант | Почему отклонён |
|---------|-----------------|
| Только E2E | Медленно, хрупко, плохо локализует |
| Mocks на всём GoogleMock без портов | Стимулирует тестировать детали реализации |
| Python pymodbus в default CI | Лишняя зависимость; C++ `LoopbackModbusSlave` достаточна |
| GTest рядом с Catch2 | Два раннера; Catch2 уже принят |

## Consequences

- Этап 2: Fake transport — deliverable вместе с Poller (выполнен; расширен coils/faults).
- Coverage gate сначала на `domain` + `project`, затем `core` (порог 70%).
- Документы FAT/SAT/CTT живут в `DOCs/testing/`.
- `OPC_E2E=1` и `OPC_SOAK=1` — единственные opt-in флаги автотестов с процессами/временем.
