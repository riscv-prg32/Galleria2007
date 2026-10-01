#!/usr/bin/env bash
# Pack one Cartridge Store bundle per language from the built cartridges.
#
#   scripts/build.sh && scripts/pack-store-bundle.sh
#
# Each bundle holds the esp32c6 and qemu variants of one language. The
# manifest is generated from metadata/metadata.<lang>.json and
# metadata/colophon.<lang>.json (single source of truth). Bundles are then
# validated with the CartridgeStore format module when that repository is
# available (CARTRIDGE_STORE_REPO, default ../CartridgeStore).
# Publishing is a separate, authenticated, human-controlled step.
set -euo pipefail
repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
prg32_repo="$(cd "${PRG32_REPO:-"$repo_dir/../PRG32"}" && pwd)"
version="$(python3 -c "import json;print(json.load(open('$repo_dir/metadata/metadata.it.json'))['version'])")"

for meta in "$repo_dir"/metadata/metadata.*.json; do
  lang="$(basename "$meta" .json | sed 's/metadata\.//')"
  name="galleria2007-$lang"
  stage="$repo_dir/dist/store-bundle-$lang"
  bundle="$repo_dir/dist/$name-$version-store.zip"
  rm -rf "$stage"
  mkdir -p "$stage"
  cp "$repo_dir/assets/store/icon.png" "$stage/icon.png"
  cp "$repo_dir/assets/store/screenshot-$lang.png" "$stage/screenshot.png"
  for arch in esp32c6 qemu; do
    cart="$repo_dir/dist/$name-$arch.prg32"
    [[ -f "$cart" ]] || { echo "missing $cart; run scripts/build.sh first" >&2; exit 1; }
    cp "$cart" "$stage/"
  done
  python3 - "$repo_dir" "$lang" "$stage/manifest.json" <<'PY'
import json, sys
from pathlib import Path
root, lang, out = Path(sys.argv[1]), sys.argv[2], Path(sys.argv[3])
manifest = json.loads((root / f"metadata/metadata.{lang}.json").read_text(encoding="utf-8"))
manifest.pop("runtime", None)            # the Store derives runtime per architecture
manifest["abi"] = "prg32-bundle-1.0"
manifest["colophon"] = json.loads((root / f"metadata/colophon.{lang}.json").read_text(encoding="utf-8"))
manifest["assets"] = {"icon": "icon.png", "screenshot": "screenshot.png", "splash": "screenshot.png"}
manifest["architectures"] = [
    {"id": "esp32c6", "file": f"galleria2007-{lang}-esp32c6.prg32"},
    {"id": "qemu", "file": f"galleria2007-{lang}-qemu.prg32"},
]
out.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
PY
  rm -f "$bundle"
  (cd "$prg32_repo" && python3 -m prg32 store pack-bundle --manifest "$stage/manifest.json" --out "$bundle") >/dev/null
  python3 "$repo_dir/tools/validate_bundle.py" "$bundle"
  echo "$bundle"
done
(cd "$repo_dir/dist" && shasum -a 256 galleria2007-*.prg32 galleria2007-*-store.zip > SHA256SUMS)
