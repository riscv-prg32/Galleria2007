#!/usr/bin/env python3
"""Validate a Cartridge Store bundle the way the Store will ingest it.

The Cartridge Store rebuilds each cartridge from the bundle (manifest
metadata + icon + screenshot + colophon) and serves that image to boards, so
the rebuilt image must still fit the 64 KiB package limit. When the
CartridgeStore repository is available (CARTRIDGE_STORE_REPO, default
../CartridgeStore) its own format module performs the rebuild; otherwise only
structural checks run. Adapted from Cockroaches_Cathisteria (MIT).
"""
from __future__ import annotations

import json
import os
import sys
import zipfile
from pathlib import Path

LIMIT = 64 * 1024
ROOT = Path(__file__).resolve().parents[1]


def main(bundle: Path) -> int:
    store_repo = Path(os.environ.get("CARTRIDGE_STORE_REPO", ROOT.parent / "CartridgeStore"))
    fmt = None
    if (store_repo / "cartridge_store/prg32_format.py").exists():
        sys.path.insert(0, str(store_repo))
        from cartridge_store import prg32_format as fmt  # type: ignore
    ok = True
    with zipfile.ZipFile(bundle) as zf:
        manifest = json.loads(zf.read("manifest.json"))
        icon = zf.read(manifest["assets"]["icon"])
        shot = zf.read(manifest["assets"].get("screenshot") or manifest["assets"]["splash"])
        metadata = {k: v for k, v in manifest.items() if k not in ("assets", "architectures", "colophon")}
        metadata["abi"] = "prg32-metadata-1.0"
        if {a["id"] for a in manifest["architectures"]} != {"esp32c6", "qemu"}:
            print("bundle must contain esp32c6 and qemu variants")
            ok = False
        for arch in manifest["architectures"]:
            data = zf.read(arch["file"])
            if fmt is None:
                size, note = len(data), "structural check only"
            else:
                fmt.validate_metadata(metadata)
                image = fmt.build_cartridge(data, metadata=metadata, icon=icon, screenshot=shot,
                                            colophon=manifest.get("colophon"), architecture=arch["id"])
                parsed = fmt.parse_cartridge(image)
                assert parsed.metadata["id"] == manifest["id"]
                assert parsed.metadata["runtime"]["architecture"] == arch["id"]
                size, note = len(image), "rebuilt by CartridgeStore format module"
            fits = size <= LIMIT
            ok &= fits
            print(f"{arch['id']:8s} {arch['file']:34s} {size:6d} bytes ({LIMIT - size:+d} free) "
                  f"{'OK' if fits else 'TOO LARGE'} [{note}]")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main(Path(sys.argv[1])))
