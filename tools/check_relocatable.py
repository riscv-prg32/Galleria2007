#!/usr/bin/env python3
"""Prove that a Galleria 2007 cartridge image is position independent.

Every PRG32 host may place the executable buffer at a different address
(ESP32-C6 IRAM, the QEMU firmware, PRG32-QT and PRG32-iOS guest memory), and
the PRG2 package carries no relocation records. A portable cartridge must
therefore run unchanged wherever it is copied. This tool:

  1. compiles the cartridge wrapper exactly like `prg32 cartridge build`
     (same flags, PRG32's own portable stubs and linker script);
  2. scans the object for absolute relocations (R_RISCV_32, R_RISCV_HI20,
     R_RISCV_LO12_*) and for any relocation inside data sections;
  3. links the image at two different load addresses and requires the two
     binaries to be byte-identical.

Usage: PRG32_REPO=../PRG32 python3 tools/check_relocatable.py build/galleria2007_it_qemu.c
"""
from __future__ import annotations

import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PRG32 = Path(os.environ.get("PRG32_REPO", ROOT.parent / "PRG32")).resolve()
sys.path.insert(0, str(PRG32))
from prg32.cartridge import build_cartridge as bc  # noqa: E402

TOOL = "riscv32-esp-elf-"
MARCH, MABI = "rv32imc_zicsr_zifencei", "ilp32"
ABSOLUTE = ("R_RISCV_32", "R_RISCV_HI20", "R_RISCV_LO12_I", "R_RISCV_LO12_S", "R_RISCV_64")
ADDRESSES = (0x40800000, 0x3FC80000)


def run(cmd):
    return subprocess.run(cmd, check=True, capture_output=True, text=True).stdout


def main(source: Path) -> int:
    with tempfile.TemporaryDirectory(prefix="g2007-reloc-") as tmp_s:
        tmp = Path(tmp_s)
        obj = tmp / "game.o"
        run([TOOL + "gcc", "-mcmodel=medany", "-msmall-data-limit=0", "-fno-jump-tables",
             "-fno-tree-switch-conversion", "-std=c99", "-ffreestanding", "-fno-builtin", "-Os",
             "-march=" + MARCH, "-mabi=" + MABI,
             "-I", str(PRG32 / "components/prg32/include"),
             "-I", str(PRG32 / "components/prg32_audio/include"), "-I", str(PRG32 / "main"),
             "-c", str(source), "-o", str(obj)])
        relocs = run([TOOL + "objdump", "-r", str(obj)])
        section, bad, counts = "", [], {}
        for line in relocs.splitlines():
            if line.startswith("RELOCATION RECORDS FOR ["):
                section = line.split("[", 1)[1].split("]", 1)[0]
                continue
            parts = line.split()
            if len(parts) >= 3 and parts[1].startswith("R_RISCV"):
                counts[parts[1]] = counts.get(parts[1], 0) + 1
                if parts[1] in ABSOLUTE or not section.startswith(".text"):
                    bad.append(f"{section}: {line.strip()}")
        print("relocations:", ", ".join(f"{k}={v}" for k, v in sorted(counts.items())))
        if bad:
            print("ERROR position-dependent relocations:\n  " + "\n  ".join(bad[:20]))
            return 1
        syms = bc.parse_nm(run([TOOL + "nm", "--defined-only", str(obj)]))
        init, update, draw = bc.detect_entries(syms, "galleria2007")
        stubs = tmp / "stubs.S"
        bc.write_portable_stubs(stubs, init, update, draw)
        run([TOOL + "gcc", "-mcmodel=medany", "-msmall-data-limit=0", "-march=" + MARCH,
             "-mabi=" + MABI, "-x", "assembler-with-cpp", "-c", str(stubs), "-o", str(tmp / "stubs.o")])
        images = []
        for addr in ADDRESSES:
            ld, elf, raw = tmp / f"cart{addr:x}.ld", tmp / f"g{addr:x}.elf", tmp / f"g{addr:x}.bin"
            bc.write_linker(ld, addr, "prg32_entry_init")
            run([TOOL + "gcc", "-nostdlib", "-march=" + MARCH, "-mabi=" + MABI, "-mcmodel=medany",
                 "-msmall-data-limit=0", "-Wl,--no-relax", "-Wl,-T," + str(ld), str(obj),
                 str(tmp / "stubs.o"), "-o", str(elf)])
            run([TOOL + "objcopy", "-O", "binary", str(elf), str(raw)])
            images.append(raw.read_bytes())
        same = images[0] == images[1]
        print(f"image linked at 0x{ADDRESSES[0]:08x} and 0x{ADDRESSES[1]:08x}: "
              f"{len(images[0])} bytes, {'IDENTICAL' if same else 'DIFFERENT'}")
        if not same:
            diff = sum(a != b for a, b in zip(*images))
            print(f"ERROR {diff} bytes depend on the load address")
            return 1
    print("position independent: runs unchanged at any load address")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(Path(sys.argv[1])))
