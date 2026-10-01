# Renderer

A Doom-like 2.5D renderer built for PRG32's constraints: no floating point,
no full framebuffer owned by the cartridge, one blit API, a 64 KiB RAM window.
It is *not* a grid raycaster (heights vary per sector) and *not* a general 3D
engine (no meshes, no slopes, no pitch).

## World representation

The map (`assets/map/galleria2007.json`) is a list of **convex** polygons
wound counter-clockwise, in centimetres, each with floor and ceiling heights,
light level, textures, zone and provenance. `tools/build_map.py` re-centres
the coordinates, deduplicates vertices and finds **portals** automatically:
two sectors are connected wherever one contains the reversed edge of the
other. For every wall it stores the inward unit normal (Q12), the portal
neighbour and the index of the *mate* wall on the other side.

## Column portal raycasting

For each screen column `x` the ray direction is

```text
camx = (2x + 1 - 320) / 320                 (Q12)
R    = forward + plane * camx               (forward has length 1.0 = 4096)
```

so the length of `R` along the view axis is exactly 1 and every distance
below is already **perpendicular** (no fish-eye correction).

Starting in the camera's sector with the clip window `[ytop, ybot] = [0,199]`:

1. **Exit wall without division.** For a convex counter-clockwise polygon,
   the ray leaves through the unique edge A→B with `side(A) < 0 ≤ side(B)`,
   where `side(V) = cross(R, V - P)`. Only multiplications and sign tests.
2. **Distance.** With `D = A - P`, `E = B - A`:
   `d = cross(D, E) * 4096 / cross(R, E)` — computed by `fx_muldiv` (64-bit
   product, hardware divide when it fits, exact shift-subtract otherwise).
3. **Texture u.** Hit `H = P + R·d/4096`; distance along the wall from its
   dominant axis times a per-wall factor `ulen_q8 = |E| / max(|Ex|,|Ey|)`.
4. **Projection.** `scale = (FOCAL << 8) / d`; a height `z` maps to
   `y = 100 - ((z - eye) * scale >> 8)`.
5. **Draw** the sector's ceiling rows above its ceiling line and floor rows
   below its floor line, then either the solid wall (stop) or, through a
   portal, the *upper step* (if the neighbour's ceiling is lower) and the
   *lower step* (if its floor is higher). Narrow the window to the portal's
   opening, skip the mate wall, continue in the neighbour.

A closed door (the gate, the grate, the 2007 masonry) is a portal rendered as
a solid wall until its world flag is set.

### Walls

Vertical texture coordinate in Q16 texels (1 texel = 4 cm), anchored at world
height 0 so textures line up across steps:

```text
vstep = (d << 14) / FOCAL
v(y)  = -eye * 16384 + (y - 100) * vstep
```

### Floors and ceilings

A flat pixel's distance depends only on its row and the height difference:
`dist = hdiff * FOCAL / |y - 100|`, read from a 101-entry reciprocal table.
The world position is `P + R·dist/4096` (two multiplies), so floors are fully
textured. `G2007_TEXTURED_FLATS 0` replaces them with shaded fills.

## Lighting

Brightness `L ∈ [0,16]` combines ambient sector light, exponential fog and
the torch:

```text
L = (sector_light * fog[d >> 6] + band(x) * flash[d >> 6]) >> 4
```

`band(x)` is the torch beam: four discrete bands around the screen centre
(13/9/4/1, +3 with the battery boost). A texel of luminance `lum ∈ [0,15]`
becomes `ramp[material][(lum * L + bayer(x, y)) >> 4]` — one multiply and one
lookup per pixel, with a 2×2 ordered dither. With the torch off, ambient
light keeps silhouettes readable; with it on, the beam reveals texture and
clues and makes the player visible to LampMan.

Sprites are shaded per sprite by scaling each cube component of their
16-entry palette. Lamp glows (LampMan, other players' torches) are drawn
procedurally and ignore fog, so a distant lamp is often seen before its
bearer.

## Billboards

Items, wall marks, vehicles, remote players, LampMan, glows and dust are
transformed to camera space once per frame, sorted far to near and drawn
column by column into each strip, hidden where `depth >= zbuf[x]`. The
z-buffer holds the distance of the wall that closed each column; occlusion by
partial steps is ignored (sufficient for gameplay). At most 32 billboards are
drawn.

## Strips and blits

The view is rendered column-major into a 16×200 byte strip (3,200 bytes) and
blitted with `prg32_sprite_draw_indexed()` as an 8-bpp sprite: 20 blits per
frame. `RENDER_X_STEP 2` renders every other column and duplicates it.

## Colour

PRG32's ILI9341 driver keeps an 8-bit framebuffer whose default palette is a
6×6×6 cube (indices 16–231) plus named colours (0–7). Its indexed-sprite path
maps every descriptor colour to a cube index with `component * 5 / max`.
Consequently:

- all game art is authored as **system-cube indices**; indices 8–15 and
  232–255 are never used (`tools/validate_assets.py` enforces this);
- the descriptor palette uses the **smallest RGB565 component values** that
  quantise to each level (5-bit: 0, 7, 13, 19, 25, 31; 6-bit: 0, 13, 26, 38,
  51, 63), so hardware shows exactly the intended colour (a lesson recorded
  by Cockroaches_Cathisteria);
- material ramps (tuff, damp tuff, shadow, whitewash, stone, masonry,
  water, rust) are 16-step, hue-preserving selections from the cube made by
  `tools/build_assets.py`, which keeps channel order (tuff stays warm, water
  cool) and keeps lime and stone neutral.

A fully custom 256-colour palette (`prg32_palette_set`) is not used because
the firmware's indexed-sprite blit maps colours through the fixed cube; it
is a possible future upgrade if the firmware gains an identity blit for
custom palettes.

## Textures

Eight 32×32 4-bit luminance patterns (rough tuff with pick marks, cut tuff
blocks, whitewash with damp streaks, irregular masonry, rubble, paving slabs,
water ripples, rusty bars) combine with material ramps into the 13 texture
ids requested by the brief (`TUF_A`, `TUF_B`, `TUF_DARK`, `TUF_CUT`,
`WHITEWASH_A/B`, `MASONRY_A`, `MASONRY_2007_WALL`, `FLOOR_RUBBLE`,
`FLOOR_STONE`, `CEILING_TUF`, `WATER_DARK`, `METAL_RUST`). All are procedural;
licensed photographs can later replace a pattern through the same tool.
