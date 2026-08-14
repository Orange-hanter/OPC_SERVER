# 04. Информационная модель OPC UA

## Роль northbound

OPC UA Server — единственный промышленный интерфейс к SCADA на текущем этапе. Модель узлов строится из проекта `*.modbusproj.json` (поле `nodePath` и секция `opcua`).

## Иерархия адресного пространства

Рекомендуемый корень пользовательских объектов:

```text
Root
└── Objects
    └── Plant                          (Folder, из name проекта / логической площадки)
        ├── Tank1                      (Object)
        │   ├── Level                  (Variable, Float)
        │   ├── Temperature            (Variable, Float)
        │   ├── Status                 (Variable, UInt16)
        │   └── Setpoint               (Variable, UInt16, Writable)
        └── Pump1
            └── Vfd
                ├── Frequency
                ├── SpeedSetpoint
                └── Running
```

Правила:

- `nodePath` в теге — путь относительно `Objects` (например `Plant/Tank1/Level`).
- Промежуточные сегменты создаются как `FolderType` / `BaseObjectType`.
- Листья — `BaseDataVariableType` с типом данных UA, согласованным с `tag.type`.
- Пользовательский namespace задаётся `opcua.namespaceUri`.

## Соответствие типов Modbus → OPC UA

| `tag.type` | UA Built-in | Примечание |
|------------|-------------|------------|
| `bool` | `Boolean` | coils / discrete |
| `uint16` | `UInt16` | |
| `int16` | `Int16` | |
| `uint32` | `UInt32` | 2 регистра |
| `int32` | `Int32` | 2 регистра |
| `float32` | `Float` | 2 регистра, byteOrder обязателен |
| `float64` | `Double` | 4 регистра |

Инженерное значение после `scale`/`offset` публикуется в UA (не «сырой» регистр), если в проекте задан scale≠1 или offset≠0.

## DataValue: значение, качество, время

Каждый тег в UA отдаётся как `DataValue`:

| Поле | Источник |
|------|----------|
| `Value` | `TagStore` после Translator |
| `StatusCode` | отображение `Quality` опроса |
| `SourceTimestamp` | время получения ответа устройства (если известно) |
| `ServerTimestamp` | время фиксации в `TagStore` |

### Маппинг качества опроса → StatusCode

| Состояние southbound | Quality (логика) | Рекомендуемый UA StatusCode |
|----------------------|------------------|-----------------------------|
| Успешный опрос | Good | `Good` |
| Данные старше порога stale | Uncertain | `UncertainLastUsableValue` |
| Таймаут / нет связи | Bad | `BadNoCommunication` |
| Modbus exception | Bad | `BadDeviceFailure` / `BadOutOfRange` |
| Ошибка декодирования / byte order | Bad | `BadDecodingError` |
| Запись принята, подтверждение ждётся | Good (или Uncertain) | `Good` + отдельный state, либо `Uncertain` до confirm |
| Запись отклонена устройством | Bad | `BadWriteNotSupported` / `BadInvalidArgument` |

Порог `stale` задаётся относительно `pollGroups.periodMs` (например 2–3 периода).

## Read / Write

### Read

- Синхронный Read UA читает актуальный снимок из `TagStore` (не блокирует полевой опрос).
- Не инициирует внеплановый Modbus-запрос в v1 (опционально — `read-through` в будущем профиле).

### Write

1. Проверка `writable` в проекте и прав UA-сессии.
2. Постановка в очередь `Dispatcher` (write-down).
3. Кодирование Translator → PDU (FC05/15 для coils, FC06/16 для registers).
4. По результату — обновление `TagStore` и StatusCode.

Конфликт write и циклического read на одном endpoint разрешает Dispatcher ([06](06-dispatch-debug-store.md)).

## Subscriptions и MonitoredItems

| Параметр | Рекомендация |
|----------|--------------|
| Sampling interval | ≥ периода соответствующей poll-группы |
| Publishing interval | кратен sampling; типично 100–1000 ms для HMI |
| Deadband | абсолютный/процентный для аналоговых тегов (настраиваемо позже) |
| Queue size | ≥ 1; для бурстов диагностики — больше |

Сервер уведомляет клиентов при изменении Value или StatusCode. Если poller не обновил тег, лишних уведомлений нет.

## Безопасность

| Режим | Когда использовать |
|-------|--------------------|
| `None` / `None` | Только лаборатория и закрытый стенд |
| `Sign` | Целостность в доверенной сети |
| `SignAndEncrypt` + `Basic256Sha256` | Промышленный контур по умолчанию |

Управление сертификатами (создание, trust list, reject list) — часть runtime, не core.

Лабораторный `demo-plant` остаётся на `None`. Для `Sign` / `SignAndEncrypt` runtime собирает open62541 с `UA_ENABLE_ENCRYPTION=OPENSSL` и **не** откатывается на None: без encryption-сборки `start()` возвращает ошибку. Сертификат/ключ:

- `--ua-cert` / `--ua-key` — готовые DER/PEM;
- иначе (по умолчанию) самоподписанный application cert на `opcua.namespaceUri`;
- `--ua-trust PATH` (повторяемый) — trust list клиентов;
- `--ua-crl PATH` (повторяемый) — revocation / CRL files;
- Sign/Encrypt по умолчанию **без** AcceptAll (нужен trust list или `--ua-accept-untrusted` для стенда);
- `--ua-strict-certs` — явно запретить AcceptAll (побеждает `--ua-accept-untrusted`).

Studio / `opc-monitor` принимают `securityMode` `Sign`/`SignAndEncrypt`, пути `certificate`/`privateKey`
и опциональные `username`/`password` (UsernameIdentityToken). Пустые пути сертификата → самоподписанный
клиентский сертификат.

Серверная идентичность (проект / CLI):

- `opcua.users[]` `{username,password}` или `--ua-user user:pass` (повторяемый);
- при наличии users Anonymous по умолчанию **выключен** (`allowAnonymous: false`), иначе
  `--ua-deny-anonymous` / `--ua-allow-anonymous`;
- username/password при `securityMode: None` — только с `opcua.allowNonePassword` /
  `--ua-allow-none-password` (иначе `start()` отказывается: plaintext credentials);
- для промышленного контура предпочитайте Sign/SignAndEncrypt + users.

## Граница с OPC Classic / DA

Адресное пространство и сервисы описываются **только в терминах OPC UA**. Классические ItemID/CLSID/DA browse **не** моделируются в ядре. Если понадобится DA-клиент SCADA:

- внешний UA↔DA шлюз, или
- будущий опциональный адаптер вне процесса UA-ядра.

## Связанные документы

- Проекты карт: [03-modbus-projects.md](03-modbus-projects.md)
- Стек реализации: [05-tech-stack.md](05-tech-stack.md)
