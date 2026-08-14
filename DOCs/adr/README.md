# Architecture Decision Records

ADR фиксируют **существенные** решения: контекст, варианты, последствия.  
Статусы: `Proposed` → `Accepted` → `Deprecated` / `Superseded`.

| ADR | Тема | Статус |
|-----|------|--------|
| [0001](0001-hexagonal-architecture.md) | Hexagonal / Ports & Adapters | Accepted |
| [0002](0002-concurrency-model.md) | Модель конкурентности (Asio strands) | Accepted |
| [0003](0003-error-handling.md) | Ошибки и `Result` | Accepted |
| [0004](0004-testing-strategy.md) | Пирамида тестов (обновлён 2026-08-13) | Accepted |
| [0005](0005-config-immutability.md) | Иммутабельность проекта карт | Accepted |
| [0006](0006-tagstore-data-model.md) | TagStore и модель значения | Accepted |
| [0007](0007-modbus-transport-port.md) | Абстракция Modbus transport | Accepted |
| [0008](0008-observability.md) | Логи, метрики, трассы | Accepted |
| [0009](0009-northbound-opcua-boundary.md) | Граница OPC UA vs Classic/DA | Accepted |
| [0010](0010-scalability-model.md) | Модель масштабирования | Accepted |
| [0011](0011-server-runtime.md) | ServerRuntime composition root | Accepted |
| [0012](0012-ci-releases.md) | CI artifacts and SemVer releases | Accepted |
| [0013](0013-post-mvp-hardening.md) | Hardening after Stage 1–4 review | Accepted |
| [0014](0014-historian-debug.md) | Historian hot/cold + Modbus frame log | Accepted |
| [0015](0015-spdlog-otel.md) | spdlog + OpenTelemetry metrics adapters | Accepted |
| [0016](0016-engineering-studio-monitoring.md) | Engineering Studio и read-only monitoring | Accepted |

Новый ADR: скопировать структуру (Context / Decision / Alternatives / Consequences), следующий номер, ссылка в этой таблице и в [08-engineering-standards](../08-engineering-standards.md) при необходимости.
