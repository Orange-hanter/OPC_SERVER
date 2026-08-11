# 08. Engineering standards

Правила, без которых код в репозиторий не принимается. Детали решений — в [ADR](adr/README.md).

## Цели качества

1. **Границы важнее фич.** Новый код не ломает hexagonal-слои ([ADR-0001](adr/0001-hexagonal-architecture.md)).
2. **Предсказуемая конкурентность.** Только модель из [ADR-0002](adr/0002-concurrency-model.md).
3. **Явные ошибки.** Нет «тихих» `catch (...)` и нет исключений как control flow для Modbus/UA ([ADR-0003](adr/0003-error-handling.md)).
4. **Тесты как контракт.** Поведение без теста — недоделано ([ADR-0004](adr/0004-testing-strategy.md)).
5. **Наблюдаемость.** Новый I/O-путь без метрик/логов — недоделан ([ADR-0008](adr/0008-observability.md)).

## Структура каталогов

```text
Src/
  domain/          # чистые типы: TagId, Quality, DataValue, RegisterValue (без I/O)
  ports/           # абстрактные интерфейсы (pure virtual / concepts)
  core/            # Dispatcher, Translator, TagStore, PollScheduler (зависит только от domain+ports)
  project/         # загрузка/валидация/migrate проектов карт
  adapters/        # (этап 2+) asio modbus, open62541, sqlite historian, spdlog metrics
  app/             # composition root: wiring зависимостей
tools/
  opc-map/         # CLI инженерии (ссылается на project/, не на adapters runtime)
tests/
  unit/
  component/
  contract/
DOCs/
  adr/             # Architecture Decision Records
```

### Правило зависимостей (enforce вручную + code review)

| Модуль | Может включать | Не может включать |
|--------|----------------|-------------------|
| `domain` | STL only | ports, core, adapters, project I/O |
| `ports` | domain | core, adapters |
| `core` | domain, ports | adapters, Asio, open62541, файловый I/O |
| `project` | domain, json | adapters runtime, TagStore |
| `adapters/*` | domain, ports, external libs | друг друга напрямую без необходимости |
| `app` | всё (только wiring) | бизнес-логику «в main» |
| `tools/opc-map` | project, domain | core poller, UA server |

Нарушение = блокер merge.

## Naming

| Сущность | Стиль | Пример |
|----------|--------|--------|
| Namespace | `opc::` + слой | `opc::core`, `opc::ports`, `opc::domain` |
| Типы / классы | `PascalCase` | `TagStore`, `ModbusTcpTransport` |
| Функции / методы | `snake_case` | `load_file`, `read_holding` |
| Переменные | `snake_case` | `period_ms` |
| Константы | `kCamelCase` или `ALL_CAPS` для макросов | `kDefaultPort` |
| Файлы | `snake_case.hpp/.cpp` | `tag_store.hpp` |
| Интерфейсы портов | префикс `I` | `IModbusTransport` |
| Тесты | `test_<area>.cpp` | `test_translator_byte_order.cpp` |

## C++ dialect

- Целевой стандарт: **C++26**; в CI допускается fallback **C++23** с предупреждением CMake.
- Предпочтения: `std::expected` (C++23), `std::span`, `std::string_view`, `enum class`, `[[nodiscard]]`.
- Reflection/contracts C++26 — только за feature-macro и с тестом на GCC 16+; не блокер portable-пути.
- Запрещено: raw `new`/`delete` без крайней нужды; `using namespace std` в заголовках; необязательный `shared_ptr` для ownership дерева объектов (предпочитать unique + ссылки/observer).

## Форматирование и static analysis

- `.clang-format` — обязателен; перед commit — format.
- `.clang-tidy` — обязательные checks; новые предупреждения в затронутых файлах не оставлять.
- Warnings as errors в Release CI.

## Комментарии и документация

- Комментарии объясняют **почему**, не что видно из кода.
- Публичные порты и ADR — на русском или английском единообразно в файле; в этом репозитории docs — **русский**, код/идентификаторы — **английский**.
- Существенное решение → ADR (не только Slack/чат).

## PR checklist (кратко)

См. [CONTRIBUTING.md](../CONTRIBUTING.md) и [10-quality-gates.md](10-quality-gates.md).
