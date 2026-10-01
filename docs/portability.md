# Portability: one cartridge, every PRG32 host

The same `.prg32` file — unchanged, byte for byte — runs on:

| Host | What executes the RV32 code | Verified |
|---|---|---|
| ESP32-C6 PRG32 firmware | the ESP32-C6 itself, from IRAM | pending hardware ([acceptance.md](acceptance.md)) |
| PRG32 QEMU firmware (ESP32-C3) | QEMU | yes — both editions load and play |
| PRG32-QT (desktop, Android, Raspberry Pi, TV) | its RV32IMAC interpreter | yes — headless core, all four cartridges |
| PRG32-iOS (iPhone, iPad) | its RV32IMAC interpreter | yes — unmodified `PRG32Core`, all four cartridges |

`scripts/check_hosts.sh` repeats every verifiable check (see
[reproduce.md](reproduce.md#9-run-on-every-host)). Both `-esp32c6` and `-qemu`
files contain the same executable image; they differ only in the
architecture recorded in the Store metadata trailer, so either runs on the
emulator hosts.

## 1. Position independence (relocatability)

The PRG2 package has no relocation records, and every host may load the
image at a different address. The cartridge is therefore fully
position-independent:

- built with the PRG32 portable flags (`-mcmodel=medany`,
  `-fno-jump-tables`, `-fno-tree-switch-conversion`, `-msmall-data-limit=0`);
- **no pointers in initialised data**: strings are offsets into one blob,
  sprite and strip descriptors are filled at run time, no function-pointer
  tables;
- all PRG32 calls go through the ABI table pointer the host passes in `a0`.

`tools/check_relocatable.py` (run by `scripts/build.sh`) proves it on every
build: the object contains only PC-relative relocations (`PCREL_HI20/LO12`,
branches, calls) and none in data sections, and the image linked at
`0x40800000` and at `0x3FC80000` is byte-identical.

## 2. Features: required vs optional

PRG32 hosts advertise services in `provided_features` and refuse cartridges
that *require* anything missing:

| Feature | ESP32-C6 / QEMU firmware | PRG32-QT | PRG32-iOS | Galleria 2007 |
|---|:-:|:-:|:-:|---|
| `sprites` | ✓ | ✓ | ✓ | **required** (the renderer blits indexed strips) |
| `audio` | ✓ | ✓ | ✓ | optional |
| `audio_plus` (stereo) | ✓ | ✓ | ✓ | optional |
| `multiplayer` | ✓ | ✓ | — | optional, gated at run time |

A previous build that *required* multiplayer was rejected by PRG32-iOS
(`Cartridge requires unavailable features 0x00000004`). Now
`src/multiplayer.c` calls `prg32_multiplayer_*` only when the host advertises
the feature (`g2007_host_features()` reads `__prg32_abi->provided_features`);
otherwise the game is a complete solo game. Stereo voices 6–7 are used only
when `prg32_audio_get_mode()` reports stereo.

The firmware never consults the header's multiplayer flag: any cartridge
that calls `prg32_multiplayer_join()` gets networking on the ESP32-C6.

## 3. Palette

Hosts start from different default palettes: the firmware uses a 6×6×6 cube
(indices 16–231) and the ILI9341 driver quantises colours with
`component * 5 / max`; PRG32-QT starts from an RGB332 palette; PRG32-iOS from
its own. At init the cartridge therefore installs the 224 entries it uses
(0–7 and 16–231) with `prg32_palette_set`, using the smallest RGB565
components that quantise back to each cube level. Every host then resolves
the strip colours and indices to exactly the same colours.

## 4. Text

PRG32-QT and PRG32-iOS implement `prg32_gfx_text8` with a reduced 5×7 font
(`A–Z`, `0–9` and a few signs); apostrophes, `<`, `>` and brackets become
`?`. The cartridge never calls the host's text or rectangle functions: the
UI is a list of overlay operations composed into the render strips with the
PRG32 firmware's 8×8 font embedded in the cartridge (`src/gen/font8.h`,
760 bytes). Text, panels and the HUD are pixel-identical on every host.

## 5. ABI calls used

`prg32_ticks_ms`, `prg32_input_read`, `prg32_random_number`,
`prg32_palette_set`, `prg32_sprite_draw_indexed`, `prg32_audio_note`,
`prg32_audio_note_off`, `prg32_audio_play_track`, `prg32_audio_stop_track`,
`prg32_audio_set_channel_pan`, `prg32_audio_get_mode` and the
`prg32_multiplayer_*` group (optional). All are part of ABI 1.6 (hash
`0x260f6136`) and implemented by every host listed above.

## 6. Timing

Game logic runs in fixed 33 ms ticks driven by `prg32_ticks_ms()`, so it
behaves the same at any frame rate: PRG32-QT's *Accurate*, *Optimal* and
*Unlimited* modes, the iOS interpreter and the board.
