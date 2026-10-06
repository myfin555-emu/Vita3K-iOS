#!/usr/bin/env python3
"""Assemble ios/src/UpstreamMain.cpp from chunked parts (one-shot restore)."""
from pathlib import Path

here = Path(__file__).resolve().parent
parts = sorted(here.glob("part*.txt"))
if not parts:
    raise SystemExit(f"No part*.txt in {here}")
text = "".join(p.read_text() for p in parts)
root = here.parents[2]
target = root / "ios" / "src" / "UpstreamMain.cpp"
target.parent.mkdir(parents=True, exist_ok=True)
target.write_text(text)
print(f"Restored {target} ({len(text)} bytes) from {len(parts)} parts")
