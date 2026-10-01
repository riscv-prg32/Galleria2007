#!/usr/bin/env bash
# Run the built cartridges unchanged on every PRG32 host that can be driven
# headlessly from a workstation:
#
#   1. position independence (tools/check_relocatable.py)
#   2. PRG32-QT emulator core   (prg32qt-headless, Qt-free build)
#   3. PRG32-iOS emulator core  (tools/ios-headless on PRG32Core, macOS + Swift)
#   4. PRG32 QEMU firmware      (scripts/qemu_preview.py --script smoke)
#
# Environment (each host is skipped when its checkout/tool is missing):
#   PRG32_REPO      PRG32 checkout with build-qemu/ (default ../PRG32)
#   PRG32_QT_REPO   PRG32-QT checkout (default ../PRG32-QT)
#   PRG32_IOS_REPO  PRG32-iOS checkout (default ../PRG32-iOS)
# The physical ESP32-C6 is checked by hand (docs/acceptance.md).
set -uo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
prg32="${PRG32_REPO:-$repo/../PRG32}"
qt="${PRG32_QT_REPO:-$repo/../PRG32-QT}"
ios="${PRG32_IOS_REPO:-$repo/../PRG32-iOS}"
out="$repo/build/hosts"
mkdir -p "$out"
fail=0
# The RISC-V toolchain without the ESP-IDF Python venv (which lacks Pillow).
if ! command -v riscv32-esp-elf-gcc >/dev/null; then
  tc="$(ls -d "$HOME"/.espressif/tools/riscv32-esp-elf/*/riscv32-esp-elf/bin 2>/dev/null | tail -1)"
  [[ -n "$tc" ]] && export PATH="$tc:$PATH"
fi
carts=("$repo"/dist/galleria2007-*-esp32c6.prg32 "$repo"/dist/galleria2007-*-qemu.prg32)
# Title -> A, intro -> A, then walk forward (masks: A=16, UP=4).
script_frames=(30:16 31:0 90:16 91:0 120:4 220:0)

echo "== position independence"
for w in "$repo"/build/galleria2007_*_qemu.c; do
  PRG32_REPO="$prg32" python3 "$repo/tools/check_relocatable.py" "$w" > "$out/reloc.log" 2>&1 \
    && tail -1 "$out/reloc.log" || { echo "FAILED: $w"; tail -3 "$out/reloc.log"; fail=1; }
done

echo "== PRG32-QT"
if [[ -d "$qt" ]] && command -v cmake >/dev/null; then
  echo "PRG32-QT $(git -C "$qt" rev-parse --short HEAD 2>/dev/null) $(git -C "$qt" status --porcelain 2>/dev/null | grep -q . && echo '(with local changes)')"
  rm -rf "$out/qt-core"          # never reuse a cache configured for another checkout
  cmake -S "$qt" -B "$out/qt-core" -G Ninja -DPRG32QT_BUILD_APP=OFF >/dev/null && cmake --build "$out/qt-core" >/dev/null || fail=1
  qtargs=()
  for s in "${script_frames[@]}"; do qtargs+=(--input "$s"); done
  for f in $(seq 121 219); do qtargs+=(--input "$f:4"); done
  for c in "${carts[@]}"; do
    "$out/qt-core/prg32qt-headless" "$c" 300 --verify-media "${qtargs[@]}" \
      --dump-ppm "$out/qt-$(basename "$c" .prg32).ppm" | tr '\n' ' ' || fail=1
    echo
  done
else
  echo "skipped (no $qt)"
fi

echo "== PRG32-iOS"
if [[ -d "$ios" ]] && command -v swift >/dev/null; then
  echo "PRG32-iOS $(git -C "$ios" rev-parse --short HEAD 2>/dev/null)"
  (cd "$repo/tools/ios-headless" && PRG32_IOS_REPO="$(cd "$ios" && pwd)" swift build -c release >/dev/null) &&
  for c in "${carts[@]}"; do
    "$repo/tools/ios-headless/.build/release/ios-headless" "$c" 300 \
      "$out/ios-$(basename "$c" .prg32).ppm" "${script_frames[@]}" || fail=1
  done
else
  echo "skipped (no $ios or no swift)"
fi

echo "== PRG32 QEMU firmware"
if [[ -f "$prg32/build-qemu/qemu_flash.bin" ]] && python3 -c "import PIL" 2>/dev/null; then
  for c in "$repo"/dist/galleria2007-*-qemu.prg32; do
    PRG32_REPO="$prg32" python3 "$repo/scripts/qemu_preview.py" --script smoke --cart "$c" \
      --out "$out/qemu-$(basename "$c" .prg32)" > "$out/qemu-$(basename "$c" .prg32).log" 2>&1 || tail -3 "$out/qemu-$(basename "$c" .prg32).log"
    grep -a "loaded cartridge" "$out/qemu-$(basename "$c" .prg32)/console.log" || { echo "$c: not loaded"; fail=1; }
  done
else
  echo "skipped (needs $prg32/build-qemu and Pillow in python3; run without the ESP-IDF venv)"
fi
echo "== result: $([[ $fail == 0 ]] && echo PASS || echo FAIL)"
exit $fail
