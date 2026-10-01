#!/usr/bin/env python3
"""Make the Cartridge Store icon and the per-language screenshots.

  assets/store/icon.png               64x64, composed from the game's own sprite
                                      and texture data (tools/build_assets.py)
  assets/store/screenshot-<lang>.png  320x200 real gameplay frame rendered by
                                      the host build of the cartridge code

The host framebuffer is pixel-identical to the cartridge strips, so run
`tests/run_tests.sh shots` first. Images are palettised because the Store
trailer counts towards the 64 KiB package limit. No concept art is used.
"""
from __future__ import annotations

import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import build_assets as ba  # noqa: E402
import g2007_ids as ids  # noqa: E402


def rgb(index: int):
    return ba.rgb565_to_rgb888(ba.PAL[index])


def icon() -> Image.Image:
    pats = ba.patterns()
    tuf = ba.ramp(dict(ids.MATERIALS)["TUF"])
    img = Image.new("RGB", (64, 64), rgb(16))
    rough = pats["ROUGH"]
    # Tuff arch: textured frame with a dark tunnel mouth.
    for y in range(64):
        for x in range(64):
            dx, dy = (x - 32) / 22.0, (y - 40) / 30.0
            inside = (dx * dx + dy * dy < 1.0 and y < 40) or (abs(x - 32) < 22 and y >= 40)
            if not inside:
                lum = rough[y % 32][x % 32]
                shade = max(1, min(15, lum * (12 - abs(x - 32) // 6) // 12))
                img.putpixel((x, y), rgb(tuf[shade]))
    # LampMan in the dark, holding his lamp: the sprite itself, enlarged.
    infos, pixels, pals = ba.build_sprites()
    info = infos[ids.SPRITES.index("LAMPMAN")]
    pal = pals[ids.SPRITES.index("LAMPMAN")]
    for y in range(info["h"]):
        for x in range(info["w"]):
            b = pixels[info["off"] + (y * info["w"] + x) // 2]
            v = (b >> 4) if x % 2 == 0 else (b & 15)
            if v:
                img.putpixel((24 + x, 30 + y), rgb(pal[v]))
    # Lamp glow.
    for y in range(64):
        for x in range(64):
            d2 = (x - 26) ** 2 + (y - 47) ** 2
            if d2 <= 2:
                img.putpixel((x, y), rgb(1))
            elif d2 <= 9 and (x + y) % 2 == 0:
                img.putpixel((x, y), rgb(228))
    return img


def main() -> int:
    out = ROOT / "assets/store"
    out.mkdir(parents=True, exist_ok=True)
    icon().quantize(colors=32).save(out / "icon.png", optimize=True)
    for lang in sorted(p.stem for p in (ROOT / "lang").glob("*.json")):
        shot = ROOT / "build/shots" / lang / "store_screenshot.png"
        if not shot.exists():
            raise SystemExit(f"{shot} missing: run tests/run_tests.sh shots first")
        frame = Image.open(shot).convert("RGB")
        frame.quantize(colors=48, dither=Image.Dither.NONE).save(out / f"screenshot-{lang}.png", optimize=True)
    for f in sorted(out.glob("*.png")):
        print(f"{f.relative_to(ROOT)}: {f.stat().st_size} bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
