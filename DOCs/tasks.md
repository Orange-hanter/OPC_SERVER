# Список задач

Актуальный backlog ядра. Порядок работ и критерии готовности: [07-roadmap.md](07-roadmap.md).  
Исторический набросок 2019 (опрос IoT/Modbus) закрыт этапами 1–2; текст сохранён в конце файла.

**Снимок:** 2026-08-14, линия веток A→B→C→D (`cursor/increment-d-860d`). В `master` пока Studio (PR #8); A/B/C/D — stacked PR #13/#14/#15/#16.

## Процент выполнения

| Контур | Оценка | Как считали |
|--------|--------|-------------|
| Лабораторный MVP (этапы 0–4) | **100%** | Все чеклисты закрыты; Read/Write/Subscriptions e2e |
| Пост-MVP (4.5–6 + инкременты A–C) | **~95%** | Закрыто; открыт хвост этапа 5: OTLP default-on / traces |
| Промышленное укрепление (этап 7 / инкремент D) | **100%** | SignAndEncrypt, нагрузка-smoke, UDP — в этой ветке |
| **Roadmap ядра, этапы 0–7** | **~98%** | 55 из 56 пунктов чеклиста (без Classic/DA и прочего «вне ядра») |
| Инкременты после Studio (A–D) | **100%** | A, B, C, D закрыты |

«~98%» — закрытые пункты спецификации. Это **ещё не** полный промышленный контур: demo-plant остаётся на `None`, PKI по умолчанию AcceptAll (пока нет `--ua-strict-certs`), UDP — MBAP в датаграмме, не RTU, нагрузочный стенд — Catch2 smoke, не профилировщик RTT/CPU.

Оговорки к «закрыто» в D:

- encryption-сборка open62541 + fail-closed, если проект просит Sign/Encrypt без `UA_ENABLE_ENCRYPTION`;
- Studio/`opc-monitor` сертификатный профиль; username/password по-прежнему отклоняются;
- load stand = 2 endpoints × 24 тега Fake poll + UA subscription smoke на `ServerStatus.State`;
- UDP = `ModbusUdpTransport` за `IModbusTransport`, выбор по `endpoints[].transport`.

## Сейчас (закрыто)

- [x] Этап 0 — документация, schema, README
- [x] Этап 1 — `*.modbusproj.json`, `opc-map validate` / `migrate-legacy`, JSON Schema engine
- [x] Этап 1.5 — hexagon, ADR-0001…0010, TagStore, Fake transport
- [x] Этап 2 — Translator, Dispatcher, RuntimeIndex, Asio reactor, FC15
- [x] Этап 2.5 — ServerRuntime, CLI, reconnect backoff, `mark_endpoint_bad`
- [x] Этап 3 — OPC UA Read (open62541, security None)
- [x] Этап 4 — Write + Subscriptions
- [x] Этап 4.5 — hardening, CMake/Conan, ASan/TSan CI
- [x] Этап 5 (кроме traces) — historian, frame log, spdlog, OTel metrics, replay, `ua_sessions` / `tag_quality`
- [x] Этап 6 — `opc-map doctor` / `import-csv` / `gen-nodeset`, профили при load, Studio, opc-monitor, `--runtime-doctor`
- [x] Инкремент A — Asio reactor
- [x] Инкремент B — schema engine, FC15, sanitizer CI, UA metrics
- [x] Инкремент C — CSV, NodeSet2, deviceProfiles expand, runtime doctor
- [x] Инкремент D — Sign/SignAndEncrypt, cert profile Studio/opc-monitor, load stand, UDP Modbus

## Дальше (открыто)

### Хвост этапа 5 (не блокер D)

- [ ] OTLP default-on в CI / traces на poll-write

### Не в ядре (не входят в процент)

- Asio-native async Modbus TCP (транспорт на strand остаётся sync POSIX)
- Встроенный OPC DA/Classic
- Полноценная SCADA
- Другие полевые протоколы без отдельного эпика Translator
- HTTP/WebSocket API в `OPC_SERVER` (отклонён ADR-0016)
- Каталог вендорских device profiles сверх demo `generic-tank-sensor`
- Промышленный PKI (CA, reject list, username token) сверх lab self-signed / AcceptAll

## Исторический backlog (2019, закрыт)

Опрос IoT по Modbus TCP (карта → PDU → сеть → буфер → повтор) и внутрипрограммный доступ/консоль — покрыто этапами 1–2 (проект карт, Dispatcher, TagStore, `--watch`). UDP из того же списка закрыт инкрементом D.
