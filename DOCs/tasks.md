# Список задач

Актуальный backlog ядра. Порядок работ и критерии готовности: [07-roadmap.md](07-roadmap.md).  
Исторический набросок 2019 (опрос IoT/Modbus) закрыт этапами 1–2; текст сохранён в конце файла.

**Снимок:** 2026-08-14, линия `cursor/async-modbus-tcp-860d` поверх industrial-pki / CI / traces / A→D. В `master` пока Studio (PR #8).

## Процент выполнения

| Контур | Оценка | Как считали |
|--------|--------|-------------|
| Лабораторный MVP (этапы 0–4) | **100%** | Все чеклисты закрыты; Read/Write/Subscriptions e2e |
| Пост-MVP (4.5–6 + инкременты A–C + хвост этапа 5) | **100%** | OTLP в CI + traces poll/write |
| Промышленное укрепление (этап 7 / инкремент D) | **100%** | SignAndEncrypt, нагрузка-smoke, UDP |
| **Roadmap ядра, этапы 0–7** | **100%** | 56 из 56 пунктов чеклиста (без Classic/DA и прочего «вне ядра») |
| Инкременты после Studio (A–D + traces) | **100%** | A–D и хвост этапа 5 закрыты |

«100%» — закрытые пункты спецификации ядра. Это **ещё не** полный промышленный контур: demo-plant остаётся на `None`, PKI по умолчанию AcceptAll (пока нет `--ua-strict-certs`), UDP — MBAP в датаграмме, не RTU, нагрузочный стенд — Catch2 smoke, OTLP в CI компилируется, но без живого коллектора.

## Сейчас (закрыто)

- [x] Этап 0 — документация, schema, README
- [x] Этап 1 — `*.modbusproj.json`, `opc-map validate` / `migrate-legacy`, JSON Schema engine
- [x] Этап 1.5 — hexagon, ADR-0001…0010, TagStore, Fake transport
- [x] Этап 2 — Translator, Dispatcher, RuntimeIndex, Asio reactor, FC15
- [x] Этап 2.5 — ServerRuntime, CLI, reconnect backoff, `mark_endpoint_bad`
- [x] Этап 3 — OPC UA Read (open62541, security None)
- [x] Этап 4 — Write + Subscriptions
- [x] Этап 4.5 — hardening, CMake/Conan, ASan/TSan CI
- [x] Этап 5 — historian, frame log, spdlog, OTel metrics, replay, `ua_sessions` / `tag_quality`, traces poll/write, OTLP в CI
- [x] Этап 6 — `opc-map doctor` / `import-csv` / `gen-nodeset`, профили при load, Studio, opc-monitor, `--runtime-doctor`
- [x] Инкремент A — Asio reactor
- [x] Инкремент B — schema engine, FC15, sanitizer CI, UA metrics
- [x] Инкремент C — CSV, NodeSet2, deviceProfiles expand, runtime doctor
- [x] Инкремент D — Sign/SignAndEncrypt, cert profile Studio/opc-monitor, load stand, UDP Modbus
- [x] Хвост этапа 5 — OTLP default-on в CI / traces на poll-write

## Дальше (открыто)

### Приоритет (после ядра)

1. ~~CI hardening: TSan races, Conan open62541 plugins, Studio Win/macOS OpenSSL/C++23~~ — `cursor/ci-hardening-860d`
2. ~~Промышленный PKI (fail-closed Sign/Encrypt, `--ua-crl`, `--ua-accept-untrusted`)~~ — `cursor/industrial-pki-860d`
3. ~~Asio-native async Modbus TCP (ADR-0007)~~ — `cursor/async-modbus-tcp-860d`
4. Username token / industrial CA identity (следующий шаг PKI)
5. Живой OTLP-коллектор в CI (сейчас только compile-in exporters)
6. Полный async `IModbusTransport` API (completion tokens) — опционально; сейчас sync facade поверх Asio async I/O

### Не в ядре (не входят в процент)

- Полный async port API (completion tokens) для Dispatcher без блокировки strand на I/O
- Встроенный OPC DA/Classic
- Полноценная SCADA
- Другие полевые протоколы без отдельного эпика Translator
- HTTP/WebSocket API в `OPC_SERVER` (отклонён ADR-0016)
- Каталог вендорских device profiles сверх demo `generic-tank-sensor`
- Username token / industrial CA сверх fail-closed channel PKI
- Живой OTLP-коллектор в CI (сейчас только compile-in exporters)

## Исторический backlog (2019, закрыт)

Опрос IoT по Modbus TCP (карта → PDU → сеть → буфер → повтор) и внутрипрограммный доступ/консоль — покрыто этапами 1–2 (проект карт, Dispatcher, TagStore, `--watch`). UDP из того же списка закрыт инкрементом D.
