# Testing

## Host tests (no ESP-IDF needed)

```bash
tests/run_tests.sh          # unit tests + gameplay tests for every language
tests/run_tests.sh shots    # also renders build/shots/<lang>/*.png
```

Everything compiles with `-std=c99 -Wall -Wextra -Werror` and runs under
AddressSanitizer and UndefinedBehaviorSanitizer. The PRG32 public headers come
from `$PRG32_REPO` (default `../PRG32`), so constants and structures match the
firmware.

| Test | Covers |
|---|---|
| `test_fixed` | sin/cos cardinal values, wrapping, Pythagoras, `fx_muldiv` fast/64-bit/saturating paths, `fx_udiv64` against the host, isqrt, projection boundaries |
| `test_world` | portal symmetry, sector containment, exit-wall query, walking down the access steps, wall radius, the gate (blocked, monotonic opening), cistern drop not walkable, grate passable only by the AI, item uniqueness, refused drops, hidden key |
| `test_inventory` | free slots, full backpack, swap, recovery by another player, refused swap keeps everything, critical-item flags, instance-pool conservation, reconcile |
| `test_ai` | every FSM transition, no stuck states, torch-dependent sight range, capture cooldown, walls block sight, chase and capture through portals, SEARCH → RETURN → PATROL |
| `test_protocol` | bit-exact packing, monotonic world flags, malformed records, duplicate pickup convergence in both orders, stale records after a drop, version wrap, stale/duplicate frames |
| `test_input` | single A/B taps, A+B chord in either order, no torch toggle when opening the backpack, chord needs release, hold A |
| `test_game` (per language) | boot → title → intro → play; **complete solo playthrough** of the vertical slice using only player actions; both full-backpack swaps; torch/backpack chord; wall forcing; walking into the cavity and up the staircase; end, outro, info, replay; capture and checkpoint recovery with progress kept; multiplayer wall flag and lost simultaneous pickup |

`tests/host/host_prg32.h` implements the PRG32 calls the cartridge uses: the
indexed blit has the firmware's semantics and text uses the firmware font
(copied from the PRG32 checkout at test time), so host frames match QEMU.

## QEMU

```bash
scripts/qemu_preview.py --script smoke --out build/qemu-smoke
scripts/qemu_preview.py --script preview --video --out build/qemu-preview
python3 ../PRG32/tools/validate_cartridge_media.py assets/store/screenshot-it.png build/qemu-preview/preview.mp4
```

The runner stages the cartridge into a *copy* of the QEMU flash image, drives
the PRG32 UART keyboard (`w/a/s/d`, `j` = A, `k` = B, `jk` = A+B), dumps the
firmware framebuffer through QMP `pmemsave`, records the firmware PCM stream
and, with `--video`, encodes a 30-second MP4.

Last run (2026-10-01, PRG32 QEMU firmware built 2026-09-13, ABI hash
`0x260f6136`): the Italian cartridge loaded (`38820 bytes code, 45912 bytes memory,
2680 bytes audio`), the English one `38632 / 45724 / 2680`, title/intro/play, walking, torch toggle, A+B backpack and
turning all worked; the 30 s preview passed `validate_cartridge_media.py`.

Note: at PRG32 `main` 687251f the QEMU firmware build fails in
`prg32_store.c` (`esp_crt_bundle.h` not found) — an upstream issue unrelated
to this cartridge; the earlier firmware with the same ABI was used.

## Bundle validation

`scripts/pack-store-bundle.sh` rebuilds each bundled cartridge with the
CartridgeStore format module (`CARTRIDGE_STORE_REPO`, default
`../CartridgeStore`) and checks the 64 KiB limit; without that repository it
falls back to structural checks.
