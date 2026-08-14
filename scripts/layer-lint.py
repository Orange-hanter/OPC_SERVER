#!/usr/bin/env python3
"""Fail if hexagonal layer include rules from DOCs/08-engineering-standards.md are broken."""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "Src"
INCLUDE = re.compile(r'^\s*#\s*include\s+"([^"]+)"')

# module -> forbidden path prefixes in quoted includes
FORBIDDEN: dict[str, tuple[str, ...]] = {
    "domain": ("ports/", "core/", "adapters/", "project/", "app/"),
    "ports": ("core/", "adapters/", "app/"),
    "core": ("adapters/", "app/"),
    "project": ("adapters/", "core/", "app/", "ports/"),
    "adapters": ("core/", "app/"),
}

# tools/opc-map may use project + domain only (enforced separately)
OPC_MAP = ROOT / "tools" / "opc-map"


def module_of(path: Path) -> str | None:
    try:
        rel = path.relative_to(SRC)
    except ValueError:
        return None
    parts = rel.parts
    if not parts:
        return None
    return parts[0]


def check_file(path: Path, module: str) -> list[str]:
    forbidden = FORBIDDEN.get(module)
    if not forbidden:
        return []
    errors: list[str] = []
    for lineno, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
        match = INCLUDE.match(line)
        if not match:
            continue
        inc = match.group(1)
        for prefix in forbidden:
            if inc.startswith(prefix):
                errors.append(f"{path.relative_to(ROOT)}:{lineno}: {module} must not include '{inc}'")
    return errors


def check_opc_map() -> list[str]:
    errors: list[str] = []
    if not OPC_MAP.exists():
        return errors
    for path in OPC_MAP.rglob("*.cpp"):
        for lineno, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
            match = INCLUDE.match(line)
            if not match:
                continue
            inc = match.group(1)
            if inc.startswith(("adapters/", "core/", "app/")):
                errors.append(
                    f"{path.relative_to(ROOT)}:{lineno}: opc-map must not include runtime '{inc}'"
                )
    return errors


def main() -> int:
    errors: list[str] = []
    for path in SRC.rglob("*"):
        if path.suffix not in {".hpp", ".cpp"}:
            continue
        module = module_of(path)
        if module is None:
            continue
        errors.extend(check_file(path, module))
    errors.extend(check_opc_map())
    if errors:
        print("layer-lint failed:")
        for item in errors:
            print(f"  {item}")
        return 1
    print("layer-lint: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
