# 05. Технологический стек (C++26)

## Принципы выбора

- Актуальный стандарт языка (**C++26**) и современный toolchain.
- Предсказуемый async I/O для сотен соединений Modbus и UA-сессий.
- Проверенный OPC UA стек с тонкой C++-обёрткой.
- Удобная инженерия карт: JSON + Schema, без тяжёлой кодогенерации на старте.
- Наблюдаемость «из коробки»: логи, метрики, трассы.

## Язык и сборка

| Компонент | Выбор |
|-----------|--------|
| Язык | **C++26** |
| Сборка | **CMake ≥ 3.28**, генератор **Ninja** |
| Компиляторы | **GCC ≥ 16** / Clang с `-std=c++26` (Linux); MSVC — по мере поддержки фич C++26 |
| Качество | clang-tidy, `-Werror` в CI, clang-format |
| Санитайзеры | ASan/UBSan (Linux debug), опционально TSan для TagStore |
| Пакеты | **Conan 2** (`CMakeDeps` + `CMakeToolchain`) или CMake `FetchContent`; git submodules — только по необходимости |

Целевой стандарт в `CMakeLists.txt`: `target_compile_features(... cxx_std_26)` и/или `-std=c++26`.  
Базовый профиль разработки: GCC 16+ (полный C++26, включая reflection где доступно). Фичи вроде contracts/reflection используем там, где компилятор уже стабилен; остальное — через portable C++26 subset до выравнивания MSVC.

Сборочные профили, переключатели качества и оба способа получения зависимостей
разобраны в [практикуме по современному CMake и Conan 2](12-modern-cmake.md).

## Сетевой I/O

| Компонент | Выбор |
|-----------|--------|
| Async I/O | standalone **Asio** 1.32 (`FetchContent`, `opc::asio`; hexagon: только `adapters/`) |
| Модель | один или несколько `io_context`, strand на endpoint |

Почему Asio: зрелый async TCP, таймеры для периодов опроса, переносимость Linux/Windows.

## OPC UA

| Компонент | Выбор |
|-----------|--------|
| Ядро UA | **open62541** 1.4.11 FetchContent (`UA_ENABLE_ENCRYPTION=OPENSSL`) или Conan 1.5 (`encryption=openssl`) |
| C++ слой | Тонкая обёртка на C++26 **или** [open62541pp](https://github.com/open62541pp/open62541pp) как база |

Сервер использует Information Model из проекта; Subscriptions — через API open62541.

## Modbus

| Компонент | Выбор |
|-----------|--------|
| Клиент | Sync POSIX **Modbus TCP** и **Modbus UDP** за `IModbusTransport`, вызов **только** со strand endpoint. Asio-native async TCP — вне ядра |
| Legacy | submodule `Lib/modbuspp` — **не ядро**; кандидат на удаление/замену |

UDP — `ModbusUdpTransport`: тот же MBAP ADU, один запрос/ответ на датаграмму (`endpoints[].transport = "udp"`).

## Конфигурация и проекты карт

| Компонент | Выбор |
|-----------|--------|
| JSON | **nlohmann/json** (уже в `Lib/Json`) |
| Валидация | JSON Schema draft 2020-12 (`nlohmann_json_schema_validator` + bundled schema; `$defs` mapped to Draft 7) + semantic checks |
| Формат | `*.modbusproj.json` ([схема](schemas/modbus-project.schema.json)) |
| YAML (опционально) | фронтенд для людей → компиляция в JSON при `opc-map validate` |

## Логирование и телеметрия

| Компонент | Выбор |
|-----------|--------|
| Логи | **spdlog** (async logger, уровни, sink в файл/stdout) |
| Метрики/трассы | **OpenTelemetry** C++ SDK: metrics including `ua_sessions` / `tag_quality`; traces poll/write — хвост этапа 5 |

## Тестирование

| Компонент | Выбор |
|-----------|--------|
| Unit / integration | **Catch2** (или GoogleTest — один на репозиторий; по умолчанию Catch2) |
| Property-тесты | byte order / pack-unpack float32↔регистры |
| Фейки | in-memory Modbus slave для CI; UA client smoke (open62541 client) |

## Инструмент инженерии `opc-map`

Отдельный executable в том же репозитории (C++26):

- shared library парсинга проекта с сервером;
- команды `validate`, `doctor`, `gen-nodeset`, `import-csv`, `migrate-legacy`;
- позже: TUI (например FTXUI) или лёгкий Web UI для разметки карт — **не** блокер v1.

## Engineering Studio

| Компонент | Выбор |
|-----------|-------|
| Desktop shell | **Tauri 2** (Windows, Linux, macOS) |
| UI | React + TypeScript + Vite + Tailwind CSS |
| Локальная валидация | JSON Schema/Ajv + bundled `opc-map validate` |
| Remote monitoring | read-only **OPC UA client** на open62541 |
| Native IPC | JSON Lines через stdin/stdout процесса `opc-monitor` |

Studio редактирует локальные `*.modbusproj.json` и не изменяет конфигурацию
runtime напрямую (ADR-0005). Мониторинг использует Browse/Subscriptions и
диагностические OPC UA узлы; отдельный HTTP/WebSocket API не вводится
(ADR-0016). Browser-сборка служит только preview с mock-адаптером, поскольку
браузер не поддерживает `opc.tcp`.

## Структура каталогов (целевая)

```text
Src/
  main.cpp
  app/
  project/          # загрузка и валидация *.modbusproj.json
  modbus/           # poller, codec, transport
  tagstore/
  translator/
  dispatcher/
  opcua/
  historian/
  debug/
tools/
  opc-map/
tests/
DOCs/
```

Текущие `Src/app.*` — точка эволюции к `app/` + модулям выше.

## Что сознательно не берём в ядро v1

- COM / DCOM / OPC DA SDK.
- Qt как обязательная зависимость runtime-сервера (допустим только для будущего GUI редактора).
- Python как runtime сервера (допустим для вспомогательных скриптов импорта).

## Связанные документы

- Архитектура: [02-architecture.md](02-architecture.md)
- Roadmap внедрения стека: [07-roadmap.md](07-roadmap.md)
