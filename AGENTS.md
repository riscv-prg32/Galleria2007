# AGENTS.md

Guidance for automated coding agents and maintainers of Galleria 2007.

## Project intent

A PRG32 cartridge with three equal purposes: an atmospheric adventure, a
teaching example of constrained 2.5D rendering and embedded C on RV32IMC,
and cultural outreach for Naples' Galleria Borbonica. **Real history,
invented mystery**: never present fiction (LampMan, his traces, the puzzle,
the provisional map) as historical fact; see `docs/history.md`.

## Languages

- Documentation, comments, commit messages: **English**.
- Game UI: **Italian** is primary (`lang/it.json` defines the keys). Every
  other language is a separate edition (`lang/<code>.json`) with its own
  metadata/colophon; all editions share the multiplayer signature.
  See `docs/localization.md`.

## Rules that are easy to break

- **One source per build:** `src/galleria2007.c` includes every module;
  `scripts/build.sh` wraps it per language. Do not add compiler flags.
- **No pointers in initialised static data** (portable cartridges have no
  relocations): strings are a `char` blob + `uint16_t` offsets, sprite
  descriptors are filled in code, no function-pointer tables.
- **No libc, no floats, no 64-bit division** (`-nostdlib`, no libgcc). Use
  `fx_muldiv`. Avoid signed left shifts of negative values (`* 256`).
- **Colours are PRG32 system-cube indices** (0–7, 16–231). Never use 8–15 or
  232–255; `tools/validate_assets.py` enforces it. The palette is installed
  at init: never rely on a host's default palette.
- **Never draw with `prg32_gfx_*` directly.** UI goes through overlay ops
  (`render_ui_*`) composed into the strips with the embedded font; PRG32-QT
  and PRG32-iOS have a reduced text font.
- **Portability:** the same `.prg32` must run unchanged on the firmware,
  QEMU, PRG32-QT and PRG32-iOS. Require only `sprites`; gate optional
  services on `g2007_host_features()`; keep the image position independent
  (`tools/check_relocatable.py`).
- **Generated files** live in `src/gen/`, `audio/audio.json`,
  `assets/map/map_report.md`: edit the sources (`tools/g2007_ids.py`,
  `assets/map/galleria2007.json`, `lang/*.json`, `tools/build_*.py`), never
  the outputs.
- **Budgets** (checked by `scripts/build.sh`): executable RAM ≤ 65,536
  (`mem=`), stored package ≤ 65,536, QEMU load image (code + audio + header)
  ≤ ~54,500.
- **Audio:** music on voices 0–3 (tracker channel = instrument), effects on
  4–5 (+6–7 only in stereo). Stereo via instrument pans, tracker `SET_PAN`
  and `sfx_from()`; keep everything mono-safe. Original music only.
- **Assets:** no photographs or third-party art unless their rights are
  recorded in `assets/source/SOURCES.md` first.
- **Do not publish** to the Cartridge Store from scripts; publication is a
  human-approved, authenticated step. No credentials in the repository.

## Workflow

```bash
tests/run_tests.sh shots                 # must report 0 failures
python3 tools/make_store_art.py          # if visuals changed
scripts/build.sh && scripts/pack-store-bundle.sh
scripts/check_hosts.sh                   # PRG32-QT, PRG32-iOS, QEMU
```

Update `README.md` and the relevant `docs/*.md` whenever gameplay,
rendering, audio, protocol or budgets change.
