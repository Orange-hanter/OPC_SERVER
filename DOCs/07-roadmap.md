# 07. Roadmap реализации

Документация в `DOCs/` задаёт целевое состояние. Ниже — **снимок факта** (что уже в `master`) и **порядок следующих инкрементов**. Исторический backlog опроса IoT/Modbus сохранён в [tasks.md](tasks.md) и покрывается этапами 1–2.

Снимок: **2026-08-13**, `master` после PR #6 (CMake/Conan) и #8 (Engineering Studio). Открытых PR нет.

## Где мы сейчас

Лабораторный контур **Modbus TCP → TagStore → OPC UA (Read/Write/Subscriptions)** работает end-to-end. Рядом: historian/frame-log, spdlog/OTel metrics, `opc-map` + doctor, Tauri Studio (read-only monitor).

Это **не** промышленный runtime: цикл опроса крутится через `sleep` в `Application::run()`, Asio strand-per-endpoint из [ADR-0002](adr/0002-concurrency-model.md) ещё не внедрён, security OPC UA — None, OTLP/traces — opt-in/будущее.

| Контур | Состояние |
|--------|-----------|
| Карты `*.modbusproj.json` + semantic validate | Есть |
| `opc-map validate` / `doctor` / `migrate-legacy` | Есть |
| TagStore, Translator, Dispatcher (`writes_first`) | Есть |
| Sync Modbus TCP (holding/input/coil/discrete; write FC05/06/16) | Есть |
| `ServerRuntime` + CLI (`--once`/`--watch`/historian/log/metrics) | Есть |
| OPC UA DataSource Read + Write + MonitoredItems | Есть |
| Diagnostics `Objects/OPC_SERVER/Diagnostics` | Есть |
| Historian hot ring + SQLite cold + frame log + replay | Есть |
| spdlog + OTel metrics (`none`/`ostream`/`otlp`) | Есть (OTLP только `-DOPC_WITH_OTLP=ON`) |
| Modular CMake presets + Conan 2 + CI artifacts | Есть |
| Engineering Studio (Tauri 2) + `opc-monitor` | Есть |
| JSON Schema **engine** (draft 2020-12) | Нет — semantic checks |
| Asio reactor / `steady_timer` вместо `sleep` | Нет — **следующий инкремент** |
| TSan CI, reconnect backoff, `mark_stale` в runtime | Нет |
| `import-csv` / `gen-nodeset` / профили устройств | Нет |
| Sign / SignAndEncrypt | Нет |

Тесты: ~40 Catch2 cases (project, doctor, core, UA smoke, historian, frame replay, opc-monitor) + Studio Vitest. Preset `asan` есть локально, в CI не гоняется.

## Этапы (факт)

### Этап 0 — Каркас документации и формата проекта

- [x] Обзор, архитектура, стек, модель UA, ops-доки
- [x] JSON Schema и пример `*.modbusproj.json`
- [x] Обновление корневого README со ссылкой на `DOCs/`

### Этап 1 — Формат проекта и `opc-map` (минимум)

- [x] Парсер `*.modbusproj.json` + семантическая валидация (по правилам Schema)
- [x] `opc-map validate` / `opc-map migrate-legacy` (из [config.json](config.json))
- [x] Unit-тесты на примеры из `DOCs/examples/`
- [ ] Полная проверка через JSON Schema draft 2020-12 engine (сейчас — эквивалентные semantic checks)

### Этап 1.5 — Архитектурный каркас (обязателен до poller)

- [x] Engineering standards, quality gates, CONTRIBUTING
- [x] ADR-0001…0010 (hexagon, concurrency, errors, testing, config, tagstore, transport, observability, UA boundary, scale)
- [x] Скелет `domain/` / `ports/` / `core/` / `adapters/testsupport`
- [x] TagStore + FakeModbusTransport + тесты
- [x] `.clang-format` / `.clang-tidy`

### Этап 2 — ModbusPoller + Translator (по ADR)

- [x] Реализация `Translator::decode/encode` + тесты byte order
- [x] `ModbusTcpTransport` (sync TCP/MBAP за `IModbusTransport`; Asio reactor — следующий инкремент)
- [x] `Dispatcher::poll_due` + write queue (`writes_first`) + Fake component tests
- [x] `RuntimeIndex` (TagId ↔ project tags)
- [x] Watchlist в консоли / app composition root
- [ ] Полный reactor (Asio `io_context` + strand per endpoint + timers) — см. инкремент A
- [ ] FC15 (`write_multiple_coils`) — порт сейчас только `write_single_coil`

### Этап 2.5 — Runtime infrastructure

- [x] `ServerRuntime` composition root + `TransportFactory`
- [x] `Application` CLI (`--project`, `--once`, `--watch`, `--version`, historian/log/metrics)
- [x] `ILog` (`SpdlogLog` / `StderrLog`), `ManualClock`, `IMetrics`
- [x] Bootstrap tests with Fake transport
- [ ] Asio reactor loop (убрать `sleep_for` в `Application::run`) — инкремент A
- [ ] Честный reconnect backoff (`reconnectDelayMs` загружается, но не выдерживается)
- [ ] Runtime `TagStore::mark_stale_before` при обрыве связи (API есть, в poll loop не вызван)

### Этап 3 — OPC UA Read

- [x] open62541 (+ C++ обёртка `OpcUaServer`), построение дерева из `nodePath`
- [x] Read из TagStore (DataSource), security None для стенда
- [x] Smoke-тест UA-клиентом (`tests/test_opc_ua_read.cpp`)
- [x] CLI `--no-opcua`, проводка в `ServerRuntime` / `Application`

### Этап 4 — Subscriptions и Write

- [x] MonitoredItems / Publish (DataSource sampling + TagStore) для Studio/SCADA
- [x] Write path → Dispatcher queue → FC06/16/05 (`OpcUaServer` DataSource write)
- [x] Политика `writes_first`, маппинг StatusCode / WritePending
- [x] Smoke-тесты UA Write + Subscription (`tests/test_opc_ua_write_subs.cpp`)

### Этап 4.5 — Hardening (ревью Stages 1–4)

- [x] ADR-0013: DataSource, facade без `dynamic_cast`, preserve value, write-queue bounds
- [x] `cmake --install` + `OPC_SERVER --version`
- [x] Modular CMake + Conan 2 + presets (`dev` / `ci` / `asan` / `unity`)
- [ ] Asio reactor / убрать sleep (перенос из 2.5) — инкремент A
- [ ] TSan CI job
- [ ] ASan/UBSan job в GitHub Actions (preset `asan` уже есть)

### Этап 5 — Historian и Debug

- [x] Hot ring buffer (`RingHistorian`), cold SQLite (`SqliteHistorian`) — ADR-0014
- [x] Frame log Modbus (`IFrameLog` / `FileFrameLog`)
- [x] spdlog (`SpdlogLog`) + OpenTelemetry metrics (`OtelMetrics`, ADR-0015)
- [x] Replay для отладки (`historian_replay.hpp`, `ReplayModbusTransport` / frame log)
- [ ] OTLP default-on in CI / traces for poll-write
- [ ] Метрики `ua_sessions`, `tag_quality` из [ADR-0008](adr/0008-observability.md)

### Этап 6 — Удобство разметки карт

- [x] `opc-map doctor` (static overlaps, holes, unpolled tags, sparse blocks)
- [x] Tauri Engineering Studio: локальный редактор проектов + удалённый
  read-only OPC UA мониторинг (Windows/Linux/macOS) — ADR-0016
- [x] `opc-monitor` JSON Lines sidecar (Browse / Subscriptions / reconnect)
- [ ] `opc-map import-csv`
- [ ] `opc-map gen-nodeset`
- [ ] Профили устройств и библиотека шаблонов
- [ ] Runtime doctor (частота exception, теги никогда не Good) — [06](06-dispatch-debug-store.md)

### Этап 7 — Промышленное укрепление

- Sign / SignAndEncrypt, сертификаты (open62541 encryption build)
- Нагрузочные тесты (число тегов, RTT)
- UDP Modbus при необходимости
- (Опционально) исследование внешнего моста UA↔Classic/DA — **не** ядро

## Следующие инкременты (порядок работ)

Не оценивать календарём. Один инкремент = одна вертикаль с тестами и обновлением этого файла.

### A — Asio reactor (обязательный следующий код)

**Зачем:** ADR-0002 и anti-DoD в [10](10-quality-gates.md) запрещают `sleep` в production-пути. Сейчас `Application::run()` спит `watch_period_ms`, а `ModbusTcpTransport` — блокирующий POSIX TCP. Один медленный slave блокирует остальные endpoints.

**Состав:**

1. Asio только в `adapters/` (hexagon: `core`/`ports` без Asio).
2. `io_context` + worker threads; **strand на каждый Modbus endpoint**.
3. `steady_timer` по `pollGroups[].periodMs` вместо внешнего sleep.
4. Write с UA-thread: `post` на strand endpoint, не синхронный `flush_writes` из callback.
5. Reconnect state machine с `reconnectDelayMs` на том же strand.
6. Вызов `mark_stale_before` / Bad NoCommunication при обрыве, чтобы UA Diagnostics и SCADA видели потерю связи.

**DoD:** `--once` без reactor-loop по-прежнему работает; `--watch` без `sleep_for`; тесты Dispatcher на Fake не ломаются; новый component-тест на два endpoint (медленный не стопает быстрый) либо эквивалент под fake clock; ASan чистый на затронутых тестах.

**Не делать в этом инкременте:** SignAndEncrypt, CSV import, OTLP default-on.

### B — Качество CI и completeness poller

После A, можно параллелить:

- TSan job (TagStore / Dispatcher / write queue).
- ASan preset в CI (не только локально).
- JSON Schema engine в `opc-map validate` (Stage 1 leftover).
- FC15 `write_multiple_coils` на порте и в Dispatcher.
- Метрики `ua_sessions` / `tag_quality`.

### C — Инструменты карт (остаток этапа 6)

- `opc-map import-csv` по контракту в [03](03-modbus-projects.md).
- `opc-map gen-nodeset` (dump дерева / фрагмент UA).
- Профили устройств (`deviceProfiles[]` в схеме уже намечены).
- Runtime doctor — отдельный порт/CLI, не смешивать со static `opc-map doctor`.

### D — Промышленный security и нагрузка (этап 7)

Только когда A закрыт: иначе SignAndEncrypt на `sleep`-цикле не имеет смысла как «промышленный» runtime.

- open62541 с encryption; security mode из проекта не игнорировать.
- Studio/opc-monitor: сертификатный профиль (сейчас явно None).
- Нагрузочный стенд: теги × endpoints × UA subscriptions.
- UDP transport — отдельный адаптер за тем же `IModbusTransport`.

## Критерии готовности

### Лабораторный MVP (конец этапа 4) — выполнен

1. Проект `demo-plant.modbusproj.json` загружается без ошибок.
2. Значения с Modbus TCP видны в UA-клиенте (Read + Subscription).
3. Запись уставки из UA доходит до регистра и подтверждается quality Good.
4. Quality Bad/Uncertain выставляется на ошибках poll/write (полный stale-on-disconnect — инкремент A).

### Следующий milestone: reactor MVP (инкремент A)

1. Нет `sleep` в `Application::run` / runtime-пути.
2. Изоляция endpoint: I/O одного устройства не блокирует strand другого.
3. UA Write не вызывает transport с потока open62541.
4. Обрыв связи → теги endpoint уходят в Bad/Uncertain предсказуемо.

## Связь с текущим репозиторием

| Сейчас | Следующая цель |
|--------|----------------|
| `sleep` + sync TCP в `Application` / `ModbusTcpTransport` | Asio `io_context` + strand-per-endpoint (инкремент A) |
| `reconnectDelayMs` в JSON, retry каждый poll | backoff state machine |
| `opc-map` validate/doctor/migrate | import-csv / gen-nodeset |
| Studio + opc-monitor, security None | SignAndEncrypt + cert profile |
| OTel metrics, OTLP opt-in | traces poll/write; OTLP в CI по флагу |
| GCC CI + Conan + Studio matrix | TSan + ASan jobs |

## Вне roadmap ядра

- Встроенный OPC DA/Classic server
- Полноценная SCADA
- Произвольные полевые протоколы без отдельного эпика Translator
- HTTP/WebSocket API внутри `OPC_SERVER` (отклонён ADR-0016)
