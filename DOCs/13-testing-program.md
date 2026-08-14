# 13. Программа тестирования OPC_SERVER

Нормативная программа качества промышленного шлюза **Modbus TCP → OPC UA**.
Стратегические решения — в [ADR-0004](adr/0004-testing-strategy.md).
Ворота merge — в [10-quality-gates.md](10-quality-gates.md).
CI/nightly/lab — в [11-ci-and-releases.md](11-ci-and-releases.md).

Шлюз нельзя «проверить глазами» на объекте как основной метод. Каждый баг из поля
классифицируется в один уровень таксономии и имеет место в **default CI**,
**nightly** или **lab**.

Продукт **не SIS**. IEC 61508 / SIL не являются целью, пока шлюз не позиционируется
как safety-функция. Берём практики стандартов, а не сертификационный театр.

## Таксономия и пирамида

```text
                    Lab / release
                 (CTT, FAT/SAT, HIL)
                         |
              Nightly / opt-in
         (TSan, E2E, fuzz, soak, coverage)
                         |
                 Default CI
    static · unit · component · contract · UA smoke · ASan · Studio
```

| Уровень | Где живёт | Когда гоняется | Что доказывает |
|---------|-----------|----------------|----------------|
| Static | clang-format, clang-tidy, `scripts/layer-lint.py` | PR | Слои hexagon, стиль |
| Unit | `tests/unit/` | PR | Codec, типы, Quality, validate без I/O |
| Component | `tests/component/` | PR | Dispatcher/TagStore + `FakeModbusTransport` |
| Contract | `tests/contract/` | PR | demo-plant, schema, CLI, паритет Ajv |
| Integration | `tests/integration/` | PR (короткий loopback) | Реальный MBAP/TCP и open62541 client |
| E2E / lab | `tests/lab/` | `OPC_E2E=1`, nightly | MVP-сценарий процесса сервера |
| Frontend | Vitest + Playwright mock | PR | Studio без `opc.tcp` |
| Native Studio | Playwright skip / sidecar | opt-in | Tauri + `opc-monitor` |
| Conformance | OPC Foundation CTT | lab / release | Профиль UA, не блокер merge |
| Acceptance | FAT/SAT чеклисты | стенд / объект | Приёмка ICS |
| Load / soak | `opc_bench`, `OPC_SOAK=1` | nightly / этап 7 | Ёмкость и утечки |
| Fuzz | `fuzz/` | Clang nightly | Парсеры JSON и MBAP |
| Security | SAST, IPC limits, certs | PR advisory + lab | IEC 62443-4-1 практики |

Catch2-теги (можно комбинировать):

```text
[unit] [component] [contract] [integration] [e2e] [soak] [benchmark]
[core] [project] [adapters] [app] [opcua] [modbus] [studio]
```

Фильтр:

```bash
./build/dev/tests/opc_tests "[unit]"
./build/dev/tests/opc_tests "[integration][modbus]"
OPC_E2E=1 ./build/dev/tests/opc_tests "[e2e]"
```

## Стандарты: что берём

| Стандарт | Роль в программе |
|----------|------------------|
| ISO/IEC 25010 | Оси качества: functional suitability, reliability, performance, compatibility, security, maintainability. Каждая ось закрывается своим видом тестов. |
| ISO/IEC/IEEE 29119 + IEEE 1012 | Уровни unit / integration / system / acceptance и трассировка требование → тест (матрица ниже). |
| IEC 62443-4-1 | Secure development: SAST, fuzz парсеров, regression на класс уязвимостей. |
| IEC 62443-4-2 | Чеклист компонента: сессии UA, целостность конфигов, least privilege Studio (read-only northbound). |
| OPC UA Part 4/8 | Семантика Read/Write/Subscriptions, DataValue, StatusCode. |
| OPC Foundation CTT | Opt-in lab conformance. Smoke на open62541 **не** заменяет CTT. |
| Modbus Application Protocol + TCP/IP Implementation Guide | FC01/02/03/04/05/06/15/16, exception 02/03, MBAP transaction id. |
| FAT/SAT (практика ICS, близко к IEC 62381) | Ручные протоколы: [testing/fat-checklist.md](testing/fat-checklist.md), [testing/sat-checklist.md](testing/sat-checklist.md). |
| NAMUR NE 107 | Диагностическая семантика; сверка Quality → UA StatusCode. |

**Не тащим в v1:** IEC 61511, OPC Classic/DA CTT, полный NIST SP 800-82 audit,
коммерческие SCADA в CI (UaExpert / Ignition / WinCC — только ручной стенд).

## Матрица требование → тест

Требования из [01-overview.md](01-overview.md) и критериев MVP [07-roadmap.md](07-roadmap.md).

| ID | Требование | Уровень | Где |
|----|------------|---------|-----|
| FR-1 | Загрузка и валидация `*.modbusproj.json` | Contract | `tests/contract/test_project.cpp`, `opc-map validate` |
| FR-2 | Циклический опрос Holding/Input/Coil/Discrete | Component + Integration | Fake + `LoopbackModbusSlave`; Dispatcher все 4 area; FC02/FC04/packed coils |
| FR-3 | Публикация DataValue (значение, Quality, timestamps) | Unit + UA smoke | Translator, TagStore, `test_opc_ua_read.cpp` |
| FR-4 | UA Write → очередь Modbus, `writes_first` | Component + UA smoke | Dispatcher, `test_opc_ua_write_subs.cpp` |
| FR-5 | Отладка: frame log, watchlist, метрики | Component | historian, frame_log, spdlog/otel |
| FR-6 | Historian hot/cold + replay | Component | `test_historian.cpp`, `test_frame_replay.cpp` |
| NFR-1 | Детерминизм опроса, изоляция endpoint | Component | `ManualClock`, два endpoint |
| NFR-2 | Quality отражает timeout / exception / stale | Unit + Component | `quality_to_status`, Fake fail/inject, DecodingError, WriteRejected, exception 02/03 |
| NFR-3 | UA security None (lab); SignAndEncrypt — этап 7 | Lab | [testing/opc-ua-ctt.md](testing/opc-ua-ctt.md) |
| NFR-4 | Наблюдаемость полей логов и метрик | Component | spdlog/otel tests; soak-контракт метрик |
| NFR-5 | Инженерия карт, schema, CLI | Contract + Studio | fixtures + Ajv parity + Vitest |
| MVP-1 | `demo-plant` загружается | Contract | `test_project.cpp` |
| MVP-2 | Modbus TCP значения видны в UA Read/Sub | E2E opt-in | `tests/lab/test_e2e_mvp.cpp` |
| MVP-3 | UA Write доходит до регистра, Quality Good | E2E + UA smoke | write_subs + e2e |
| MVP-4 | Обрыв связи → Bad/Uncertain предсказуемо | Component + E2E | Fake disconnect, e2e drop |

Studio (ADR-0016): редактор и read-only monitor не имеют права инициировать UA Write.

## Правила (дополнение ADR-0004)

1. Новый порт → сразу fake + хотя бы один component test.
2. Баг из поля → regression на самом дешёвом воспроизводимом уровне.
3. Запрещены flaky `sleep` в unit/component; только `IClock` / `ManualClock`.
4. Сеть в default CI — только `127.0.0.1` и ephemeral ports.
5. Реальный PLC — только FAT/HIL, никогда default CI.
6. `OPC_E2E=1` включает lab E2E; без флага кейс `SKIP`.
7. Coverage gate только на `domain` + `core` + `project` (не adapters I/O).
8. Core не включает `adapters/`; это проверяет `scripts/layer-lint.py`.

## Технологии

**Оставляем**

- Catch2 3.x + CTest `catch_discover_tests`
- `FakeModbusTransport`, `ManualClock`, `NullMetrics` / `NullLog`
- open62541 client для UA smoke
- Vitest, Testing Library, Playwright (browser mock)
- CMake presets `dev` / `ci` / `asan`

**Добавляем**

- Catch2 `GENERATOR` для property roundtrip Translator
- C++ `LoopbackModbusSlave` в `tests/support/` (без Python в default CI)
- `scripts/layer-lint.py`
- llvm-cov / gcov (`OPC_ENABLE_COVERAGE`)
- CMake preset `tsan`
- libFuzzer-таргет `project_load_fuzzer` (`OPC_ENABLE_FUZZERS`, Clang)
- Catch2 benchmark-таргет `opc_bench` (не в default ctest)
- `cargo test` для sidecar path/IPC guards
- OPC Foundation CTT — документация и opt-in script, не submodule ядра

**Не берём**

- GoogleMock как основной стиль (тестируем порты, не моки методов)
- Cypress, Testcontainers, Cucumber/Gherkin (FAT — Markdown-чеклисты)

## Каталоги тестов

```text
tests/
  support/           # repo_root, free_tcp_port, LoopbackModbusSlave
  unit/
  component/
  contract/
  integration/
  lab/               # E2E, soak; SKIP без env
  fixtures/          # JSON для паритета C++ / Ajv
fuzz/
  project_load_fuzzer.cpp
scripts/
  layer-lint.py
  coverage.sh
  run-e2e.sh
  soak-runtime.sh
  run-opc-ua-ctt.sh
DOCs/testing/
  fat-checklist.md
  sat-checklist.md
  opc-ua-ctt.md
```

Один CMake-таргет `opc_tests` (кроме `opc_bench` и fuzzer).

## CI vs nightly vs lab

| Лента | Содержание | Блокер merge |
|-------|------------|--------------|
| Default CI (PR) | GCC `ci` + `ctest`; ASan/UBSan; layer-lint; Studio lint/tsc/vitest/playwright mock | Да |
| Nightly / `workflow_dispatch` | TSan; `OPC_E2E=1`; coverage отчёт; fuzz (Clang); soak короткий | Нет, кроме регрессии TSan перед Asio |
| Lab / release | OPC UA CTT, FAT, HIL PLC, SignAndEncrypt, native Tauri, коммерческий SCADA | Релизный gate, не PR |

Короткий Modbus TCP loopback **входит в default CI**: это миллисекунды на 127.0.0.1
и единственная страховка `ModbusTcpTransport`. Полный процессный E2E — nightly.

## Покрытие

```bash
cmake --preset coverage
cmake --build --preset coverage
ctest --preset coverage
./scripts/coverage.sh
```

Порог (этап 2 программы): не падать ниже **70% line** на `Src/domain`,
`Src/core`, `Src/project`. Adapters не гейтятся.

## Studio

- Vitest: schema, i18n, Monitor, adapters.
- Playwright mock — быстрый PR.
- Паритет Ajv ↔ C++ на `tests/fixtures/` и `DOCs/examples/demo-plant.modbusproj.json`.
- `cargo test` — path suffix, 16 MiB limit, JSONL event mapping.
- Native Tauri + sidecar: `OPC_STUDIO_NATIVE=1` (не default CI).

## Связанные документы

- [ADR-0004](adr/0004-testing-strategy.md), [ADR-0002](adr/0002-concurrency-model.md) (TSan)
- [08-engineering-standards.md](08-engineering-standards.md)
- [10-quality-gates.md](10-quality-gates.md), [11-ci-and-releases.md](11-ci-and-releases.md)
- [04-opcua-information-model.md](04-opcua-information-model.md) (Quality → StatusCode)
- [testing/fat-checklist.md](testing/fat-checklist.md), [testing/sat-checklist.md](testing/sat-checklist.md)
- [testing/opc-ua-ctt.md](testing/opc-ua-ctt.md)
