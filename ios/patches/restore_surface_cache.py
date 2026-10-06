#!/usr/bin/env python3
"""Restore vita3k/renderer/src/vulkan/surface_cache.cpp from compressed payload."""
from __future__ import annotations

import base64
import pathlib
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[2]
Z64 = pathlib.Path(__file__).with_name("surface_cache.cpp.z64")
OUT = ROOT / "vita3k" / "renderer" / "src" / "vulkan" / "surface_cache.cpp"

def main() -> None:
    data = Z64.read_text().strip()
    OUT.write_bytes(zlib.decompress(base64.b64decode(data)))
    print(f"restored {OUT} ({OUT.stat().st_size} bytes)")

if __name__ == "__main__":
    main()
