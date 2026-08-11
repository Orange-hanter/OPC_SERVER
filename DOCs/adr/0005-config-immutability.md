# ADR-0005: Config / project immutability

- **Status:** Accepted
- **Date:** 2026-08-11

## Context

Карта Modbus — источник истины для опроса и UA-дерева. Hot-reload «на лету» без модели версий приводит к гонкам: poller читает старые регистры, UA уже раздал новые NodeId.

## Decision

1. **`Project` immutable после успешного load** (shared_ptr to const / frozen snapshot).
2. Runtime держит `shared_ptr<const Project>` (или аналог).
3. **Hot-reload = отдельный эпик:** load → validate → build new graph → atomic swap epoch → drain old.
4. Engineering changes идут через файлы + `opc-map validate`, не через скрытый API мутации карт в процессе (v1).
5. Legacy `config.json` только через `migrate-legacy` → `*.modbusproj.json`.

## Alternatives

| Вариант | Почему отклонён |
|---------|-----------------|
| Mutable global config | Гонки, невоспроизводимые баги |
| DB как primary config в v1 | Усложняет engineering UX; файлы прозрачнее |

## Consequences

- Core принимает `const Project&` / `shared_ptr<const Project>`.
- Тесты могут шарить один project snapshot.
- UI редактора карт (будущее) пишет файл и перезапускает/делает versioned reload — не патчит TagStore напрямую.
