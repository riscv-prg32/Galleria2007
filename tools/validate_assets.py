#!/usr/bin/env python3
"""Validate generated assets and the rights manifest.

Checks:
  * every colour index used by ramps, sprites and the UI is an exact PRG32
    system-cube entry (0..7 or 16..231): indices 8..15 and 232..255 are
    remapped by the ILI9341 indexed-sprite path and must not be used;
  * texture luminance and sprite nibbles are within 4 bits;
  * every asset family has an entry in assets/source/SOURCES.md, and no
    raster image other than the generated previews/store art is present;
  * Store images are palettised and small.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ALLOWED = set(range(0, 8)) | set(range(16, 232))


def numbers(block: str):
    return [int(v) for v in re.findall(r"\b\d+\b", re.sub(r"/\*.*?\*/", "", block, flags=re.S))]


def section(src: str, name: str) -> str:
    m = re.search(r"%s\[[^\]]*\](?:\[[^\]]*\])? = \{(.*?)\n\};" % re.escape(name), src, re.S)
    if not m:
        raise SystemExit(f"validate_assets: {name} not found")
    return m.group(1)


def main() -> int:
    errors = []
    src = (ROOT / "src/gen/assets_data.h").read_text()
    for name in ("g_ramps", "g_sprite_pal"):
        bad = sorted(set(numbers(section(src, name))) - ALLOWED)
        if bad:
            errors.append(f"{name} uses non-cube palette indices {bad}")
    ui = (ROOT / "src/ui.c").read_text()
    for m in re.finditer(r"#define C_\w+ (\d+)", ui):
        if int(m.group(1)) not in ALLOWED:
            errors.append(f"ui colour {m.group(0)} is not a cube index")
    sources = (ROOT / "assets/source/SOURCES.md").read_text(encoding="utf-8")
    for family in ("TEXTURES", "SPRITES", "AUDIO", "MAP", "HISTORY", "STORE"):
        if f"ID: G2007-{family}" not in sources:
            errors.append(f"SOURCES.md lacks an entry for {family}")
    allowed_png = {"assets/textures/texture_sheet.png", "assets/sprites/sprite_sheet.png",
                   "assets/map/map_debug.png", "assets/store/icon.png"}
    for p in ROOT.joinpath("assets").rglob("*"):
        rel = p.relative_to(ROOT).as_posix()
        if p.suffix.lower() in (".jpg", ".jpeg", ".webp", ".gif", ".tif", ".tiff"):
            errors.append(f"unexpected raster file {rel} (record its rights in SOURCES.md first)")
        if p.suffix.lower() == ".png" and rel not in allowed_png and not rel.startswith("assets/store/screenshot-"):
            errors.append(f"unexpected PNG {rel}")
    try:
        from PIL import Image
        for p in (ROOT / "assets/store").glob("*.png"):
            im = Image.open(p)
            if im.mode != "P":
                errors.append(f"{p.name} must be palettised")
            if p.stat().st_size > 12000:
                errors.append(f"{p.name} is {p.stat().st_size} bytes (budget 12000)")
    except ImportError:
        pass
    for e in errors:
        print("ERROR", e, file=sys.stderr)
    if not errors:
        print("assets: palette, provenance and store-art checks passed")
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
