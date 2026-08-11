# OPC_SERVER

Промышленный шлюз **Modbus → OPC UA → SCADA**: диспетчеризация опроса, отладка, трансляция данных и накопление истории для диспетчерских задач.

> Документация целевой архитектуры: **[DOCs/README.md](DOCs/README.md)**

## Возможности (целевые)

- Опрос устройств по **Modbus TCP** (UDP — позже) по удобным **проектам карт** `*.modbusproj.json`
- Публикация тегов в **OPC UA** (Read / Write / Subscriptions) для SCADA
- Диспетчеризация групп опроса, write-down, качество и временные метки
- Отладка кадров и сессий, локальный historian

OPC Classic / DA не входят в ядро; граница описана в [DOCs/01-overview.md](DOCs/01-overview.md).

## Стек (целевой)

- **C++26**, CMake, Ninja
- Asio, open62541, nlohmann/json, spdlog, OpenTelemetry  
  Подробности: [DOCs/05-tech-stack.md](DOCs/05-tech-stack.md)

## Документация

| Раздел | Ссылка |
|--------|--------|
| Оглавление | [DOCs/README.md](DOCs/README.md) |
| Обзор | [DOCs/01-overview.md](DOCs/01-overview.md) |
| Архитектура | [DOCs/02-architecture.md](DOCs/02-architecture.md) |
| Проекты карт Modbus | [DOCs/03-modbus-projects.md](DOCs/03-modbus-projects.md) |
| Пример проекта | [DOCs/examples/demo-plant.modbusproj.json](DOCs/examples/demo-plant.modbusproj.json) |
| Roadmap | [DOCs/07-roadmap.md](DOCs/07-roadmap.md) |

## Сборка (текущий каркас)

Репозиторий пока содержит ранний каркас приложения. Актуальная инструкция по целевому стеку появится вместе с этапами roadmap; сейчас:

```bash
git clone --recursive https://github.com/Orange-hanter/OPC_SERVER
cd OPC_SERVER
cmake ./ -B build
cmake --build build
```

На Windows генератор по умолчанию может создать проект Visual Studio; на Linux предпочтителен Ninja:

```bash
cmake -S . -B build -G Ninja
cmake --build build
```

## Статус

Код: прототип загрузки JSON и пустой цикл (`Src/`).  
Спецификация: комплект документов в `DOCs/` — ориентир для реализации по [roadmap](DOCs/07-roadmap.md).
