#!/usr/bin/env python3
"""Assemble ios/src/UpstreamMain.cpp from zlib+base64 parts."""
import base64
import zlib
from pathlib import Path

here = Path(__file__).resolve().parent
# Prefer compressed zparts if present, else plain part*.txt
zparts = sorted(here.glob("zpart*.txt"))
if zparts:
    b64 = "".join(p.read_text().strip() for p in zparts)
    text = zlib.decompress(base64.b64decode(b64)).decode()
    n = len(zparts)
else:
    parts = sorted(here.glob("part*.txt"))
    if not parts:
        raise SystemExit(f"No zpart*.txt or part*.txt in {here}")
    text = "".join(p.read_text() for p in parts)
    n = len(parts)
root = here.parents[2]
target = root / "ios" / "src" / "UpstreamMain.cpp"
target.parent.mkdir(parents=True, exist_ok=True)
target.write_text(text)
print(f"Restored {target} ({len(text)} bytes) from {n} parts")
