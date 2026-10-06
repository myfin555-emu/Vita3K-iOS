#!/usr/bin/env python3
"""Restore surface_cache.cpp after accidental PLACEHOLDER overwrite.

Preferred:
  git show 83c64e0a:vita3k/renderer/src/vulkan/surface_cache.cpp \\
    > vita3k/renderer/src/vulkan/surface_cache.cpp
  patch -p1 < ios/patches/0003-god-eater-menu-surface-sync.patch

Or run this script from the repo root (needs that commit reachable).
"""
from __future__ import annotations

import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = ROOT / "vita3k" / "renderer" / "src" / "vulkan" / "surface_cache.cpp"
PATCH = ROOT / "ios" / "patches" / "0003-god-eater-menu-surface-sync.patch"
GOOD = "83c64e0a34966694abcc75707f32d270f5d89f34"

def main() -> int:
    try:
        raw = subprocess.check_output(
            ["git", "show", f"{GOOD}:vita3k/renderer/src/vulkan/surface_cache.cpp"],
            cwd=ROOT,
        )
    except subprocess.CalledProcessError as e:
        print("Could not read good revision from git:", e, file=sys.stderr)
        print("Copy surface_cache.cpp from artifacts/god-eater-fix/ instead.", file=sys.stderr)
        return 1
    OUT.write_bytes(raw)
    print(f"wrote unpatched {OUT} ({len(raw)} bytes)")
    if PATCH.exists():
        r = subprocess.run(["patch", "-p1", "--forward", str(PATCH)], cwd=ROOT)
        if r.returncode not in (0, 1):
            print("patch failed", r.returncode, file=sys.stderr)
            return r.returncode
        print("applied", PATCH)
    print("done — commit and push surface_cache.cpp")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
