# ADR-0014: Historian hot/cold and Modbus frame log

- Status: Accepted
- Date: 2026-08-11

## Context

Stage 5 needs a local gateway buffer for short-horizon trends/debug and a PCAP-like Modbus journal, without coupling core poll logic to SQLite or filesystem I/O.

## Decision

1. **`IHistorian`** remains the port. Hot layer is `RingHistorian` (fixed-capacity ring). Cold layer is `SqliteHistorian` (hot ring + pending queue flushed to SQLite on `flush()`).
2. **Subscription**: `ServerRuntime` subscribes the historian to `TagStore` on `start()` and unsubscribes/`flush()` on `stop()`. Each `poll_once` also flushes cold pending.
3. **`IFrameLog`**: optional adapter injected into `ModbusTcpTransport` via `ModbusTcpTransportOptions`. Logs full MBAP+PDU TX/RX, RTT, exception, endpoint id.
4. **Metrics**: `MemoryMetrics` replaces production `NullMetrics` as an in-process sink (`historian.dropped`, dispatcher counters). OTel/spdlog exporters can wrap the same port later.
5. **Replay**: `historian_replay.hpp` publishes samples back into `ITagStore` (no field I/O).

## Consequences

- Core stays free of SQLite and frame journaling.
- Cold retention/compaction policies remain future work; v1 is append-only SQLite + bounded hot ring.
- Frame log is opt-in via `--frame-log`; historian defaults on (disable with `--no-historian`).
