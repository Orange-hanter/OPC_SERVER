# ADR-0016: Engineering Studio и read-only remote monitoring

- **Status:** Accepted
- **Date:** 2026-08-11

## Context

Engineering Studio должен позволять инженеру удалённо просматривать дерево и живые
значения OPC UA сервера, не связывая desktop/frontend-процесс с ABI open62541 и не
добавляя в сервер управляющий API. Мониторинг не должен менять значения или
конфигурацию промышленного сервера.

## Decision

1. **Engineering Studio** — отдельный инженерный интерфейс. Он подключается к
   стандартному OPC UA endpoint и не входит в runtime `OPC_SERVER`.
2. Удалённый мониторинг **read-only**: разрешены connect, browse, subscribe,
   unsubscribe и disconnect. Write, Call и изменение subscriptions на сервере вне
   владения клиента не поддерживаются.
3. Нативная граница реализована процессом `opc-monitor` на open62541. Studio запускает
   его локально и общается через UTF-8 **JSON Lines**: одна JSON-команда в строке
   stdin, одно JSON-событие в строке stdout. Диагностические сообщения процесса не
   смешиваются с протоколом stdout.
4. Команды: `connect`, `browse`, `subscribe`, `unsubscribe`, `disconnect`,
   `shutdown`. События: `connection`, `browseResult`, `dataChange`, `error`.
   Опциональный `requestId` копируется в непосредственный ответ или ошибку.
5. Потеря transport-соединения публикуется как `connection`; sidecar выполняет
   ограниченные периодические попытки reconnect к последнему endpoint и
   восстанавливает принадлежащие ему subscriptions.
6. Сервер публикует надежные read-only value nodes в
   `Objects/OPC_SERVER/Diagnostics`: `State`, `GoodCount`, `UncertainCount`,
   `BadCount`, `LastError`. Счётчики отражают текущее качество последних значений
   TagStore. OPC UA Events могут быть добавлены позднее, но не заменяют эти узлы.

## Alternatives

| Вариант | Почему отклонён |
|---------|-----------------|
| Линковать open62541 во frontend | Нативный ABI и lifecycle усложняют Studio и его обновление |
| WebSocket/HTTP API внутри сервера | Дублирует OPC UA browse/subscription и расширяет attack surface |
| Разрешить write через monitor | Нарушает безопасную границу наблюдения |
| Только OPC UA Events | Сложнее диагностировать и тестировать; события можно потерять |

## Consequences

- Studio и open62541 обновляются независимо, а IPC можно воспроизводить из shell.
- Каждый процесс Studio имеет собственную OPC UA session и subscriptions.
- JSON Lines требует ограничения размера строки и строгой валидации входа.
- Sidecar не является security boundary: endpoint всё равно должен применять
  аутентификацию и сетевые политики.
