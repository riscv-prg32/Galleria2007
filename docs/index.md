# Galleria 2007 documentation

All documentation is in English; the game UI is Italian (primary) with an
English edition.

## Reproduce, replicate, publish

| Document | For |
|---|---|
| [reproduce.md](reproduce.md) | rebuilding the exact released cartridges and bundles, step by step, with versions and expected outputs |
| [replicate.md](replicate.md) | building your own cartridge on this engine (new site, map, language or game) |
| [portability.md](portability.md) | why one unchanged `.prg32` runs on the ESP32-C6 and QEMU firmware, PRG32-QT and PRG32-iOS |
| [store_publishing.md](store_publishing.md) | bundle format, validation and the human-approved submission |

## Design and implementation

| Document | Contents |
|---|---|
| [architecture.md](architecture.md) | modules, data pipeline, frame/tick, states, memory map |
| [renderer.md](renderer.md) | portal raycaster maths, lighting, palette, UI composition |
| [gameplay.md](gameplay.md) | controls, backpack, puzzle, LampMan, narrative beats |
| [multiplayer.md](multiplayer.md) | state-gossip protocol and conflict rules |
| [audio.md](audio.md) | adaptive stereo soundtrack and positional effects |
| [localization.md](localization.md) | language editions and the string pipeline |
| [history.md](history.md) | historical anchor, provenance labels, guardrails |
| [performance.md](performance.md) | cost model and tuning knobs |

## Quality

| Document | Contents |
|---|---|
| [testing.md](testing.md) | host tests, host checks, QEMU, media and bundle validation |
| [acceptance.md](acceptance.md) | acceptance checklist and hardware/multiplayer report templates |
| [provisional.md](provisional.md) | content awaiting Galleria Borbonica validation |
| [../assets/source/SOURCES.md](../assets/source/SOURCES.md) | asset and rights manifest |
| [../assets/map/map_report.md](../assets/map/map_report.md) | generated map report with provenance |
