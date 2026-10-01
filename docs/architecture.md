# Architecture

Galleria 2007 is a portable PRG32 cartridge written in freestanding C99 for
RV32IMC. It follows the conventions of the PRG32 game family
(Cockroaches_Cathisteria, SpaceBeltMadness, MoanaAndTheLemonApocalypse,
Outbun-napoli97): one unity-build source, generated data headers, host tests,
scripted QEMU previews and a validated Store bundle.

## Platform constraints (PRG32 `main`, October 2026)

| Constraint | Consequence in this project |
|---|---|
| The builder compiles **one** source file | `src/galleria2007.c` `#include`s every module (unity build) |
| Portable ABI-table cartridges, **no relocations** | no pointers in initialised static data: strings are one `char` array plus `uint16_t` offsets, sprite descriptors are filled in code |
| `-nostdlib`, no libgcc | integer/fixed-point maths only, no 64-bit division, own `memset`/`memcpy` |
| 64 KiB executable RAM (default profile) | code + data + bss = 45.9 KB |
| 64 KiB stored package | code + AUD0 + Store trailer = 51.7 KB |
| ~54.5 KB QEMU load image (header + code/data + audio) | ~41.6 KB |
| ILI9341 indexed framebuffer maps colours to a 6×6×6 cube | all art uses system-cube indices (see [renderer.md](renderer.md#colour)) |
| Multiplayer snapshot = `x, y, sprite, flags, input(7 bits)` | state-gossip protocol ([multiplayer.md](multiplayer.md)) |
| Tracker: channel N plays instrument N; 6 mono voices | music on voices 0–3, effects on 4–5 ([audio.md](audio.md)) |

## Source layout

```text
src/
  galleria2007.c   unity build + PRG32 entry points (galleria2007_init/update/draw)
  config.h         every engine limit and tuning constant
  fixed.[ch]       Q12 trig (quarter-wave table), 64-bit-safe muldiv, isqrt
  world.[ch]       map types, shared progression flags, item instances,
                   collision, exit-wall query, line of sight, portal BFS
  inventory.[ch]   five-slot backpack of item instances, swap, reconcile
  ai.[ch]          LampMan FSM (pure transition function) + movement
  net_proto.[ch]   multiplayer records: pack/unpack, merge rules (pure)
  multiplayer.c    glue to prg32_multiplayer_* (send rotation, peers)
  input.[ch]       edge detection and the A+B chord state machine
  renderer.[ch]    portal raycaster, flats, billboards, strip blits
  audio.[ch]       adaptive music state machine and sound effects
  ui.c             HUD, backpack/archive, read panel, title/end screens
  game.[ch]        game state machine, player, interaction, puzzle, triggers
  platform.h       the only include of prg32.h
  gen/             generated headers (never edit by hand)
lang/              it.json (primary), en.json — UI and Archive text
assets/map/        galleria2007.json (map source), map_report.md, map_debug.png
assets/source/     SOURCES.md (rights manifest)
assets/store/      icon, per-language screenshots, preview video
audio/audio.json   generated AUD0 source (tools/build_audio.py)
metadata/          metadata.<lang>.json, colophon.<lang>.json
tools/             deterministic generators and validators
tests/             host unit tests, gameplay tests, host PRG32 implementation
scripts/           build.sh, pack-store-bundle.sh, qemu_preview.py
```

Pure modules (`fixed`, `world`, `inventory`, `ai`, `net_proto`, `input`) have
no PRG32 dependency and are unit-tested on the host.

## Generated data

| Generator | Output | Input |
|---|---|---|
| `tools/build_ids.py` | `src/gen/ids.h` | `tools/g2007_ids.py` (single id registry) |
| `tools/build_map.py` | `src/gen/map_data.h`, `assets/map/map_report.md`, `map_debug.png` | `assets/map/galleria2007.json` |
| `tools/build_assets.py` | `src/gen/assets_data.h`, `src/gen/trig_table.h`, texture/sprite sheets | procedural code (no photos) |
| `tools/build_strings.py` | `src/gen/string_ids.h`, `src/gen/strings_<lang>.h` | `lang/*.json` |
| `tools/build_audio.py` | `audio/audio.json`, `src/gen/audio_ids.h` | note lists in the script |
| `tools/make_store_art.py` | `assets/store/icon.png`, `screenshot-<lang>.png` | sprite data + host render |

All generators are deterministic (fixed seeds); `scripts/build.sh` re-runs
them, and the results are committed so the C code builds without Python.

## Frame and tick

```text
galleria2007_update()                     galleria2007_draw()
  input_update()  (once per frame)          build SpriteRef list (items, marks,
  while accumulated >= 33 ms:                 vehicles, remote players, LampMan,
    tick(): state machine                     lamp glows, dust)
      GS_PLAY: player, target query,        render_frame(): 20 strips of 16x200
               interaction, wall forcing,     -> prg32_sprite_draw_indexed()
               triggers                     ui_draw(): HUD / panels with
      every state in a session:               prg32_gfx_text8 and rect_indexed
               LampMan, multiplayer,
               reconcile backpack, particles
      choose_music()
  audio_update()  (bar-aligned changes, ambience)
```

Logic runs in fixed 33 ms ticks (at most 4 per frame), so behaviour is the
same on the ESP32-C6, in QEMU and in host tests. Button edges belong to the
first tick of a frame.

## Game states

`GS_TITLE → GS_INTRO → GS_PLAY ⇄ {GS_INVENTORY, GS_SWAP, GS_READ, GS_CAUGHT}
→ GS_END → GS_OUTRO ⇄ {GS_INFO, archive}` — `GS_OUTRO` "RIGIOCA" starts a new
session (Archive knowledge is kept for the power cycle; there is no
persistent save in v0.1, as the brief allows).

The world keeps running while a panel is open (other players and LampMan do
not pause), but capture only happens in `GS_PLAY`.

## Memory map (Italian edition, measured)

From `riscv32-esp-elf-size`/`nm` on the cartridge object (`-Os`):

| Item | Bytes |
|---|---:|
| code (`.text`, all modules, plus ABI stubs at link) | 22,444 |
| read-only data (`.rodata`) | 13,772 |
| — texture patterns (8 × 32×32 × 4 bpp) | 4,096 |
| — Italian text table | 3,330 |
| — sprite pixels | 1,784 |
| — map walls / sectors / entities / vertices | 1,752 + 372 + 448 + 332 |
| — quarter-wave sine table | 514 |
| zero-initialised data (`.bss`) | 7,092 |
| — strip buffer (16 × 200) | 3,200 |
| — visible-sprite list | 1,408 |
| — z-buffer, RGB565 palette | 640 + 512 |
| — sprite references, game state, world, AI, input | ~1,330 |
| **mem (linked: code + data + bss)** | **45,912 / 65,536** |
| **package (with audio and Store trailer)** | **51,747 / 65,536** |
| **QEMU load image (header + code + audio)** | **~41,630 / ~54,500** |

The largest functions are `game_update` (the inlined tick and state
machine, 5.2 KB), `ui_draw` (2.9 KB) and `render_frame` (2.4 KB, the whole
renderer inlined).
