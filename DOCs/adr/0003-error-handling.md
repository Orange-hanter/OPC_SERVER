# ADR-0003: Error handling

- **Status:** Accepted
- **Date:** 2026-08-11

## Context

Смешение исключений, errno, bool и «магических» кодов делает SCADA-шлюз опасным: потеря quality, скрытые reconnect-штормы, недетерминизм.

## Decision

### Три класса исходов

| Класс | Представление | Примеры |
|-------|---------------|---------|
| Ожидаемые доменные/протокольные | `opc::Result<T>` / `std::expected<T, Error>` | Modbus exception, timeout, validation diagnostic |
| Инварианты / баги | `assert` / contract (где доступно) | Нарушение strand, null port |
| Фатальный init | exception до входа в main loop | Не открылся listen socket, нет прав на cert |

### `Error`

Минимальные поля:

- `code` (`enum class ErrorCode`)
- `message` (человекочитаемо)
- `path` / `component` (опционально: endpoint id, tag name)
- `retryable` (bool)

### Запрещено

- `catch (...)` без rethrow/log в library code.
- Глотать ошибку опроса без обновления `Quality` тега.
- Использовать исключения для «slave returned 0x03».

### Границы

- `project::load_*` → `LoadResult` с diagnostics (уже так).
- Transport port → `Result<Response>`.
- Translator → `Result<DataValue>` / `Result<RawRegisters>`.

## Alternatives

| Вариант | Почему отклонён |
|---------|-----------------|
| Exceptions everywhere | Плохо стыкуется с Asio completion handlers |
| Только errno/int codes | Теряем контекст и типобезопасность |
| absl::Status | Лишняя зависимость; `std::expected` достаточно |

## Consequences

- Все публичные методы портов `[[nodiscard]]`.
- Адаптеры мапят native errors → `ErrorCode` централизованно.
- Тесты обязаны проверять negative paths (timeout, exception code, bad byte order).
