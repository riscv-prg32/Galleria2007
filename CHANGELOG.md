# Changelog

## 0.1.0 — 2026-10-01 (prototype, not yet published)

- Portable PRG32 cartridge (ABI table, 64 KiB profile), Italian and English
  editions for ESP32-C6 and QEMU, Store metadata, colophons, icon,
  screenshots, 30 s QEMU preview and validated bundles.
- Doom-like portal renderer: convex sectors, heights, textured walls and
  flats, billboards, torch beam, fog, system-cube colour ramps.
- Vertical slice of the 2007 discovery with Archive, five-slot backpack,
  persistent drops, the walled-passage puzzle and the 75-step ending.
- LampMan finite-state machine with sight, noise and portal-graph pathing.
- Up-to-four-player state-gossip protocol over the PRG32 multiplayer service.
- Original adaptive stereo soundtrack and positional stereo effects on SID-like
  procedural instruments.
- Host unit and gameplay tests (full solo walkthrough), QEMU runner.
- Portability: one unchanged `.prg32` runs on the ESP32-C6/QEMU firmware,
  PRG32-QT and PRG32-iOS — position independence proven on every build,
  only `sprites` required (multiplayer/audio optional, gated at run time),
  host-independent palette, UI composed with an embedded font.
- Frame rate: 320×160 view with HUD bands sent only when they change,
  unchanged frames not re-sent, dynamic half resolution while moving,
  pre-shaded lookup tables, cheaper walls/floors/sprites and sector
  traversal. Cartridge compute per moving frame ≈31 ms → ≈7 ms on the
  160 MHz model; ≈23 fps estimated on the board (was ≈13).
  `scripts/perf.sh` measures it.
- Store bundles in the format accepted by CartridgeStore (`prg32-metadata-1.0`
  manifest, `splash` screenshot), validated with its ingestion code and
  reproducible; reproduction, replication, portability and publishing guides.
