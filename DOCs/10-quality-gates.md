# 10. Quality gates

Минимальные ворота качества до merge. Пока CI в GitHub Actions может быть неполным —
агент/разработчик обязан прогнать локально то же самое.

Программа тестирования: [13-testing-program.md](13-testing-program.md). Стратегия: [ADR-0004](adr/0004-testing-strategy.md).

## Обязательные gates

| Gate | Команда / критерий |
|------|--------------------|
| Configure | `cmake --preset dev` (или `ci`) |
| Build | `cmake --build --preset dev` |
| Unit/component/contract/integration | `ctest --preset dev --output-on-failure` — 100% pass (без `OPC_E2E`) |
| Layer lint | `python3 scripts/layer-lint.py` — 0 нарушений hexagon |
| CI | Workflow **CI** green on PR (GCC build+test + ASan + Studio quality) |
| Format | `clang-format -i` на изменённых `*.hpp/*.cpp`; diff format clean |
| Architecture | Diff не нарушает таблицу зависимостей из [08](08-engineering-standards.md) |
| ADR | Если меняется граница слоёв / concurrency / ошибки / тесты — есть ADR или update |
| Observability | Новый I/O path: есть log fields + хотя бы одна метрика (когда подсистема уже есть) |
| Tests as contract | Новое поведение — unit или component с fake port (DoD) |

## Рекомендуемые gates (включать по мере появления toolchain)

| Gate | Критерий |
|------|----------|
| clang-tidy | Нет новых warnings на изменённых файлах |
| ASan/UBSan | Preset `asan` — clean (в CI на каждом PR) |
| TSan | Preset `tsan`, тесты TagStore/Dispatcher — clean (nightly; блокер до Asio) |
| Coverage | `scripts/coverage.sh` ≥ 70% line на `Src/domain`, `Src/core`, `Src/project` |
| Fuzz | `OPC_ENABLE_FUZZERS` + Clang; smoke corpus на JSON loader |
| Studio cargo test | `cargo test` в `frontend/apps/studio/src-tauri` |

## Definition of Done (фича)

1. Поведение описано тестом (unit или component с fake port). Баг из поля — regression.
2. Порт/адаптер разделены; core не тянет Asio/UA (`layer-lint`).
3. Ошибки — через `Result`/`expected`, не через «магический» bool без diagnostics.
4. Документация: либо update DOCs/ADR, либо явное «docs N/A» в PR.
5. `opc-map` / demo project не сломаны, если затронут формат.
6. Теги Catch2 выставлены (`[unit]` / `[component]` / …).

## Anti-DoD (не принимать)

- God-object `App` с сетевой логикой и парсингом JSON вперемешку.
- Глобальные mutable singleton’ы для TagStore.
- `sleep` в production-пути вместо таймеров Asio.
- Тест, который ходит в реальный PLC без opt-in env flag.
- Flaky `sleep` в unit/component вместо `ManualClock`.
- Снижение coverage `domain`/`core`/`project` ниже порога без явного ADR.
