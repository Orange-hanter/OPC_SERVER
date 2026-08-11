# 03. Проекты карт Modbus

## Цель

Сделать **разметку карты Modbus максимально удобной**: инженер работает с **проектом** (имена, группы, профили устройств, единицы), а не с «сырыми» адресами в разрозненном JSON. Runtime и OPC UA получают уже согласованную, проверенную по схеме конфигурацию.

## Что такое проект

Один **проект** — файл `*.modbusproj.json` (или каталог с главным `project.modbusproj.json` и подключаемыми фрагментами).

Проект объединяет:

1. Метаданные (имя площадки, версия схемы, описание).
2. Сетевые endpoints (host, port, таймауты, reconnect).
3. Устройства (slave id, профиль, комментарии).
4. Группы опроса (период, приоритет, список блоков регистров).
5. Теги (имя, тип, адрес, byte order, scale/offset, единица, writable, путь в OPC UA).
6. Параметры northbound OPC UA (endpoint URL, namespace, security mode по умолчанию).

Пример: [examples/demo-plant.modbusproj.json](examples/demo-plant.modbusproj.json).  
Схема: [schemas/modbus-project.schema.json](schemas/modbus-project.schema.json).

## Модель данных (логическая)

```text
Project
├── opcua { endpointUrl, applicationName, securityPolicy }
├── endpoints[] { id, host, port, transport, timeouts }
├── deviceProfiles[] { id, vendor, registerLayouts... }   # опционально
├── devices[] { id, endpointId, unitId, profileId?, tags[] | blocks[] }
└── pollGroups[] { id, periodMs, priority, deviceId, blocks[] }
```

### Тег

Минимально удобное описание точки:

| Поле | Назначение |
|------|------------|
| `name` | Логическое имя (`Tank1.Level`) |
| `nodePath` | Путь в дереве UA (`Plant/Tank1/Level`) |
| `area` | Область Modbus: `holding` / `input` / `coil` / `discrete` |
| `address` | Адрес регистра/coil (как в карте устройства; база 0 или 1 — см. `addressBase`) |
| `type` | `bool`, `uint16`, `int16`, `uint32`, `int32`, `float32`, `float64` |
| `quantity` | Число регистров/битов (выводится из `type`, если не задано) |
| `byteOrder` | Порядок байт/слов, напр. `ABCD`, `CDAB`, `BADC`, `DCBA` |
| `scale`, `offset` | `eng = raw * scale + offset` |
| `unit` | Единица (`m`, `°C`, `bar`) |
| `writable` | Разрешена ли запись из UA |
| `group` | Привязка к `pollGroups[].id` |
| `description` | Комментарий для инженера |

### Блок регистров

Для эффективности опроса теги можно группировать в непрерывные **блоки** (`start`, `count`, `code`), из которых теги «нарезаются» по смещениям — как в прототипе [`config.json`](config.json), но с именами и сетью.

## Удобства инженерии

1. **Именованные теги и иерархия** — SCADA видит `Plant/...`, а не `HR[40001]`.
2. **Группы опроса** — быстрые и медленные точки без смешивания периодов.
3. **Профили устройств** — шаблон «счётчик X / ПЧ Y» с типовой картой; instance задаёт только endpoint и unit id.
4. **Единицы и scale/offset** — инженерные величины сразу в TagStore/UA.
5. **Комментарии и `description`** — документация карты рядом с адресами.
6. **Импорт** (спецификация tooling): CSV/Excel колонок `name,area,address,type,byteOrder,scale,offset,unit,writable` → фрагмент проекта.
7. **Валидация по JSON Schema** — ошибки до запуска на объекте.
8. **`opc-map doctor`** — пересечения регистров, дыры в блоках, теги без группы, writable без FC16/FC06 и т.п.

## CLI `opc-map` (спецификация инструмента)

Код инструмента появится по roadmap; контракт команд:

| Команда | Поведение |
|---------|-----------|
| `opc-map validate <project>` | Проверка JSON Schema + семантических правил |
| `opc-map doctor <project>` | Диагностика пересечений, производительности блоков, предупреждения |
| `opc-map gen-nodeset <project> -o out.xml` | Генерация фрагмента узлов / dump дерева UA |
| `opc-map import-csv <csv> -o fragment.json` | Импорт таблицы регистров |
| `opc-map migrate-legacy config.json -o out.modbusproj.json` | Миграция со старого [`config.json`](config.json) |

Коды выхода: `0` — ok, `1` — ошибки валидации, `2` — ошибка ввода/файла.

## Миграция с `DOCs/config.json`

Старый формат:

- ключ `Nodes` с клиентами;
- `id`, `code`, `shift`, `registers`, `API` со смещениями и типами;
- нет host/port, имён групп опроса, UA-путей.

Правила миграции:

| Было | Стало |
|------|-------|
| `Nodes.<key>` | `devices[].id` = key |
| `id` | `devices[].unitId` |
| `code` / `shift` / `registers` | `pollGroups[].blocks[]` или автосборка из тегов |
| `API.<offset>` type/name/byteorder | `devices[].tags[]` |
| — | Добавить `endpoints[]` вручную (host/port) |
| — | Задать `nodePath` / `opcua` |

`opc-map migrate-legacy` создаёт черновик проекта и помечает `# TODO` поля сети.

## Рекомендации по удобной разметке

1. Сначала завести **endpoint** и **device**, затем импортировать CSV карты от вендора.
2. Имена тегов — стабильные логические; адреса Modbus могут меняться при замене ПЛК.
3. Держать непрерывные регистры в одном блоке (меньше транзакций).
4. Не смешивать в одной fast-группе десятки устройств на одном TCP — лучше несколько соединений/групп.
5. Явно помечать `writable: true` только для команд и уставок.
6. Фиксировать `addressBase` (`0` или `1`) на уровне проекта, чтобы исключить off-by-one.

## Связанные документы

- Архитектура: [02-architecture.md](02-architecture.md)
- Модель UA: [04-opcua-information-model.md](04-opcua-information-model.md)
- Операции опроса: [06-dispatch-debug-store.md](06-dispatch-debug-store.md)
