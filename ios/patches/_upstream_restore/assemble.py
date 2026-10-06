#!/usr/bin/env python3
"""Assemble ios/src/UpstreamMain.cpp from zlib+base64 payload."""
import base64
import zlib
from pathlib import Path

here = Path(__file__).resolve().parent
zfile = here / "UpstreamMain.cpp.z64.txt"
if not zfile.exists():
    raise SystemExit(f"missing {zfile}")
text = zlib.decompress(base64.b64decode(zfile.read_text().strip())).decode()
root = here.parents[2]
target = root / "ios" / "src" / "UpstreamMain.cpp"
target.parent.mkdir(parents=True, exist_ok=True)
target.write_text(text)
print(f"Restored {target} ({len(text)} bytes)")
