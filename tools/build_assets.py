#!/usr/bin/env python3
"""Generate Galleria 2007 graphics and math tables (deterministic).

Outputs ``src/gen/assets_data.h`` with:
  * a quarter-wave sine table (Q12);
  * eight 32x32 4-bit luminance texture patterns (procedural, original);
  * 16-step colour ramps per material, expressed as PRG32 *system palette*
    indices (see docs/palette.md for why the fixed 6x6x6 cube is used);
  * original billboard sprites (4 bits per pixel + 16-entry palettes);
  * row-reciprocal, fog and flashlight lookup tables.

All randomness uses fixed seeds, so repeated runs produce identical bytes.
No photograph is used: when licensed Galleria photographs become available,
a photo-derived pattern can replace a procedural one here (same 32x32 4-bit
format) without touching the renderer. Previews are written to
``assets/textures/`` and ``assets/sprites/``.
"""
from __future__ import annotations

import math
import random
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import g2007_ids as ids  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
FOCAL = 176           # must match G2007_FOCAL in src/config.h
HORIZON = 100         # rows in the reciprocal table (>= rows above/below the horizon)
FOG_STEP_SHIFT = 6    # light tables are indexed by distance >> 6 (64 cm)
TEX = 32

# --------------------------------------------------------------------------
# PRG32 system palette (mirrors palette_default_color() in the firmware).
# Indices 16..231 form a 6x6x6 cube; 0..7 are named colours. Indices 8..15
# and 232..255 are deliberately unused because the firmware's indexed sprite
# path maps colours through the cube.
# --------------------------------------------------------------------------

LV5 = [0, 7, 13, 19, 25, 31]   # smallest values that quantise to each level
LV6 = [0, 13, 26, 38, 51, 63]   # (the ILI9341 driver uses component * 5 / max)


def cube_rgb565(r, g, b):
    return (LV5[r] << 11) | (LV6[g] << 5) | LV5[b]


def rgb565_to_rgb888(c):
    r, g, b = (c >> 11) & 31, (c >> 5) & 63, c & 31
    return (r * 255 // 31, g * 255 // 63, b * 255 // 31)


def system_palette():
    pal = [0] * 256
    named = [0x0000, 0xffff, 0xf800, 0x07e0, 0x001f, 0xffe0, 0x07ff, 0xf81f]
    for i in range(8):
        pal[i] = named[i]
    for i in range(16, 232):
        v = i - 16
        pal[i] = cube_rgb565(v // 36, (v // 6) % 6, v % 6)
    return pal


PAL = system_palette()
CUBE = [(i, rgb565_to_rgb888(PAL[i])) for i in range(16, 232)]


def nearest_cube(rgb):
    best, bi = 1e18, 16
    for i, (r, g, b) in CUBE:
        # perceptual-ish weights
        d = 3 * (r - rgb[0]) ** 2 + 4 * (g - rgb[1]) ** 2 + 2 * (b - rgb[2]) ** 2
        if d < best:
            best, bi = d, i
    return bi


def luma(rgb):
    return 0.299 * rgb[0] + 0.587 * rgb[1] + 0.114 * rgb[2]


def chroma(rgb):
    s = sum(rgb) + 1e-6
    return (rgb[0] / s, rgb[1] / s, rgb[2] / s)


def ramp(base):
    """16 cube indices from black to a warm highlight, hue-preserving.

    The 6x6x6 cube is coarse, so a naive nearest-colour search jumps between
    hues (tuff turning green or pink). Each level instead picks the cube
    colour that best matches the target *luminance* while staying close to
    the base *chromaticity*, and luminance never decreases along the ramp.
    """
    base_c = chroma(base)
    out, last_l = [], -1.0
    for k in range(16):
        if k == 0:
            out.append(16)
            last_l = 0.0
            continue
        t = k / 15.0
        f = t ** 1.1 * 1.1
        rgb = [min(255, c * f) for c in base]
        if k >= 13:                       # flashlight highlights warm to white
            w = (k - 12) / 3.0 * 0.3
            rgb = [c + (255 - c) * w for c in rgb]
        target_l, target_c = luma(rgb), chroma(rgb) if k >= 13 else base_c
        best, bi, bl = 1e18, 16, 0.0
        for i, cand in CUBE:
            q = ((i - 16) // 36, ((i - 16) // 6) % 6, (i - 16) % 6)   # cube coords
            cl = luma(cand)
            if cl + 1e-6 < last_l:
                continue
            # Never invert the base channel order (keeps tuff warm, water cool).
            if cl > 0 and any(base[a] > base[b] + 20 and q[a] <= q[b]
                   for a in range(3) for b in range(3)):
                continue
            # Near-neutral bases (whitewash, stone) stay grey-to-warm.
            if max(base) - min(base) < 40 and not (q[0] == q[1] and q[2] in (q[1], q[1] - 1)):
                continue
            cc = chroma(cand)
            hue_err = sum((a - b) ** 2 for a, b in zip(cc, target_c))
            if cl < 20:
                hue_err *= 0.2            # near-black hue is invisible
            score = (cl - target_l) ** 2 + 12000 * hue_err
            if score < best:
                best, bi, bl = score, i, cl
        out.append(bi)
        last_l = bl
    return out


# --------------------------------------------------------------------------
# Procedural texture patterns (values 0..15)
# --------------------------------------------------------------------------

def value_noise(rng, cells):
    grid = [[rng.random() for _ in range(cells)] for _ in range(cells)]

    def sample(x, y):
        fx, fy = x * cells / TEX, y * cells / TEX
        x0, y0 = int(fx) % cells, int(fy) % cells
        x1, y1 = (x0 + 1) % cells, (y0 + 1) % cells
        tx, ty = fx - int(fx), fy - int(fy)
        tx, ty = tx * tx * (3 - 2 * tx), ty * ty * (3 - 2 * ty)
        a = grid[y0][x0] * (1 - tx) + grid[y0][x1] * tx
        b = grid[y1][x0] * (1 - tx) + grid[y1][x1] * tx
        return a * (1 - ty) + b * ty
    return sample


def fbm(seed):
    rng = random.Random(seed)
    layers = [(value_noise(rng, 4), 0.5), (value_noise(rng, 8), 0.3), (value_noise(rng, 16), 0.2)]
    return lambda x, y: sum(s(x, y) * w for s, w in layers)


def clamp15(v):
    return max(0, min(15, int(round(v))))


def pat_rough(seed=11):
    n = fbm(seed)
    rng = random.Random(seed + 1)
    img = [[clamp15(6 + n(x, y) * 9) for x in range(TEX)] for y in range(TEX)]
    for _ in range(14):                   # pick/chisel marks typical of dug tuff
        x, y = rng.randrange(TEX), rng.randrange(TEX)
        for i in range(rng.randrange(3, 7)):
            xx, yy = (x + i) % TEX, (y + i // 2) % TEX
            img[yy][xx] = max(1, img[yy][xx] - 4)
    return img


def voronoi(seed, count, mortar, lo, hi, jitter=0):
    rng = random.Random(seed)
    pts = [(rng.random() * TEX, rng.random() * TEX, rng.randint(lo, hi)) for _ in range(count)]
    n = fbm(seed + 7)
    img = []
    for y in range(TEX):
        row = []
        for x in range(TEX):
            ds = []
            for px, py, v in pts:
                dx = min(abs(x - px), TEX - abs(x - px))
                dy = min(abs(y - py), TEX - abs(y - py))
                ds.append((dx * dx + dy * dy, v))
            ds.sort()
            d0, d1 = math.sqrt(ds[0][0]), math.sqrt(ds[1][0])
            if d1 - d0 < mortar:
                row.append(clamp15(3 + n(x, y) * 2))
            else:
                row.append(clamp15(ds[0][1] + (n(x, y) - 0.5) * 3 - d0 * jitter))
        img.append(row)
    return img


def pat_blocks(seed=23, bw=16, bh=8):
    n = fbm(seed)
    img = []
    for y in range(TEX):
        row = []
        for x in range(TEX):
            off = (bw // 2) if (y // bh) % 2 else 0
            lx, ly = (x + off) % bw, y % bh
            if ly == bh - 1 or lx == bw - 1:
                row.append(clamp15(3 + n(x, y) * 2))
            elif ly == 0 or lx == 0:
                row.append(clamp15(12 + n(x, y) * 3))
            else:
                row.append(clamp15(7 + n(x, y) * 6))
        img.append(row)
    return img


def pat_plaster(seed=31):
    n = fbm(seed)
    rng = random.Random(seed)
    img = [[clamp15(11 + n(x, y) * 4) for x in range(TEX)] for y in range(TEX)]
    for _ in range(4):                    # damp streaks running down the lime
        x = rng.randrange(TEX)
        for y in range(rng.randrange(8, TEX)):
            img[y][x] = max(5, img[y][x] - 3)
    for y in range(26, TEX):              # darker band near the floor
        for x in range(TEX):
            img[y][x] = max(4, img[y][x] - (y - 25))
    return img


def pat_water(seed=41):
    n = fbm(seed)
    img = []
    for y in range(TEX):
        row = []
        for x in range(TEX):
            v = 6 + 3 * math.sin((y + 2 * math.sin(x * 2 * math.pi / TEX)) * 2 * math.pi / 8) + n(x, y) * 3
            row.append(clamp15(v))
        img.append(row)
    return img


def pat_metal(seed=53):
    n = fbm(seed)
    img = []
    for y in range(TEX):
        row = []
        for x in range(TEX):
            lx = x % 8
            if lx in (2, 3, 4):
                v = 10 + n(x, y) * 4 - (1 if lx == 4 else 0)
            elif y % 16 in (0, 1):
                v = 9 + n(x, y) * 3        # horizontal bar
            else:
                v = 1 + n(x, y) * 2        # gaps behind the bars
            row.append(clamp15(v))
        img.append(row)
    return img


def patterns():
    return {
        "ROUGH": pat_rough(),
        "BLOCKS": pat_blocks(),
        "PLASTER": pat_plaster(),
        "MASONRY": voronoi(61, 14, 1.1, 7, 12, 0.15),
        "RUBBLE": voronoi(71, 22, 0.9, 5, 12, 0.35),
        "SLABS": pat_blocks(83, bw=16, bh=16),
        "WATER": pat_water(),
        "METAL": pat_metal(),
    }


def pack4(img):
    out = []
    for row in img:
        for x in range(0, len(row), 2):
            out.append((row[x] << 4) | row[x + 1])
    return out


# --------------------------------------------------------------------------
# Original sprites. '.' = transparent. Palette letters per sprite.
# z = height of the sprite's bottom above the floor (cm).
# --------------------------------------------------------------------------
SPRITE_ART = {
    "LAMPMAN": dict(world=(60, 180), z=0, pal={
        "a": (70, 50, 40), "s": (200, 150, 110), "e": (40, 30, 20), "c": (60, 70, 60),
        "y": (255, 220, 90), "W": (255, 255, 230), "h": (90, 80, 70), "p": (50, 50, 60),
        "b": (30, 25, 20)}, art="""
......aaaa......
.....aaaaaa.....
....aaaaaaaa....
.....ssssss.....
.....sesses.....
.....ssssss.....
......ssss......
.....cccccc.....
...cccccccccc...
..cccccccccccc..
..cccccccccccc..
..cc.cccccc.cc..
..cc.cccccc.cc..
..cc.cccccc.cc..
..cc.cccccc.cc..
..ss.cccccc.cc..
.yyy.cccccc.ss..
.yWy.cccccc.....
.yyy.cccccc.....
..h..cccccc.....
.....pppppp.....
.....pppppp.....
.....ppp.ppp....
.....pp...pp....
.....pp...pp....
.....pp...pp....
.....pp...pp....
.....pp...pp....
.....pp...pp....
.....pp...pp....
....bbb...bbb...
....bbb...bbb...
"""),
    # 'j' is the tint slot (palette entry 1) recoloured per remote player.
    "PLAYER": dict(world=(60, 175), z=0, pal={
        "j": (230, 120, 40), "k": (230, 200, 40), "W": (255, 255, 230), "s": (200, 150, 110),
        "e": (40, 30, 20), "p": (60, 60, 80), "b": (40, 30, 20), "g": (120, 120, 120)}, art="""
.....kkkkkk.....
....kkkkkkkk....
...kkkkWWkkkk...
....ssssssss....
....sesssses....
.....ssssss.....
......ssss......
....jjjjjjjj....
...jjjjjjjjjj...
..jjjjjjjjjjjj..
..jjjjggjjjjjj..
..jj.jjjjjj.jj..
..jj.jjjjjj.jj..
..jj.jjjjjj.jj..
..jj.jjjjjj.jj..
..ss.jjjjjj.ss..
.....jjjjjj.....
.....jjjjjj.....
.....pppppp.....
.....pppppp.....
.....pppppp.....
.....ppp.ppp....
.....pp...pp....
.....pp...pp....
.....pp...pp....
.....pp...pp....
.....pp...pp....
.....pp...pp....
.....pp...pp....
.....pp...pp....
....bbb...bbb...
....bbb...bbb...
"""),
    "CAR_A": dict(world=(380, 160), z=0, pal={
        "a": (70, 80, 90), "g": (30, 40, 50), "c": (160, 160, 150), "k": (20, 20, 20),
        "w": (110, 110, 110), "d": (110, 100, 80)}, art="""
................................
...........aaaaaaaaa............
.........aaggggagggggaa.........
........aagggggaggggggaa........
.......aaaaaaaaaaaaaaaaaaaa.....
....aaaaaaaaaaaaaaaaaaaaaaaaaa..
...aaaaaaaaaaaaaaaaaaaaaaaaaaaa.
...aaddaaaaaaaaaaaaaaaaaaaddaaa.
...aaaaaaaaaaaaaaaaaaaaaaaaaaaa.
..ccaaaaaaaaaaaaaaaaaaaaaaaaaacc
..caaaa.kkkk.aaaaaaaaa.kkkk.aaac
......kkkkkkk.........kkkkkkk...
......kkwwwkk.........kkwwwkk...
......kkkkkkk.........kkkkkkk...
.......kkkkk...........kkkkk....
................................
"""),
    "CAR_B": dict(world=(320, 150), z=0, pal={
        "a": (120, 50, 45), "g": (30, 35, 40), "c": (170, 170, 160), "k": (20, 20, 20),
        "w": (110, 110, 110), "d": (120, 110, 90)}, art="""
................................
................................
..........aaaaaaaaaa............
........aaaggggaggggaa..........
.......aagggggaagggggaa.........
......aaaaaaaaaaaaaaaaaaa.......
...aaaaaaaaaaaaaaaaaaaaaaaaa....
..aaaaaaaaaaaaaaaaaaaaaaaaaaa...
..aaddaaaaaaaaaaaaaaaaaaaddaa...
..caaaaaaaaaaaaaaaaaaaaaaaaaac..
..aaa.kkkkk.aaaaaaaa.kkkkk.aaa..
.....kkkkkkk........kkkkkkk.....
.....kkwwwkk........kkwwwkk.....
.....kkkkkkk........kkkkkkk.....
......kkkkk..........kkkkk......
................................
"""),
    "MOTO": dict(world=(200, 120), z=0, pal={
        "a": (90, 60, 40), "m": (140, 140, 140), "k": (20, 20, 20), "s": (60, 40, 30)}, art="""
........................
........................
.................mm.....
................m.......
.......ssss....m........
......ssssss..aaa.......
.....aaaaaaaaaaaa.......
....aaaaaaaaaaaaaa......
...mmm.aaaaaaa..mm......
..kkkkk..mmm...kkkkk....
.kk...kk.mm...kk...kk...
.k..m..k.....k...m...k..
.kk...kk......kk...kk...
..kkkkk........kkkkk....
........................
........................
"""),
    "MARK": dict(world=(90, 45), z=110, pal={
        "l": (235, 235, 225), "t": (40, 40, 40), "r": (150, 40, 30)}, art="""
llllllllllllllll
ltttll.ttt.tttll
llllllllllllllll
lt.ttttl.ttltttl
llllllllllllllll
lrrrrrrrrrrrrrrl
llllllllllllllll
................
"""),
    "ITEM_NOTEBOOK": dict(world=(30, 30), z=0, pal={
        "b": (40, 60, 120), "p": (230, 230, 210), "k": (20, 20, 20)}, art="""
............
............
............
............
...bbbbbbb..
..bbppppppb.
..bbpkkkppb.
..bbppppppb.
..bbpkkkkpb.
..bbppppppb.
...bbbbbbb..
............
"""),
    "ITEM_CAMERA": dict(world=(30, 30), z=0, pal={
        "k": (30, 30, 30), "m": (150, 150, 150), "g": (90, 140, 200)}, art="""
............
............
............
............
....mm......
..kkkkkkkk..
..kmmkkkkk..
..kkkggkkk..
..kkgggkkk..
..kkkggkkk..
..kkkkkkkk..
............
"""),
    "ITEM_ROPE": dict(world=(36, 30), z=0, pal={
        "r": (200, 170, 110), "d": (130, 100, 60)}, art="""
............
............
............
............
...rrrrrr...
..rddddddr..
.rdrrrrrrdr.
.rdr....rdr.
.rdrrrrrrdr.
..rddddddr..
...rrrrrr...
............
"""),
    "ITEM_BATTERY": dict(world=(24, 30), z=0, pal={
        "g": (60, 160, 60), "k": (30, 30, 30), "m": (200, 200, 200)}, art="""
............
............
............
.....mm.....
....kkkk....
....gggg....
....gggg....
....kkkk....
....gggg....
....gggg....
....kkkk....
............
"""),
    "ITEM_KEY": dict(world=(26, 26), z=0, pal={
        "y": (210, 170, 60), "d": (130, 100, 40)}, art="""
............
............
............
............
............
..yyy.......
.y...yyyyyy.
.y...ydd.dy.
..yyy.......
............
............
............
"""),
    "ITEM_TOOL": dict(world=(50, 24), z=0, pal={
        "m": (90, 90, 100), "r": (170, 60, 40)}, art="""
............
............
............
............
............
............
..........m.
.mmmmmmmmmm.
rr.........m
r...........
............
............
"""),
    "ITEM_MAP": dict(world=(30, 26), z=0, pal={
        "p": (220, 200, 150), "k": (110, 80, 40)}, art="""
............
............
............
............
............
..pppppppp..
..pkpppkpp..
..ppkkpppp..
..pppkkkpp..
..pkppppkp..
..pppppppp..
............
"""),
}


def build_sprites():
    infos, pixels, pals = [], [], []
    for name in ids.SPRITES:
        spec = SPRITE_ART[name]
        rows = [r for r in spec["art"].strip("\n").split("\n")]
        w = len(rows[0])
        if any(len(r) != w for r in rows) or w % 2:
            raise SystemExit(f"sprite {name}: rows must share one even width")
        keys = list(spec["pal"].keys())
        if len(keys) > 15:
            raise SystemExit(f"sprite {name}: at most 15 colours")
        pal = [0] + [nearest_cube(spec["pal"][k]) for k in keys]
        pal += [0] * (16 - len(pal))
        img = [[0 if ch == "." else keys.index(ch) + 1 for ch in row] for row in rows]
        infos.append(dict(name=name, off=len(pixels), w=w, h=len(rows),
                          ww=spec["world"][0], wh=spec["world"][1], z=spec["z"]))
        pixels += pack4(img)
        pals.append(pal)
    return infos, pixels, pals


def tables():
    sinq = [int(round(math.sin(i / 256 * math.pi / 2) * 4096)) for i in range(257)]
    recip = [0] + [min(65535, (FOCAL * 256) // k) for k in range(1, HORIZON + 1)]
    fog, flash = [], []
    for i in range(64):
        d = (i << FOG_STEP_SHIFT) + 32
        fog.append(max(0, min(16, int(round(16 * math.exp(-d / 1400.0))))))
        flash.append(max(0, min(16, int(round(16 * (1.0 - d / 2600.0) ** 1.6 if d < 2600 else 0)))))
    return sinq, recip, fog, flash


def c_array(ctype, name, values, per_line=16, size=None):
    size = size if size is not None else len(values)
    out = [f"static const {ctype} {name}[{size}] = {{"]
    for i in range(0, len(values), per_line):
        out.append("    " + ", ".join(str(v) for v in values[i:i + per_line]) + ",")
    out.append("};")
    return out


def previews(pats, ramps, infos, pixels, pals):
    try:
        from PIL import Image
    except ImportError:
        return
    tdir = ROOT / "assets/textures"
    tdir.mkdir(parents=True, exist_ok=True)
    sheet = Image.new("RGB", (len(ids.TEXTURES) * 36, 36), (0, 0, 0))
    for t, (name, pat, mat) in enumerate(ids.TEXTURES):
        img = pats[pat]
        r = ramps[ids.index(ids.MATERIALS, mat, "material")]
        for y in range(TEX):
            for x in range(TEX):
                sheet.putpixel((t * 36 + 2 + x, 2 + y), rgb565_to_rgb888(PAL[r[img[y][x]]]))
    sheet.resize((sheet.width * 3, sheet.height * 3), Image.NEAREST).save(tdir / "texture_sheet.png")
    sdir = ROOT / "assets/sprites"
    sdir.mkdir(parents=True, exist_ok=True)
    width = sum(i["w"] + 4 for i in infos)
    sheet = Image.new("RGB", (width, 36), (40, 40, 40))
    x0 = 0
    for info, pal in zip(infos, pals):
        for y in range(info["h"]):
            for x in range(info["w"]):
                b = pixels[info["off"] + (y * info["w"] + x) // 2]
                v = (b >> 4) if x % 2 == 0 else (b & 15)
                if v:
                    sheet.putpixel((x0 + 2 + x, 2 + y), rgb565_to_rgb888(PAL[pal[v]]))
        x0 += info["w"] + 4
    sheet.resize((sheet.width * 3, sheet.height * 3), Image.NEAREST).save(sdir / "sprite_sheet.png")


def main():
    pats = patterns()
    ramps = [ramp(base) for _, base in ids.MATERIALS]
    infos, pixels, pals = build_sprites()
    sinq, recip, fog, flash = tables()
    out = ["/* Generated by tools/build_assets.py. Do not edit by hand.",
           " * Procedural, original graphics: no photographs are embedded. */",
           "#ifndef G2007_GEN_ASSETS_DATA_H", "#define G2007_GEN_ASSETS_DATA_H", "",
           f"#define GEN_FOCAL {FOCAL}", f"#define GEN_HORIZON {HORIZON}",
           f"#define GEN_FOG_STEP_SHIFT {FOG_STEP_SHIFT}", ""]
    out += [f"static const uint8_t g_patterns[PAT_COUNT][512] = {{"]
    for name in ids.PATTERNS:
        packed = pack4(pats[name])
        out.append(f"    {{ /* {name} */")
        for i in range(0, 512, 32):
            out.append("        " + ",".join(str(v) for v in packed[i:i + 32]) + ",")
        out.append("    },")
    out += ["};", ""]
    out += ["static const uint8_t g_ramps[MAT_COUNT][16] = {"]
    for (name, _), r in zip(ids.MATERIALS, ramps):
        out.append(f"    {{{', '.join(str(v) for v in r)}}}, /* {name} */")
    out += ["};", ""]
    out += c_array("uint8_t", "g_tex_pattern",
                   [ids.PATTERNS.index(p) for _, p, _ in ids.TEXTURES], size="TEX_COUNT")
    out += c_array("uint8_t", "g_tex_material",
                   [ids.index(ids.MATERIALS, m, "material") for _, _, m in ids.TEXTURES],
                   size="TEX_COUNT") + [""]
    out += ["static const SpriteInfo g_sprite_info[SPR_COUNT] = {",
            "    /* offset, w, h, world_w, world_h, z */"]
    for i in infos:
        out.append(f"    {{{i['off']}, {i['w']}, {i['h']}, {i['ww']}, {i['wh']}, {i['z']}}}, /* {i['name']} */")
    out += ["};", ""]
    out += c_array("uint8_t", "g_sprite_pixels", pixels, per_line=24) + [""]
    out += ["static const uint8_t g_sprite_pal[SPR_COUNT][16] = {"]
    for i, p in zip(infos, pals):
        out.append(f"    {{{', '.join(str(v) for v in p)}}}, /* {i['name']} */")
    out += ["};", ""]
    out += c_array("uint16_t", "g_recip_row", recip, size="GEN_HORIZON + 1") + [""]
    out += c_array("uint8_t", "g_fog", fog, size="64")
    out += c_array("uint8_t", "g_flash", flash, size="64") + ["", "#endif", ""]
    (ROOT / "src/gen/assets_data.h").write_text("\n".join(out), encoding="utf-8")
    trig = ["/* Generated by tools/build_assets.py. Do not edit. */",
            "#ifndef G2007_GEN_TRIG_H", "#define G2007_GEN_TRIG_H", ""]
    trig += c_array("int16_t", "g_sin_quarter", sinq, size="257") + ["", "#endif", ""]
    (ROOT / "src/gen/trig_table.h").write_text("\n".join(trig), encoding="utf-8")
    previews(pats, ramps, infos, pixels, pals)
    total = 514 + 512 * len(ids.PATTERNS) + 16 * len(ids.MATERIALS) + len(pixels) + 16 * len(infos) + 8 * len(infos) + 202 + 128
    print(f"assets: {len(ids.PATTERNS)} patterns, {len(infos)} sprites, {len(pixels)} sprite bytes, ~{total} bytes total")


if __name__ == "__main__":
    main()
