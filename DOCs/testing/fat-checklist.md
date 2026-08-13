# Factory Acceptance Test (FAT)

Стенд на симуляторе Modbus (loopback slave, pymodbus или диаг-slave) и UA-клиенте
(UaExpert / open62541). Физический PLC не обязателен. Протокол заполняется перед
передачей сборки заказчику / на объект.

Версия ПО: _____________  Дата: _____________  Стенд: _____________  Исполнитель: _____________

## Предусловия

- [ ] Релизная сборка (SemVer-тег) или кандидат с SHA из CI
- [ ] Проект карты загружается: `opc-map validate <project>` → exit 0
- [ ] `opc-map doctor` без error (warnings осознаны и записаны)
- [ ] Политика UA security стенда зафиксирована (обычно None)

## Функциональные сценарии

- [ ] **FAT-1** Старт `OPC_SERVER --project <map>` слушает объявленный `opc.tcp`
- [ ] **FAT-2** Browse: дерево `nodePath` совпадает с картой (Plant/Tank/…)
- [ ] **FAT-3** Read: float32/uint16/bool с ожидаемым byte order и scale/offset
- [ ] **FAT-4** Subscription: изменение регистра симулятора приходит в MonitoredItem
- [ ] **FAT-5** Write уставки: UA Write → регистр симулятора; Quality Good
- [ ] **FAT-6** Non-writable тег: UA Write → `BadNotWritable`
- [ ] **FAT-7** Обрыв southbound: теги → Bad/Uncertain (`BadNoCommunication` / stale)
- [ ] **FAT-8** Восстановление связи: Quality возвращается в Good без рестарта процесса
- [ ] **FAT-9** Frame log (если включён) содержит MBAP request/response
- [ ] **FAT-10** Historian: значения переживают короткий restart hot-ring / SQLite flush
- [ ] **FAT-11** Studio (если в поставке): validate карты, read-only monitor, **нет** Write

## Нефункциональные (минимум стенда)

- [ ] **FAT-N1** Два endpoint не блокируют друг друга при timeout одного из них
- [ ] **FAT-N2** Write queue: переполнение даёт диагностируемую ошибку, не молчаливый drop
- [ ] **FAT-N3** Логи без секретов сертификатов; поля `endpoint_id` / `tag` / `error_code` присутствуют
- [ ] **FAT-N4** `--version` совпадает с этикеткой релиза

## Результат

Принято / принято с замечаниями / не принято.

Замечания (ID дефекта → уровень теста для regression: unit/component/integration/e2e):
