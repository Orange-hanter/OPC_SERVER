# 07. Roadmap реализации

Документация в `DOCs/` задаёт целевое состояние. Ниже — порядок внедрения кода. Исторический backlog опроса IoT/Modbus сохранён в [tasks.md](tasks.md) и покрывается этапами 1–2.

## Этапы

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
- [ ] Полный reactor (Asio io_context + strand per endpoint + timers)
- [x] Watchlist в консоли / app composition root

### Этап 2.5 — Runtime infrastructure

- [x] `ServerRuntime` composition root + `TransportFactory`
- [x] `Application` CLI (`--project`, `--once`, `--watch`, `--version`)
- [x] `ILog`, `ManualClock`, StderrLog
- [x] Bootstrap tests with Fake transport
- [ ] Asio reactor loop (replace sleep) — следующий инкремент

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
- [ ] Asio reactor / убрать sleep (перенос из 2.5)
- [ ] TSan CI job

### Этап 5 — Historian и Debug

- [x] Hot ring buffer (`RingHistorian`), cold SQLite (`SqliteHistorian`) — ADR-0014
- [x] Frame log Modbus (`IFrameLog` / `FileFrameLog`)
- [x] spdlog (`SpdlogLog`) + OpenTelemetry metrics (`OtelMetrics`, ADR-0015)
- [x] Replay для отладки (`historian_replay.hpp`, `ReplayModbusTransport` / frame log)
- [ ] OTLP default-on in CI / traces for poll-write

### Этап 6 — Удобство разметки карт

- [x] `opc-map doctor` (static overlaps, holes, unpolled tags, sparse blocks)
- [ ] `import-csv`, `gen-nodeset`
- [x] Tauri Engineering Studio: локальный редактор проектов + удалённый
  read-only OPC UA мониторинг (Windows/Linux/macOS)
- Профили устройств и библиотека шаблонов

### Этап 7 — Промышленное укрепление

- Sign / SignAndEncrypt, сертификаты
- Нагрузочные тесты (число тегов, RTT)
- UDP Modbus при необходимости
- (Опционально) исследование внешнего моста UA↔Classic/DA — **не** ядро

## Критерии готовности MVP (конец этапа 4)

1. Проект `demo-plant.modbusproj.json` загружается без ошибок.
2. Значения с Modbus TCP видны в UA-клиенте (Read + Subscription).
3. Запись уставки из UA доходит до регистра и подтверждается quality Good.
4. При обрыве связи теги переходят в Bad/Uncertain предсказуемо.

## Связь с текущим репозиторием

| Сейчас | Цель |
|--------|------|
| `ServerRuntime` + OPC UA + Historian/frame log | Asio reactor, SignAndEncrypt, OTel |
| `*.modbusproj.json` + `opc-map` | doctor / import-csv / редактор |
| Sync `ModbusTcpTransport` | Asio strand-per-endpoint |
| open62541 FetchContent | Опционально pin hash / system package |

## Вне roadmap ядра

- Встроенный OPC DA/Classic server
- Полноценная SCADA
- Произвольные полевые протоколы без отдельного эпика Translator
