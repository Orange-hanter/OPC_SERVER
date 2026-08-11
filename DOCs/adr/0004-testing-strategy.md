# ADR-0004: Testing strategy

- **Status:** Accepted
- **Date:** 2026-08-11

## Context

Шлюз нельзя «проверить глазами» на объекте как основной метод. Нужна пирамида, где core тестируется без PLC и без SCADA.

## Decision

### Пирамида

```text
        E2E (opt-in, real/device lab)
           / \
    Contract (demo-plant, opc-map)
         /     \
 Component (fake transport + core) 
       /         \
  Unit (domain, translator, validate)
```

| Уровень | Что | Как |
|---------|-----|-----|
| Unit | byte order, scale/offset, validate xref, Error mapping | Catch2, без сети |
| Component | poll group cycle, write queue priority, quality stale | `FakeModbusTransport` + core |
| Contract | `demo-plant.modbusproj.json` loads; migrate-legacy roundtrip | уже есть зачатки |
| E2E | UA client ↔ server ↔ modbus simulator | opt-in `OPC_E2E=1`, не в default CI |

### Правила

1. Новый порт → сразу fake + хотя бы один component test.
2. Баг из поля → regression test на том уровне, где воспроизводится дёшево.
3. Запрещены flaky sleeps; только virtual clock (`IClock`) в unit/component.
4. Determinism: тесты не зависят от wall-clock кроме явно marked integration.

### Ownership тестов

- `tests/unit` — domain/project/translator
- `tests/component` — core + fakes
- `tests/contract` — файлы из `DOCs/examples`

## Alternatives

| Вариант | Почему отклонён |
|---------|-----------------|
| Только E2E | Медленно, хрупко, плохо локализует |
| Mocks на всём GoogleMock без портов | Стимулирует тестировать детали реализации |

## Consequences

- Этап 2: Fake transport — deliverable вместе с Poller.
- Coverage gate сначала на `domain` + `project`, затем `core`.
