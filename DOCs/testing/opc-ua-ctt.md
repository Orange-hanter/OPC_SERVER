# OPC UA Compliance Test Tool (CTT)

CTT Фонда OPC — лабораторная проверка сервера на соответствие Part 4/8.
Это **не** замена Catch2 smoke и **не** блокер pull request.

## Когда гонять

- Перед релизом `v1.x`, когда northbound заявлен как промышленный интерфейс.
- После изменений Information Model, DataSource, Subscriptions, security policy.
- Failures заносятся в backlog; известные ограничения v0.x (security None, нет certs)
  не блокируют merge.

## Профиль

Рекомендуемый стартовый профиль для open62541-адаптера:

- Server profile: **Microembedded Device Server** + **Data Access** (Read/Write/Subscriptions)
- Security: сначала **None** (lab); затем **Sign / SignAndEncrypt + Basic256Sha256** (этап 7)
- Transport: `opc.tcp` на loopback
- Карта: минимальный проект с 1 writable uint16, 1 float32, 1 bool

## Подготовка

1. Собрать Release: `cmake --preset release && cmake --build --preset release`
2. Запустить сервер:

```bash
./build/release/OPC_SERVER --project DOCs/examples/demo-plant.modbusproj.json
```

Для CTT southbound должен отвечать (симулятор или `--once` непригоден — нужен живой poll).
Поднять `LoopbackModbusSlave` / pymodbus и временный `.modbusproj.json` на 127.0.0.1.

3. Установить [OPC UA CTT](https://opcfoundation.org/developer-tools/certification-test-tools/opc-ua-compliance-test-tool-ua-ctt/)
   (нужна учётная запись OPC Foundation).
4. Указать endpoint `opc.tcp://127.0.0.1:4840`, namespace URI из проекта.

Обёртка: `scripts/run-opc-ua-ctt.sh` (печатает чеклист; сам CTT GUI/CLI вендорский).

## Интерпретация

| Класс CTT | Действие |
|-----------|----------|
| Failed на Read/Write/MonitoredItems при None | Дефект продукта → Catch2 regression |
| Failed на Session/SecureChannel при отсутствии certs | Ожидаемо до этапа 7 |
| Optional / not supported (historizing UA, method calls) | Не цель v1; задокументировать |

Ручной интероп (не CI): UaExpert Browse/Subscribe/Write; при наличии лицензии —
Ignition или WinCC как SCADA-клиент по тому же чеклисту FAT-2…FAT-6.
