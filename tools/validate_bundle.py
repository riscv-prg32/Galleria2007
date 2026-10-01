#!/usr/bin/env python3
"""Validate a Cartridge Store bundle exactly as the Store will ingest it.

Validation levels, strongest first:
  1. "store-ingest": runs CartridgeStore's own `_prepare_bundle()` (the code
     behind POST /api/publish/bundle) when the CartridgeStore repository is
     available (CARTRIDGE_STORE_REPO, default ../CartridgeStore) and its
     Python requirements are installed in this interpreter;
  2. "store-format": rebuilds each image with CartridgeStore's
     `prg32_format` module (no Flask needed);
  3. "structural": local checks only.
Every level also checks the rules from the Store's API documentation:
manifest `abi` == prg32-metadata-1.0, icon/splash are PNG or JPEG, both
`esp32c6` and `qemu` variants, unique architectures, rebuilt images within the
64 KiB package limit. Exit code 0 means the bundle is ready for submission.
"""
from __future__ import annotations

import json
import os
import sys
import zipfile
from pathlib import Path

LIMIT = 64 * 1024
ROOT = Path(__file__).resolve().parents[1]


def is_image(data: bytes) -> bool:
    return data.startswith(b"\x89PNG\r\n\x1a\n") or data.startswith(b"\xff\xd8\xff")


def main(bundle: Path) -> int:
    store_repo = Path(os.environ.get("CARTRIDGE_STORE_REPO", ROOT.parent / "CartridgeStore"))
    level, fmt, prepare = "structural", None, None
    if (store_repo / "cartridge_store/prg32_format.py").exists():
        sys.path.insert(0, str(store_repo))
        from cartridge_store import prg32_format as fmt  # type: ignore
        level = "store-format"
        try:
            from cartridge_store.app import _prepare_bundle as prepare  # type: ignore
            level = "store-ingest"
        except Exception as exc:  # Flask & co. not installed
            print(f"note: Store ingestion code unavailable ({exc.__class__.__name__}); using format module")
    errors = []
    with zipfile.ZipFile(bundle) as zf:
        manifest = json.loads(zf.read("manifest.json").decode("utf-8"))
        if manifest.get("abi") != "prg32-metadata-1.0":
            errors.append("manifest.abi must be prg32-metadata-1.0")
        for key in ("id", "title", "version", "summary"):
            if not manifest.get(key):
                errors.append(f"manifest.{key} is required")
        assets = manifest.get("assets", {})
        icon = zf.read(assets["icon"])
        splash = zf.read(assets["splash"]) if assets.get("splash") else None
        if not is_image(icon) or (splash is not None and not is_image(splash)):
            errors.append("icon/splash must be PNG or JPEG")
        archs = [a.get("id") for a in manifest.get("architectures", [])]
        if sorted(archs) != ["esp32c6", "qemu"]:
            errors.append(f"expected esp32c6 and qemu variants, found {archs}")
        if prepare is not None:
            try:
                prepared = prepare(zf)
            except Exception as exc:
                errors.append(f"Store rejected the bundle: {exc}")
                prepared = []
            images = [(p["architecture"], p["file"], p["image"]) for p in prepared]
        else:
            images = []
            for arch in manifest.get("architectures", []):
                data = zf.read(arch["file"])
                if fmt is not None:
                    meta = {k: v for k, v in manifest.items() if k not in ("assets", "architectures", "colophon")}
                    fmt.validate_metadata(meta)
                    data = fmt.build_cartridge(data, metadata=meta, icon=icon, screenshot=splash,
                                               colophon=manifest.get("colophon"), architecture=arch["id"])
                images.append((arch["id"], arch["file"], data))
        for arch, name, image in images:
            if fmt is not None:
                parsed = fmt.parse_cartridge(image)
                if parsed.metadata["id"] != manifest["id"] or parsed.metadata["runtime"]["architecture"] != arch:
                    errors.append(f"{name}: rebuilt metadata mismatch")
            fits = len(image) <= LIMIT
            if not fits:
                errors.append(f"{name}: {len(image)} bytes exceeds {LIMIT}")
            print(f"{arch:8s} {name:34s} {len(image):6d} bytes ({LIMIT - len(image):+d} free) "
                  f"{'OK' if fits else 'TOO LARGE'} [{level}]")
    for e in errors:
        print("ERROR", e)
    print(f"{bundle.name}: {'READY for submission' if not errors else 'NOT ready'} ({level})")
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main(Path(sys.argv[1])))
