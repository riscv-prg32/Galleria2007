# Performance

**Status: estimates only. Physical ESP32-C6 frame rate has not been measured
yet** (see [acceptance.md](acceptance.md)). QEMU speed says nothing about
hardware speed.

## Cost model per frame (full 320×200 view)

| Part | Estimate | Basis |
|---|---:|---|
| SPI transfer of the view | ~40 ms | 64,000 px × ~0.62 µs/px (figure measured by SpaceBeltMadness on the ILI9341 at 32 MHz) |
| Wall columns | ~4 ms | ~30k px × ~20 cycles at 160 MHz |
| Textured floors/ceilings | ~6 ms | ~30k px × ~30 cycles (row distance from a table, 3 multiplies) |
| Portal traversal | <1 ms | ~4 sectors × ~5 walls per column, sign tests, one muldiv per sector |
| Firmware indexed blit | ~5 ms | 20 strips; per-pixel cube mapping (the identity fast path is not available for the cube palette) |
| Sprites, UI, logic | ~2 ms | |
| **Total** | **~55 ms** | **≈ 18 fps** expected; SPI-bound |

The game logic runs at a fixed 30 Hz regardless of frame rate, so a lower
frame rate changes smoothness, not game speed.

## Knobs (`src/config.h`)

| Knob | Effect |
|---|---|
| `RENDER_X_STEP 2` | renders 160 columns and doubles them: roughly halves cartridge-side rendering cost (not the SPI cost) |
| `G2007_TEXTURED_FLATS 0` | shaded floors/ceilings instead of textured: removes ~6 ms |
| `G2007_FAR_CM` | shorter view distance: fewer portals and more black pixels |
| `G2007_STRIP_W` | wider strips: fewer blit calls, +200 bytes RAM per column |
| `G2007_MAX_PORTALS` | caps traversal depth per column |

The next optimisation, if hardware needs it, is to reduce the transferred
area (e.g. a 320×160 view with a static HUD band, as Cockroaches_Cathisteria
does with 320×136), since the SPI transfer dominates.

## Instrumentation

`G2007_DEBUG_STATS`, `G2007_DEBUG_MAP`, `G2007_DEBUG_AI`, `G2007_DEBUG_NET`
are reserved compile-time switches (all 0; never enable them in Store builds).
For hardware timing use PRG32's metrics firmware
(`CONFIG_PRG32_METRICS_ENABLE`) as described in the PRG32 performance guide.
