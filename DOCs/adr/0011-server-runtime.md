# ADR-0011: ServerRuntime composition root

- **Status:** Accepted
- **Date:** 2026-08-11

## Context

После появления Project, TagStore, Dispatcher и transport нужны **фундаментальные объекты приложения**, через которые собирается рабочий контур, а не разрозненные вызовы в `main`.

## Decision

1. **`opc::app::ServerRuntime`** — composition root southbound+core:
   - владеет `RuntimeIndex`, `TagStore`, `Dispatcher`, map transports;
   - принимает injected `IClock`, `IMetrics`, `ILog`;
   - создаёт transports через **`TransportFactory`** (тест подменяет Fake).
2. **`opc::app::Application`** — CLI + lifecycle (`init` / `run` / `--once` / `--watch`).
3. **`ILog` + StderrLog/NullLog**, **`ManualClock`** — инфраструктурные порты/адаптеры.
4. ServerRuntime **неmovable** (указатели Dispatcher → TagStore); фабрика возвращает `unique_ptr`.

## Consequences

- Тесты бутстрапа не поднимают реальный PLC.
- UA/Historian подключаются позже теми же deps без переписывания poll path.
- `main.cpp` остаётся тонким.
