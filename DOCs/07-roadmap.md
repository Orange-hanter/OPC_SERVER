# 07. Roadmap реализации

Документация в `DOCs/` задаёт целевое состояние. Ниже — порядок внедрения кода. Исторический backlog опроса IoT/Modbus сохранён в [tasks.md](tasks.md) и покрывается этапами 1–2.

## Этапы

### Этап 0 — Каркас документации и формата проекта (текущий)

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
- [x] `Application` CLI (`--project`, `--once`, `--watch`)
- [x] `ILog`, `ManualClock`, StderrLog
- [x] Bootstrap tests with Fake transport
- [ ] Asio reactor loop (replace sleep) — следующий инкремент

### Этап 3 — OPC UA Read

- [x] open62541 (+ C++ обёртка `OpcUaServer`), построение дерева из `nodePath`
- [x] Read из TagStore, security None для стенда
- [x] Smoke-тест UA-клиентом (`tests/test_opc_ua_read.cpp`)
- [x] CLI `--no-opcua`, проводка в `ServerRuntime` / `Application`

### Этап 4 — Subscriptions и Write

- [x] MonitoredItems / Publish (TagStore → `writeDataValue` → UA notifications)
- [x] Write path → Dispatcher queue → FC06/16/05/15 (`OpcUaServer` ValueCallback)
- [x] Политика `writes_first`, маппинг StatusCode / WritePending
- [x] Smoke-тесты UA Write + Subscription (`tests/test_opc_ua_write_subs.cpp`)

### Этап 5 — Historian и Debug

- Hot ring buffer, cold SQLite
- Frame log Modbus, метрики spdlog/OTel
- Replay для отладки

### Этап 6 — Удобство разметки карт

- `opc-map doctor`, `import-csv`, `gen-nodeset`
- TUI или лёгкий UI редактора проектов (после стабилизации формата)
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
| `Src/app.cpp` читает JSON и крутит пустой цикл | App загружает project, стартует poller + UA |
| `DOCs/config.json` | Вытесняется `*.modbusproj.json` |
| `Lib/modbuspp` submodule | Замена на Asio-client / libmodbus |
| CMake без UA | C++26, FetchContent open62541, OPC UA Read |

## Вне roadmap ядра

- Встроенный OPC DA/Classic server
- Полноценная SCADA
- Произвольные полевые протоколы без отдельного эпика Translator
