#!/usr/bin/env python3
"""Minimal OTLP/HTTP sink for local/CI smoke when Docker collector is unavailable.

Accepts POST /v1/metrics and /v1/traces (protobuf or JSON) and appends one JSON
line per request to the receipt file so Catch2 can assert delivery.
"""

from __future__ import annotations

import argparse
import json
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any


class Handler(BaseHTTPRequestHandler):
    receipt_path: Path

    def log_message(self, fmt: str, *args: Any) -> None:  # noqa: A003
        sys.stderr.write("%s - %s\n" % (self.address_string(), fmt % args))

    def do_POST(self) -> None:  # noqa: N802
        length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(length) if length > 0 else b""
        signal = "unknown"
        if self.path.startswith("/v1/metrics"):
            signal = "metrics"
        elif self.path.startswith("/v1/traces"):
            signal = "traces"
        elif self.path.startswith("/v1/logs"):
            signal = "logs"

        record = {
            "signal": signal,
            "path": self.path,
            "content_type": self.headers.get("Content-Type", ""),
            "bytes": len(body),
        }
        self.receipt_path.parent.mkdir(parents=True, exist_ok=True)
        with self.receipt_path.open("a", encoding="utf-8") as out:
            out.write(json.dumps(record) + "\n")
            out.flush()

        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.end_headers()
        self.wfile.write(b"{}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=4318)
    parser.add_argument("--receipt", required=True, help="JSONL receipt path")
    parser.add_argument("--ready", default="", help="Optional readiness file touched after bind")
    args = parser.parse_args()

    Handler.receipt_path = Path(args.receipt)
    if Handler.receipt_path.exists():
        Handler.receipt_path.unlink()

    server = ThreadingHTTPServer((args.host, args.port), Handler)
    print(f"otlp_http_receiver listening on http://{args.host}:{args.port}", flush=True)
    print(f"receipt={Handler.receipt_path}", flush=True)
    if args.ready:
        ready = Path(args.ready)
        ready.parent.mkdir(parents=True, exist_ok=True)
        ready.write_text("ready\n", encoding="utf-8")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
