# Performance

**Status:** cartridge compute is *measured on a model*, display transfer is
*estimated*; neither has been confirmed on a physical ESP32-C6 yet (see
[acceptance.md](acceptance.md)). QEMU speed says nothing about hardware.

## Where a frame goes on the ESP32-C6

| Part | Cost | Scales with |
|---|---|---|
| Cartridge compute (update + draw) | measured below | pixels rendered, sectors crossed |
| Firmware indexed blit | ≈ 0.07 µs per pixel + ≈ 0.05 ms per blit (estimate) | pixels blitted, blit calls |
| SPI transfer in `prg32_gfx_present` | ≈ 0.62 µs per pixel of the dirty bounding box (32 MHz SPI, figure measured by SpaceBeltMadness) | **area that changed** |

The transfer is the largest part and depends only on how many pixels the
cartridge touches, so the renderer is built to touch as few as possible.

## Measuring: `scripts/perf.sh`

```bash
scripts/perf.sh          # PRG32_REPO, PRG32_QT_REPO (defaults: sibling checkouts)
```

Each test scene is a wrapper that sets the `G2007_TEST_*` switches
(`src/config.h`) and is built with the normal PRG32 builder. The cartridges
run on the PRG32-QT core, whose *Accurate ESP32-C6* profile charges every
guest instruction a class cost at 160 MHz (ALU 1, load 3, store 2, taken
branch 3, multiply 4, divide 16). `tools/qt-perf/perf.cpp` reports
instructions and modelled milliseconds per frame. The model is deterministic
and uncalibrated: use it to compare changes, not as an absolute promise.

Two columns per scene: **full** = a full-resolution frame rendered every
frame (`G2007_TEST_NO_SKIP`), **moving** = turning on the spot, i.e. what the
player gets while walking (dynamic half resolution).

### Results (cartridge compute per frame, modelled ms at 160 MHz)

| Scene | Before (v0.1.0 initial) | Full-resolution frame | Moving frame |
|---|---:|---:|---:|
| tunnel | 30.8 | 11.7 | 6.8 |
| cistern | 21.3 | 9.6 | 6.1 |
| vehicle hall | 41.0 | 16.6 | 7.4 |
| shelter | 28.5 | 10.4 | 7.6 |
| cavity | 33.0 | 13.2 | 7.7 |
| stairs | 28.1 | 12.2 | 7.5 |
| **average** | **30.7** | **12.3** | **7.2** |

A frame in which nothing changes costs about 0.1 ms and sends nothing.

### Estimated frame time on the board

| | Before | After (moving) |
|---|---:|---:|
| Pixels sent per frame | 64,000 (320×200) | 51,200 (320×160 view) |
| SPI transfer | ≈ 40 ms | ≈ 32 ms |
| Firmware blit | ≈ 5.5 ms | ≈ 4.5 ms |
| Cartridge compute | ≈ 31 ms (21–41) | ≈ 7 ms |
| **Frame** | **≈ 76 ms ≈ 13 fps** | **≈ 44 ms ≈ 23 fps** |

Game logic runs at a fixed 30 Hz regardless of the frame rate, so the frame
rate changes smoothness, not game speed. The display transfer is now about
three quarters of the frame; further gains must come from sending fewer
pixels (see the knobs) or from the firmware (an identity fast path for
indexed strips, a faster SPI clock).

## What makes it fast

**Send fewer pixels**

- The 3D view is a 320×160 window (`G2007_VIEW_Y0`, `G2007_VIEW_H`); the HUD
  lives in a top and a bottom band that are blitted only when their content
  changes (a hash of the overlay ops touching them).
- A frame whose camera, sprites, door flags and overlay are all unchanged is
  neither rendered nor blitted; static full-screen pages are sent once.
- The UI is composed into the strips, so a frame is 20 blits and no other
  drawing calls.

**Render fewer pixels**

- Dynamic resolution (`G2007_DYNAMIC_RES`): while the camera moves each ray
  fills two columns; the first still frame is refined to full resolution.
  Columns are duplicated four pixels at a time with word operations.
- Floors and ceilings are shaded in row pairs (`G2007_FLAT_Y_STEP`), and rows
  beyond `G2007_FLAT_TEX_FAR`, where a texel is smaller than a pixel, use the
  pattern's mean luminance.

**Cheaper pixels**

- `g_shade[material][brightness][dither phase][luminance]` (4.6 KB, built at
  init) turns shading into one table lookup: no multiply, no second lookup.
- Light comes from two small tables (ambient × fog, torch × fall-off) indexed
  by distance bin.
- Wall spans fetch one texel and one table entry per pixel, two rows per
  iteration; magnified walls share one texel fetch per row pair.
- Sprites step their texture incrementally and fetch a texel only when the
  row changes; their palettes are shaded only after culling.

**Cheaper columns**

- The exit wall of each sector is remembered per portal depth: the next
  column tests that wall first (two cross products) instead of scanning the
  sector.
- The wall distance uses one hardware divide (`ray_distance`) instead of a
  64-bit shift-subtract division.

## Knobs (`src/config.h`)

| Knob | Effect |
|---|---|
| `G2007_VIEW_H` / `G2007_VIEW_Y0` | view height: every 16 rows fewer saves ≈ 3.2 ms of transfer and ≈ 10 % of rendering (Cockroaches_Cathisteria uses 136 rows) |
| `G2007_DYNAMIC_RES 0` | always full resolution (sharper while moving, ≈ +5 ms) |
| `RENDER_X_STEP 2` | always half resolution |
| `G2007_FLAT_Y_STEP 1` | every floor/ceiling row shaded on its own (≈ +2.5 ms at full resolution) |
| `G2007_FLAT_TEX_FAR` | distance beyond which floors are not textured |
| `G2007_TEXTURED_FLATS 0` | shaded floors/ceilings without texture (≈ −1.3 ms) |
| `G2007_FAR_CM`, `G2007_MAX_PORTALS` | view distance, portal depth per column |

## Cost of the optimisation

Executable RAM grew from 49.9 KB to 57.8 KB (shade and light tables, exit
cache, overlay hashes): 7.8 KB of the 64 KiB window remain. The stored
package is 54.9 KB of 64 KiB and the QEMU load image ≈ 45.1 KB of ≈ 54.5 KB.

## Hardware measurements to make

Use PRG32's metrics firmware (`CONFIG_PRG32_METRICS_ENABLE`) and record the
results in [acceptance.md](acceptance.md): frame time standing, walking and
turning in the tunnel, the vehicle hall and the cavity; then adjust
`G2007_VIEW_H` if the target is not met.
