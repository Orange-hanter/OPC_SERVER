# 07. Roadmap реализации

Документация в `DOCs/` задаёт целевое состояние. Ниже — порядок внедрения кода. Исторический backlog опроса IoT/Modbus сохранён в [tasks.md](tasks.md) и покрывается этапами 1–2.

## Этапы

### Этап 0 — Каркас документации и формата проекта (текущий)

- [x] Обзор, архитектура, стек, модель UA, ops-доки
- [x] JSON Schema и пример `*.modbusproj.json`
- [x] Обновление корневого README со ссылкой на `DOCs/`

### Этап 1 — Формат проекта и `opc-map` (минимум)

- Парсер `*.modbusproj.json` + валидация по Schema
- `opc-map validate` / `opc-map migrate-legacy` (из [config.json](config.json))
- Unit-тесты на примеры из `DOCs/examples/`

### Этап 2 — ModbusPoller + TagStore

- TCP transport (Asio), FC03/04/01/02
- Цикл групп опроса, таймауты, reconnect
- Translator (типы, byte order, scale/offset)
- TagStore с quality и timestamps
- Вывод watchlist в консоль (закрывает пункты из [tasks.md](tasks.md))

### Этап 3 — OPC UA Read

- open62541 (+ C++ обёртка), построение дерева из `nodePath`
- Read из TagStore, security None для стенда
- Smoke-тест UA-клиентом

### Этап 4 — Subscriptions и Write

- MonitoredItems / Publish
- Write path → Dispatcher queue → FC06/16/05/15
- Политика `writes_first`, маппинг StatusCode

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
| CMake без C++26/UA | C++26, FetchContent open62541, тесты |

## Вне roadmap ядра

- Встроенный OPC DA/Classic server
- Полноценная SCADA
- Произвольные полевые протоколы без отдельного эпика Translator
