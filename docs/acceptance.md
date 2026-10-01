# Acceptance

Status of the implementation checklist (brief §41) for v0.1.0. **Do not
report "ready for Store" until every item marked PENDING passes and a human
approves the authenticated publication.**

## Build

- [x] Portable ABI-table build only, no legacy absolute imports (ABI hash `0x260f6136`)
- [x] Uses current PRG32 public headers (`main` a8669e5) and the 64 KiB profile
- [x] `cartridge summary` and `store inspect-metadata` pass (`build/*.summary.txt`, `*.metadata.txt`)
- [x] Position independent: no absolute/data relocations, identical images at two load addresses
- [x] Runs unchanged on PRG32-QT and PRG32-iOS cores and the QEMU firmware (`scripts/check_hosts.sh`)
- [x] Only `sprites` required; multiplayer/audio optional; palette and font host-independent

## Gameplay (host walkthrough test + QEMU smoke run)

- [x] D-pad movement/rotation, A interaction, B torch, A+B backpack
- [x] Five-slot backpack, full-backpack replacement/drop, dropped object persists
- [x] Historical registration, walled-passage puzzle, cavity reveal, 75-step ending
- [x] LampMan AI, capture and recovery; solo completion possible

## Renderer

- [x] 320×200 output, indexed-colour compatible, textured walls/flats, portals, height differences
- [x] Billboards, torch shading, distance darkness
- [x] Frame cost reduced: ≈7 ms of cartridge compute per moving frame on the 160 MHz model (was ≈31 ms), 20 % fewer pixels sent, nothing sent when nothing changes
- [ ] PENDING: frame rate on a physical ESP32-C6 (estimate ≈23 fps while moving, [performance.md](performance.md))

## Multiplayer

- [x] Multiplayer flag/API, stable signature `galleria2007-v1`, up to four players
- [x] Torch state, item take/drop, wall opening, duplicate-pickup conflict (host tests)
- [x] Offline QEMU works
- [ ] PENDING: physical multiplayer (report below)

## Audio

- [x] Original music only, adaptive state machine, environmental sounds, AUD0 packed
- [x] Stereo soundtrack (instrument pans + tracker `SET_PAN`) and positional stereo effects; mono-safe
- [ ] PENDING: stereo listening on two MAX98357A speakers (QEMU streams one channel)
- [ ] PENDING: no frame instability from audio on hardware

## History / outreach

- [x] 2007 discovery and 75-step access described as documented; fiction separated
- [x] No unlicensed photography shipped; provenance manifest complete
- [x] Outreach screen text is a replaceable placeholder

## Store

- [x] `esp32c6` and `qemu` variants per language; metadata; colophon; icon; real screenshot; 30 s QEMU preview
- [x] Bundles accepted by the CartridgeStore's own ingestion code (`READY for submission`); reproducible checksums; no credentials in the repository
- [ ] PENDING: human approval before authenticated publication

---

## Physical ESP32-C6 performance report (to fill in)

| Item | Result |
|---|---|
| Board / firmware commit / profile | |
| Display readability on the 2.8" panel | |
| Joystick/A/B, A+B chord | |
| Frame time: tunnel / vehicle hall / cavity (worst) | |
| Watchdog resets, memory stability over 20 min | |
| Audio stability (mono, stereo) | |

## Multiplayer acceptance report (to fill in)

| Test | Boards | Result |
|---|---:|---|
| Join with signature `galleria2007-v1` | 2 | |
| IT + EN editions in the same world | 2 | |
| Simultaneous pickup of one item: one winner, loser sees the message | 2 | |
| Torch visibility of a remote player | 2 | |
| Wall opening replicated | 2 | |
| Dropped item recovered by another player | 2 | |
| Four players present and moving | 4 | |
