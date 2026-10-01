# Replicating Galleria 2007: build your own portable PRG32 cartridge

This guide explains how the cartridge is put together so that you can build
a similar one — another historical site, another map, another language, or a
different game on the same engine. It walks through the project in the order
you would write it. For rebuilding the published artefacts exactly, see
[reproduce.md](reproduce.md).

## 0. The rules every portable PRG32 cartridge must respect

The same `.prg32` file must run unchanged on the ESP32-C6 firmware, the QEMU
firmware, PRG32-QT and PRG32-iOS ([portability.md](portability.md)):

1. **One C source per build.** `prg32 cartridge build` compiles one file: make
   it include every module (a unity build, `src/galleria2007.c`).
2. **Position independent, no relocations.** Hosts copy the image to
   different addresses. Use the builder's flags (`-mcmodel=medany`, no jump
   tables) and never put pointers in initialised static data: store strings
   as one `char` blob plus `uint16_t` offsets, fill descriptors in code, avoid
   function-pointer tables. `tools/check_relocatable.py` proves it.
3. **Freestanding.** `-nostdlib` and no libgcc: no floats, no 64-bit division,
   provide `memset`/`memcpy` (with loop-pattern optimisation disabled).
4. **Only public ABI calls**, from `prg32.h`; never firmware internals.
5. **Require only what every host provides** (`sprites`), declare the rest
   optional and test `__prg32_abi->provided_features` at run time.
6. **Do not rely on host defaults** that differ: install the palette you use
   with `prg32_palette_set`, and draw text with your own font.
7. **Budgets:** ≤ 64 KiB executable RAM (code + data + bss), ≤ 64 KiB stored
   package, and ≈ 54.5 KB QEMU load image (header + code/data + audio).

## 1. Project skeleton

```text
src/            C modules + src/gen/ (generated, committed)
lang/           one JSON per language (it.json defines the keys)
assets/map/     map source (JSON) + generated report/preview
assets/source/  SOURCES.md rights manifest
assets/store/   icon, screenshots, preview video
audio/          generated AUD0 source
metadata/       metadata.<lang>.json, colophon.<lang>.json
tools/          deterministic generators and validators
tests/          host tests + host PRG32 implementation
scripts/        build.sh, pack-store-bundle.sh, qemu_preview.py, check_hosts.sh
```

Start from `src/config.h` (all limits) and `tools/g2007_ids.py` (all ids
shared by C and Python, emitted as `src/gen/ids.h`).

## 2. Cartridge anatomy

`src/galleria2007.c` defines the three entry points the builder finds by
prefix (`--entry-prefix galleria2007`):

```c
void galleria2007_init(void)   { game_init(); }
void galleria2007_update(void) { game_update(); }   /* fixed 33 ms ticks */
void galleria2007_draw(void)   { game_draw(); }
```

The builder's portable stubs store the ABI table pointer (passed in `a0`) in
`__prg32_abi` and route every `prg32_*` call through it. The firmware calls
update and draw every frame and presents after draw.

## 3. Fixed-point mathematics

`src/fixed.c`: Q12 directions, 10-bit angles, a 257-entry quarter-wave sine
table, `fx_muldiv()` (64-bit product, hardware divide when possible, exact
shift-subtract otherwise) and `fx_isqrt()`. Tests: `tests/test_fixed.c`.

## 4. The map

Author `assets/map/galleria2007.json`:

```json
{"id": "tunnel_a", "poly": [[0,1000],[400,1000],[400,1600],[400,2000],[400,2400],[0,2400]],
 "floor": 0, "ceil": 560, "light": 4, "wall": "TUF_A", "floor_tex": "FLOOR_STONE",
 "ceil_tex": "CEILING_TUF", "zone": "TUNNEL", "flags": [], "provenance": "PUBLIC"}
```

- polygons are **convex, counter-clockwise**, in centimetres (y north);
- two sectors connect where one contains the *reversed* edge of the other —
  split edges with collinear vertices where openings meet;
- `doors` turn a portal into a closed wall until a world flag opens it;
- `entities` place items, historical points, clues, spawn points, AI
  waypoints; every one needs a sector and a `provenance` label.

`python3 tools/build_map.py` validates winding, convexity, self-intersection,
edge lengths, portal pairing, floor < ceiling, coordinate range and entity
placement, then writes `src/gen/map_data.h`, `assets/map/map_report.md` and a
top-down `map_debug.png`. Check walkability with `tests/test_world.c`.

**For a new site:** replace the JSON (keep zone/entity semantics or extend
the id lists), regenerate, and update the walkthrough test.

## 5. The renderer

[renderer.md](renderer.md) derives every formula. In short: one ray per
screen column walks the sector graph through portals (exit wall found by sign
tests), draws ceiling/floor rows, upper/lower steps and the final wall into a
16×200 byte strip; billboards and the UI are composed into the same strip; the
strip is blitted with `prg32_sprite_draw_indexed`. Lighting is a 0–16
brightness from ambient light, exponential fog and the torch beam, applied
through 16-step colour ramps.

## 6. Graphics assets

`tools/build_assets.py`:

- **patterns**: 32×32 4-bit luminance textures (procedural; a licensed photo
  could be converted to the same format);
- **materials**: 16-step ramps chosen from the PRG32 system cube with hue
  constraints (`MATERIALS` in `tools/g2007_ids.py`);
- **sprites**: ASCII art with per-sprite palettes, 4 bits per pixel, world
  size and height above floor;
- **tables**: row reciprocals, fog and torch fall-off.

Colours must be cube indices 0–7 or 16–231 (`tools/validate_assets.py`).

## 7. Text and languages

UI text lives in `lang/<code>.json`; `tools/build_strings.py` checks key
parity, ASCII after transliteration (`È` → `E'`) and a 38-column limit, and
emits a blob + offsets per language. `scripts/build.sh` builds one cartridge
per language with a wrapper defining `G2007_LANG_<CODE>`. See
[localization.md](localization.md). Text is drawn by the renderer with the
embedded firmware font (`tools/build_font.py`), so it looks the same on every
host.

## 8. Game systems

| System | File | Key idea |
|---|---|---|
| input | `src/input.c` | chord state machine: A+B never toggles B |
| backpack | `src/inventory.c` | slots hold item *instances*; drops persist |
| world | `src/world.c` | monotonic progression flags; collision by sector + radius push |
| antagonist | `src/ai.c` | pure `ai_think()` transition table + portal-graph pathing |
| interaction | `src/game.c` | target in a cone, unoccluded (last z-buffer), priority |
| UI | `src/ui.c` | overlay ops composed by the renderer |
| audio | `src/audio.c`, `tools/build_audio.py` | one track per mood, bar-aligned switching, positional stereo SFX |
| multiplayer | `src/net_proto.c`, `src/multiplayer.c` | 29-bit records gossiped through the snapshot fields |

## 9. Audio

Compose in `tools/build_audio.py`: note lists per channel, pan moves, tempo.
Channel *N* plays instrument *N* in the tracker, so keep music on 0–3 and
effects on 4–7. Use procedural SID-like instruments (no PCM bytes) to save
space. Pack with PRG32's `tools/prg32audio_pack.py`. See [audio.md](audio.md).

## 10. Multiplayer

Read [multiplayer.md](multiplayer.md). Choose one signature per protocol
version, keep every merge rule order-independent (OR for monotonic flags,
last-writer-wins registers with deterministic tie-breaks), and make the game
complete without peers.

## 11. Tests

- pure modules: unit tests compiled as single translation units;
- the whole game: `tests/test_game.c` includes the cartridge with the host
  PRG32 implementation (`tests/host/host_prg32.h`) and plays the slice by
  pressing buttons;
- screenshots: `tests/host/shots.c`.

Run `tests/run_tests.sh shots` after every change.

## 12. Metadata, art and packaging

1. `metadata/metadata.<lang>.json` (`prg32-metadata-1.0`, a unique Store `id`
   per edition) and `metadata/colophon.<lang>.json` (`prg32-colophon-1.0`);
2. `tools/make_store_art.py` (64×64 icon, 320×200 screenshot, palettised);
3. `scripts/build.sh` → `dist/*.prg32`;
4. `scripts/pack-store-bundle.sh` → `dist/*-store.zip`, validated by the
   Store's own ingestion code;
5. `scripts/check_hosts.sh` → runs the cartridges on every host;
6. publish by hand: [store_publishing.md](store_publishing.md).

## 13. Checklist for a derived project

- [ ] new Store ids, titles, authors, license decision in `metadata/`
- [ ] new map JSON with honest provenance labels
- [ ] rights for every non-procedural asset recorded in `SOURCES.md`
- [ ] `lang/*.json` rewritten; historical facts paraphrased from sources
- [ ] tests updated (walkthrough), all passing
- [ ] `scripts/build.sh`, `scripts/pack-store-bundle.sh`, `scripts/check_hosts.sh` green
- [ ] hardware acceptance on an ESP32-C6
