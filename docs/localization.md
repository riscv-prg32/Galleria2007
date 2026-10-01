# Localization and language editions

- **Internal and external documentation:** English.
- **Game UI:** Italian is the primary language.
- **Language editions:** each language is a separate cartridge build from the
  same code, with its own Store metadata, colophon and screenshot.

| Edition | Cartridges | Store id | Bundle |
|---|---|---|---|
| Italiano (primary) | `galleria2007-it-{esp32c6,qemu}.prg32` | `org.riscv-prg32.galleria2007` | `galleria2007-it-<ver>-store.zip` |
| English | `galleria2007-en-{esp32c6,qemu}.prg32` | `org.riscv-prg32.galleria2007.en` | `galleria2007-en-<ver>-store.zip` |

Store ids are drafts pending project-owner approval. All editions use the same
multiplayer signature (`galleria2007-v1`) and protocol, so mixed-language
groups share one world.

## Why one language per cartridge

Executable RAM holds code *and* data. Compiling only the selected language
keeps ~3.5 KB of text per edition instead of ~7 KB for two, leaving room for
more languages without touching the budget. `scripts/build.sh` generates a
two-line wrapper per edition (`#define G2007_LANG_EN 1` + `#include
"src/galleria2007.c"`) because the PRG32 builder takes a single source and no
compiler flags.

## String pipeline

`lang/it.json` is the reference: its key order defines the string ids
(`src/gen/string_ids.h`). `tools/build_strings.py`:

1. checks every language has exactly the reference keys;
2. transliterates accented letters the traditional all-caps Italian way
   (`È` → `E'`), because the PRG32 8×8 font is ASCII-only;
3. rejects non-ASCII characters, lines longer than 38 columns, and Archive
   bodies outside 2–5 lines;
4. writes `src/gen/strings_<lang>.h`: one `char` array plus `uint16_t`
   offsets (position-independent; portable cartridges cannot relocate
   pointer tables).

Code uses `str(S_KEY)`. Item names follow the item-id order and Archive
entries are `A<n>_TITLE`/`A<n>_BODY` pairs (checked at compile time).

## Adding a language

1. Copy `lang/en.json` to `lang/<code>.json` and translate every value
   (keep keys; keep lines ≤ 38 columns; ASCII or the accents listed in
   `tools/build_strings.py`).
2. Add `metadata/metadata.<code>.json` and `metadata/colophon.<code>.json`
   (a new Store `id`, `language`, translated texts).
3. Add `#elif defined(G2007_LANG_<CODE>)` in `src/galleria2007.c` and the
   default in `src/config.h`.
4. Run `tests/run_tests.sh shots` (it discovers `lang/*.json`),
   `tools/make_store_art.py`, `scripts/build.sh` and
   `scripts/pack-store-bundle.sh`.

Historical Archive texts must be translated from the Italian *facts*, not
embellished: see [history.md](history.md).
