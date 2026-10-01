#!/usr/bin/env python3
"""Run the PRG32 CLI, tolerating checkouts that predate --cart-ram-kib.

Current PRG32 (main, 2026-10) defaults to a 64 KiB executable cartridge RAM
window and accepts ``--cart-ram-kib``. Older checkouts checked portable
builds against a 32 KiB fallback and reject the flag. In that case this
adapter removes the flag and raises only the host-side fallback, exactly
like the Cockroaches_Cathisteria and SpaceBeltMadness adapters. It never
changes firmware or package-format behaviour.

Usage: PRG32_REPO=/path/to/PRG32 python3 tools/prg32_cli.py <prg32 args>
"""
import importlib
import os
import sys
from pathlib import Path

repo = Path(os.environ.get("PRG32_REPO", Path(__file__).resolve().parents[2] / "PRG32")).resolve()
sys.path.insert(0, str(repo))

args = sys.argv[1:]
cli_source = (repo / "prg32/prg32.py").read_text(encoding="utf-8")
if "--cart-ram-kib" not in cli_source:
    ram = 64 * 1024
    if "--cart-ram-kib" in args:
        i = args.index("--cart-ram-kib")
        ram = int(args[i + 1]) * 1024
        del args[i:i + 2]
    from prg32.utilities import env_variables  # noqa: E402
    env_variables.FALLBACK_CART_RAM_SIZE = ram
    for mod in ("prg32.cartridge.build_cartridge", "prg32.utilities.runtime_handler"):
        importlib.import_module(mod).FALLBACK_CART_RAM_SIZE = ram

from prg32 import prg32  # noqa: E402

prg32.main(args)
