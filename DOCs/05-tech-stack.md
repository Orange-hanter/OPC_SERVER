# 05. Технологический стек (C++23)

## Принципы выбора

- Актуальный стандарт языка (**C++23**) и современный toolchain.
- Предсказуемый async I/O для сотен соединений Modbus и UA-сессий.
- Проверенный OPC UA стек с тонкой C++-обёрткой.
- Удобная инженерия карт: JSON + Schema, без тяжёлой кодогенерации на старте.
- Наблюдаемость «из коробки»: логи, метрики, трассы.

## Язык и сборка

| Компонент | Выбор |
|-----------|--------|
| Язык | **C++23** |
| Сборка | **CMake ≥ 3.28**, генератор **Ninja** |
| Компиляторы | MSVC (Windows), Clang/GCC (Linux) — в CI оба профиля |
| Качество | clang-tidy, `-Werror` в CI, clang-format |
| Санитайзеры | ASan/UBSan (Linux debug), опционально TSan для TagStore |
| Пакеты | CMake `FetchContent` / CPM для зависимостей; git submodules — только по необходимости |

Целевой стандарт в `CMakeLists.txt`: `target_compile_features(... cxx_std_23)`.

## Сетевой I/O

| Компонент | Выбор |
|-----------|--------|
| Async I/O | **Boost.Asio** или standalone **Asio** |
| Модель | один или несколько `io_context`, strand на endpoint |

Почему Asio: зрелый async TCP, таймеры для периодов опроса, переносимость Linux/Windows.

## OPC UA

| Компонент | Выбор |
|-----------|--------|
| Ядро UA | **open62541** (C, высокая производительность, широкое покрытие сервисов) |
| C++ слой | Тонкая обёртка на C++23 **или** [open62541pp](https://github.com/open62541pp/open62541pp) как база |

Сервер использует Information Model из проекта; Subscriptions — через API open62541.

## Modbus

| Компонент | Выбор |
|-----------|--------|
| Клиент | **Asio-native Modbus TCP client** (предпочтительно) **или** libmodbus за адаптером |
| Legacy | submodule `Lib/modbuspp` — **не ядро**; кандидат на удаление/замену |

UDP — второй этап того же абстрактного transport-интерфейса.

## Конфигурация и проекты карт

| Компонент | Выбор |
|-----------|--------|
| JSON | **nlohmann/json** (уже в `Lib/Json`) |
| Валидация | JSON Schema (draft 2020-12) — библиотека вроде `nlohmann_json_schema_validator` или аналог |
| Формат | `*.modbusproj.json` ([схема](schemas/modbus-project.schema.json)) |
| YAML (опционально) | фронтенд для людей → компиляция в JSON при `opc-map validate` |

## Логирование и телеметрия

| Компонент | Выбор |
|-----------|--------|
| Логи | **spdlog** (async logger, уровни, sink в файл/stdout) |
| Метрики/трассы | **OpenTelemetry** C++ SDK (poll RTT, UA sessions, write queue depth) |

## Тестирование

| Компонент | Выбор |
|-----------|--------|
| Unit / integration | **Catch2** (или GoogleTest — один на репозиторий; по умолчанию Catch2) |
| Property-тесты | byte order / pack-unpack float32↔регистры |
| Фейки | in-memory Modbus slave для CI; UA client smoke (open62541 client) |

## Инструмент инженерии `opc-map`

Отдельный executable в том же репозитории (C++23):

- shared library парсинга проекта с сервером;
- команды `validate`, `doctor`, `gen-nodeset`, `import-csv`, `migrate-legacy`;
- позже: TUI (например FTXUI) или лёгкий Web UI для разметки карт — **не** блокер v1.

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
