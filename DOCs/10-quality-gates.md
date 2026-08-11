# 10. Quality gates

Минимальные ворота качества до merge. Пока CI в GitHub Actions может быть неполным — агент/разработчик обязан прогнать локально то же самое.

## Обязательные gates

| Gate | Команда / критерий |
|------|--------------------|
| Configure | `cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=g++ -DCMAKE_BUILD_TYPE=Debug` |
| Build | `cmake --build build` |
| Unit/component tests | `ctest --test-dir build --output-on-failure` — 100% pass |
| CI | Workflow **CI** green on PR (GCC build+test minimum) |
| Format | `clang-format -i` на изменённых `*.hpp/*.cpp`; diff format clean |
| Architecture | Diff не нарушает таблицу зависимостей из [08](08-engineering-standards.md) |
| ADR | Если меняется граница слоёв / concurrency / ошибки — есть ADR или update существующего |
| Observability | Новый I/O path: есть log fields + хотя бы одна метрика (когда подсистема уже есть) |

## Рекомендуемые gates (включать по мере появления toolchain)

| Gate | Критерий |
|------|----------|
| clang-tidy | Нет новых warnings на изменённых файлах |
| ASan/UBSan | Debug build + tests под sanitizers — clean |
| TSan | Тесты TagStore/Dispatcher — clean |
| Coverage | Не падать ниже порога на `domain`/`core` (порог ввести на этапе 2) |

## Definition of Done (фича)

1. Поведение описано тестом (unit или component с fake port).
2. Порт/адаптер разделены; core не тянет Asio/UA.
3. Ошибки — через `Result`/`expected`, не через «магический» bool без diagnostics.
4. Документация: либо update DOCs/ADR, либо явное «docs N/A» в PR.
5. `opc-map` / demo project не сломаны, если затронут формат.

## Anti-DoD (не принимать)

- God-object `App` с сетевой логикой и парсингом JSON вперемешку.
- Глобальные mutable singleton’ы для TagStore.
- `sleep` в production-пути вместо таймеров Asio.
- Тест, который ходит в реальный PLC без opt-in env flag.
