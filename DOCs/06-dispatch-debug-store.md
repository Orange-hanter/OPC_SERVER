# 06. Диспетчеризация, отладка, трансляция и накопление

## Диспетчеризация

Dispatcher управляет **когда** и **в каком порядке** идут Modbus-транзакции на каждом endpoint.

### Группы опроса

| Priority | Типичный periodMs | Назначение |
|----------|-------------------|------------|
| `fast` | 50–200 | Контуры, аварии, обороты |
| `normal` | 200–1000 | Технологические аналоги |
| `slow` | 1000–5000 | Статусы, редкие счётчики |

Правила:

- Каждая группа привязана к одному `deviceId` (одно unit id на соединении в транзакции).
- Блоки регистров внутри группы по возможности непрерывны.
- Просроченный цикл не «догоняется» штормом: пропускаем слот, считаем метрику `poll.overrun`.

### Очередь записи (write-down)

```text
UA Write → validate → write_queue(endpoint) → worker
                         ↑
            cyclic reads уступают, если policy = writes_first
```

Политики (конфиг runtime / будущие поля проекта):

| Политика | Поведение |
|----------|-----------|
| `writes_first` (по умолчанию) | Перед следующим read обслуживается очередь write |
| `fair` | N reads / 1 write |
| `reads_only_window` | Запись только в выделенном окне |

Конфликты: два write в один регистр — побеждает последний с фиксацией в логе; опционально reject второго с `BadInvalidState`.

### Изоляция устройств

- Отдельный strand/очередь на `endpoint`.
- Таймаут одного slave не блокирует другие endpoints.
- После N ошибок подряд — backoff reconnect, теги устройства → `BadNoCommunication`.

## Трансляция

Translator — чистое преобразование без I/O.

### Протокольный уровень

- Pack/unpack по `type` + `byteOrder`.
- Coils ↔ `bool`; multi-register ↔ `float32` / `int32` / …
- Выбор FC при записи: одиночный регистр → FC06, несколько → FC16; coil → FC05/15.

### Семантический уровень

- `eng = raw * scale + offset` (и обратное при write).
- Единицы (`unit`) как метаданные UA Property (опционально).
- Алиасы: стабильный `name` / `nodePath` для SCADA при смене физических адресов.

Таблицы трансляции живут в проекте (теги), не в коде.

## Отладка

### Каналы диагностики

| Канал | Содержание |
|--------|------------|
| Modbus frame log | TX/RX PDU+MBAP, RTT, exception code, endpoint id |
| Tag watch | Подписка на список `TagId` с периодом вывода в консоль/сокет |
| UA session dump | Endpoint URL клиента, security mode, число subscriptions |
| Metrics | `poll.rtt_ms`, `poll.errors`, `write.queue_depth`, `ua.sessions`, `historian.dropped` |

### Режим «PCAP-подобный» лог

Бинарный или текстовый журнал кадров с timestamp для offline-разбора. Replay журнала через фейковый transport — для регрессии Translator/TagStore без поля.

### Live doctor

`opc-map doctor` — статика проекта; runtime doctor (будущее): частота exception, «дырявые» блоки, теги никогда не Good.

## Накопление (Historian)

### Горячий слой (hot)

- Кольцевой буфер в RAM на тег или на группу.
- Пишет каждое успешное обновление TagStore или по deadband.
- Размер: конфиг `historian.hot.capacity` (число сэмплов).

### Холодный слой (cold)

| Профиль | Хранилище |
|---------|-----------|
| Базовый (v1) | SQLite или сегментированные файлы |
| Расширенный | TimescaleDB / внешняя БД |

Поля сэмпла: `tagId`, `timestamp`, `value`, `status`.

### Retention и replay

- Политика: хранить N часов/дней или M гигабайт.
- Replay: проиграть hot/cold в TagStore/Debug для анализа инцидента (без записи в поле).

### Связь с SCADA

Historian **не заменяет** исторический сервер SCADA; это локальный буфер шлюза для отладки, трендов короткого горизонта и восстановления после рестартов клиентов.

## Наблюдаемость end-to-end

```text
Poller --(metrics)--> OTel metrics
TagStore --(events)--> Historian + Debug watch
OpcUaServer --(metrics)--> sessions / publish rate
spdlog <-- structured fields: endpoint, device, tag, rtt
```

Корреляционный id транзакции Modbus полезен в логах write-path.

## Связанные документы

- Архитектура модулей: [02-architecture.md](02-architecture.md)
- Качество в UA: [04-opcua-information-model.md](04-opcua-information-model.md)
- Этапы внедрения: [07-roadmap.md](07-roadmap.md)
