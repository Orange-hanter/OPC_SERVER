# ADR-0009: Northbound OPC UA boundary

- **Status:** Accepted
- **Date:** 2026-08-11

## Context

Имя репозитория исторически «OPC», SCADA-мир всё ещё встречает Classic/DA. Нужна жёсткая граница, чтобы Classic не протекал в core.

## Decision

1. **Единственный northbound в ядре — OPC UA** (`IOpcUaFacade` / adapter open62541).
2. OPC Classic/DA **out of scope** ядра. Допускается только внешний процесс-мост или будущий optional adapter за тем же `ITagStore`, без COM в `core`.
3. Information Model строится из `Project` (`nodePath`), не из DA ItemID.
4. Security: lab `None`; industrial `SignAndEncrypt` + Basic256Sha256 — конфигурация проекта/runtime, не хардкод в core.

## Alternatives

| Вариант | Почему отклонён |
|---------|-----------------|
| Dual stack UA+DA в одном процессе v1 | Раздувает поверхность атаки и зависимостей |
| Только DA | Не соответствует цели современного SCADA-контура |

## Consequences

- Любой PR с COM/DCOM в `core/` — reject.
- Документация Classic только в overview/ADR, не в runtime guide как supported feature.
