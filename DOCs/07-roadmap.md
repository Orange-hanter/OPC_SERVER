# 07. Roadmap реализации

Документация в `DOCs/` задаёт целевое состояние. Ниже — **снимок факта** и **порядок следующих инкрементов**. Живой backlog и процент выполнения: [tasks.md](tasks.md).

Снимок: **2026-08-14**. Инкременты A (Asio), B (CI/schema/FC15), C (карты: CSV/nodeset/профили/runtime doctor) и D (SignAndEncrypt, UDP, load stand) закрыты в этой линии веток. Сводка задач и процент: [tasks.md](tasks.md).

**Выполнение roadmap ядра (этапы 0–7, без Classic/DA):** ~98% пунктов чеклиста. Лабораторный MVP — 100%. Этап 7 / инкремент D — закрыт (с оговорками в tasks.md).

## Где мы сейчас

Лабораторный контур **Modbus TCP → TagStore → OPC UA (Read/Write/Subscriptions)** работает end-to-end. Рядом: historian/frame-log, spdlog/OTel metrics, `opc-map` + doctor, Tauri Studio (read-only monitor). **Asio reactor** (strand-per-endpoint, `steady_timer`, reconnect backoff) закрывает anti-DoD `sleep` из [ADR-0002](adr/0002-concurrency-model.md).

Это **ещё не** полный промышленный runtime: transport на strand остаётся блокирующим POSIX, demo-plant security — None (SignAndEncrypt доступен по проекту), OTLP/traces — opt-in/хвост этапа 5.

| Контур | Состояние |
|--------|-----------|
| Карты `*.modbusproj.json` + semantic validate | Есть |
| `opc-map validate` / `doctor` / `migrate-legacy` / `import-csv` / `gen-nodeset` | Есть |
| TagStore, Translator, Dispatcher (`writes_first`) | Есть |
| Sync Modbus TCP (holding/input/coil/discrete; write FC05/06/16) | Есть |
| `ServerRuntime` + CLI (`--once`/`--watch`/historian/log/metrics) | Есть |
| OPC UA DataSource Read + Write + MonitoredItems | Есть |
| Diagnostics `Objects/OPC_SERVER/Diagnostics` | Есть |
| Historian hot ring + SQLite cold + frame log + replay | Есть |
| spdlog + OTel metrics (`none`/`ostream`/`otlp`) | Есть (OTLP только `-DOPC_WITH_OTLP=ON`) |
| Modular CMake presets + Conan 2 + CI artifacts | Есть |
| Engineering Studio (Tauri 2) + `opc-monitor` | Есть |
| JSON Schema **engine** (draft 2020-12) | Есть — инкремент B |
| Asio reactor / `steady_timer` вместо `sleep` | Есть — инкремент A |
| Reconnect backoff + Bad/NoCommunication на endpoint | Есть (per-endpoint `mark_endpoint_bad`, не глобальный `mark_stale_before`) |
| TSan CI / ASan CI | Есть — инкремент B |
| `import-csv` / `gen-nodeset` / профили устройств / runtime doctor | Есть — инкремент C |
| Sign / SignAndEncrypt | Есть (fail-closed; lab demo остаётся None) |
| Modbus UDP | Есть (`endpoints[].transport = "udp"`) |
| Load stand | Есть (Catch2: 2 endpoints × N tags + UA sub smoke) |

Тесты: Catch2 (project, doctor, import-csv, gen-nodeset, runtime doctor, core, UA smoke/encryption, UDP, load stand, historian, frame replay, opc-monitor) + Studio Vitest. Presets `asan` / `tsan` гоняются в CI (инкремент B).

## Этапы (факт)

### Этап 0 — Каркас документации и формата проекта

- [x] Обзор, архитектура, стек, модель UA, ops-доки
- [x] JSON Schema и пример `*.modbusproj.json`
- [x] Обновление корневого README со ссылкой на `DOCs/`

### Этап 1 — Формат проекта и `opc-map` (минимум)

- [x] Парсер `*.modbusproj.json` + семантическая валидация (по правилам Schema)
- [x] `opc-map validate` / `opc-map migrate-legacy` (из [config.json](config.json))
- [x] Unit-тесты на примеры из `DOCs/examples/`
- [x] Полная проверка через JSON Schema draft 2020-12 engine (bundled schema + semantic checks)

### Этап 1.5 — Архитектурный каркас (обязателен до poller)

- [x] Engineering standards, quality gates, CONTRIBUTING
- [x] ADR-0001…0010 (hexagon, concurrency, errors, testing, config, tagstore, transport, observability, UA boundary, scale)
- [x] Скелет `domain/` / `ports/` / `core/` / `adapters/testsupport`
- [x] TagStore + FakeModbusTransport + тесты
- [x] `.clang-format` / `.clang-tidy`

### Этап 2 — ModbusPoller + Translator (по ADR)

- [x] Реализация `Translator::decode/encode` + тесты byte order
- [x] `ModbusTcpTransport` (sync TCP/MBAP за `IModbusTransport`; вызов только со strand)
- [x] `Dispatcher::poll_due` + write queue (`writes_first`) + Fake component tests
- [x] `RuntimeIndex` (TagId ↔ project tags)
- [x] Watchlist в консоли / app composition root
- [x] Полный reactor (Asio `io_context` + strand per endpoint + timers) — инкремент A
- [x] FC15 (`write_multiple_coils`) на порте и в Dispatcher (coalesce consecutive coils)

### Этап 2.5 — Runtime infrastructure

- [x] `ServerRuntime` composition root + `TransportFactory`
- [x] `Application` CLI (`--project`, `--once`, `--watch`, `--version`, historian/log/metrics)
- [x] `ILog` (`SpdlogLog` / `StderrLog`), `ManualClock`, `IMetrics`
- [x] Bootstrap tests with Fake transport
- [x] Asio reactor loop (убрать `sleep_for` в `Application::run`) — инкремент A
- [x] Честный reconnect backoff (`reconnectDelayMs` на strand; `--once` без backoff-skip)
- [x] Runtime Bad/NoCommunication при обрыве (`Dispatcher::mark_endpoint_bad` на endpoint;
      глобальный `TagStore::mark_stale_before` намеренно не зовётся, чтобы не портить соседние endpoints)

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
- [x] Asio reactor / убрать sleep (перенос из 2.5) — инкремент A
- [x] TSan CI job (core/runtime; UA smoke skipped)
- [x] ASan/UBSan job в GitHub Actions (preset `asan`)

### Этап 5 — Historian и Debug

- [x] Hot ring buffer (`RingHistorian`), cold SQLite (`SqliteHistorian`) — ADR-0014
- [x] Frame log Modbus (`IFrameLog` / `FileFrameLog`)
- [x] spdlog (`SpdlogLog`) + OpenTelemetry metrics (`OtelMetrics`, ADR-0015)
- [x] Replay для отладки (`historian_replay.hpp`, `ReplayModbusTransport` / frame log)
- [ ] OTLP default-on in CI / traces for poll-write
- [x] Метрики `ua_sessions`, `tag_quality` из [ADR-0008](adr/0008-observability.md)

### Этап 6 — Удобство разметки карт

- [x] `opc-map doctor` (static overlaps, holes, unpolled tags, sparse blocks)
- [x] Tauri Engineering Studio: локальный редактор проектов + удалённый
  read-only OPC UA мониторинг (Windows/Linux/macOS) — ADR-0016
- [x] `opc-monitor` JSON Lines sidecar (Browse / Subscriptions / reconnect)
- [x] `opc-map import-csv`
- [x] `opc-map gen-nodeset`
- [x] Профили устройств и библиотека шаблонов
- [x] Runtime doctor (частота exception, теги никогда не Good) — [06](06-dispatch-debug-store.md)

### Этап 7 — Промышленное укрепление

- [x] Sign / SignAndEncrypt, сертификаты (open62541 encryption build)
- [x] Нагрузочные тесты (число тегов, RTT)
- [x] UDP Modbus при необходимости
- (Опционально) исследование внешнего моста UA↔Classic/DA — **не** ядро

## Следующие инкременты (порядок работ)

Не оценивать календарём. Один инкремент = одна вертикаль с тестами и обновлением этого файла.

### A — Asio reactor (закрыт)

**Зачем:** ADR-0002 и anti-DoD в [10](10-quality-gates.md) запрещают `sleep` в production-пути.

**Состав (сделано):**

1. Asio только в `adapters/` (`AsioReactor` pimpl, headers без `<asio.hpp>`).
2. `io_context` + worker threads (≥2); **strand на каждый Modbus endpoint**.
3. `steady_timer` по min `pollGroups[].periodMs` endpoint; `--period-ms` — интервал watchlist.
4. Write с UA-thread: `enqueue_write` + `post` flush на strand; `--once` по-прежнему `poll_once`.
5. Reconnect backoff: skip `poll_due`, пока `now < next_reconnect` (`reconnectDelayMs`).
6. `mark_endpoint_bad` (Bad/NoCommunication) при connect fail / disconnect; не глобальный `mark_stale_before`.

**DoD:** `--once` без reactor-loop; `--watch` без `sleep_for`; Dispatcher Fake tests; isolation test двух endpoints; reconnect backoff test.

**Не делалось в A:** SignAndEncrypt, CSV import, OTLP default-on, async Modbus TCP.

### B — Качество CI и completeness poller (закрыт)

- TSan job (TagStore / Dispatcher / write queue / reactor; UA smoke skipped).
- ASan preset в CI.
- JSON Schema engine в `opc-map validate`.
- FC15 `write_multiple_coils` на порте и coalesce consecutive coils в Dispatcher.
- Метрики `ua_sessions` / `tag_quality`.

### C — Инструменты карт (закрыт)

**Состав (сделано):**

1. `opc-map import-csv` — CSV колонок из [03](03-modbus-projects.md) → loadable draft (`127.0.0.1:502`, один device, один poll group).
2. `opc-map gen-nodeset` — фрагмент UA NodeSet2 (Objects i=85, FolderType i=61, BaseDataVariableType i=63) из `nodePath`.
3. Профили устройств: при load, до validate, пустой `device.tags` копирует `deviceProfiles[].tags`; непустой — union, instance побеждает по `name`.
4. Runtime doctor: `opc::core::runtime_doctor` + `OPC_SERVER --runtime-doctor` (со `--once` — stderr, exit 1 при Error). Не смешивается со static `opc-map doctor`.

**Не делалось в C:** SignAndEncrypt, нагрузочные тесты, UDP — перенесены в D.

### D — Промышленный security и нагрузка (этап 7) — закрыт

**Состав (сделано):**

1. open62541 с `UA_ENABLE_ENCRYPTION=OPENSSL`; `opcua.securityMode` Sign/SignAndEncrypt не игнорируется (fail-closed без encryption-сборки).
2. Studio / `opc-monitor`: сертификатный профиль (`certificate`/`privateKey`, самоподпись если пути пустые).
3. Нагрузочный стенд Catch2: 2 endpoints × N tags Fake poll + UA subscription smoke.
4. `ModbusUdpTransport` за `IModbusTransport`, выбор в `default_transport_factory`.

**Оговорки:** demo-plant остаётся None; PKI AcceptAll без `--ua-strict-certs`; load stand — smoke, не профилировщик.

## Критерии готовности

### Лабораторный MVP (конец этапа 4) — выполнен

1. Проект `demo-plant.modbusproj.json` загружается без ошибок.
2. Значения с Modbus TCP видны в UA-клиенте (Read + Subscription).
3. Запись уставки из UA доходит до регистра и подтверждается quality Good.
4. Quality Bad/Uncertain выставляется на ошибках poll/write (полный stale-on-disconnect — инкремент A).

### Milestone: reactor MVP (инкремент A) — выполнен

1. Нет `sleep` в `Application::run` / runtime-пути.
2. Изоляция endpoint: I/O одного устройства не блокирует strand другого (≥2 worker threads).
3. UA Write не вызывает transport с потока open62541 (`post` на strand).
4. Обрыв связи → теги **этого** endpoint уходят в Bad/NoCommunication.

### Milestone: инкремент B — выполнен

TSan/ASan в CI, JSON Schema engine, FC15, метрики `ua_sessions` / `tag_quality`.

### Milestone: инкремент C — выполнен

`opc-map import-csv` / `gen-nodeset`, expand `deviceProfiles` при load, `OPC_SERVER --runtime-doctor`.

### Milestone: инкремент D — выполнен

SignAndEncrypt (fail-closed), сертификатный профиль Studio/opc-monitor, load stand, UDP transport.

## Связь с текущим репозиторием

| Сейчас | Следующая цель |
|--------|----------------|
| Asio `io_context` + strand-per-endpoint; sync TCP на strand | Asio-native async Modbus TCP (по желанию) |
| `reconnectDelayMs` backoff на strand | — |
| `opc-map` validate (JSON Schema + semantic) / doctor / migrate / import-csv / gen-nodeset | — |
| Studio + opc-monitor, cert profile Sign/SignAndEncrypt | username token / industrial CA |
| OTel metrics including `ua_sessions` / `tag_quality`, OTLP opt-in | traces poll/write; OTLP в CI по флагу |
| GCC CI + Conan + Studio matrix + ASan/TSan | — |

## Вне roadmap ядра

- Встроенный OPC DA/Classic server
- Полноценная SCADA
- Произвольные полевые протоколы без отдельного эпика Translator
- HTTP/WebSocket API внутри `OPC_SERVER` (отклонён ADR-0016)
