#!/usr/bin/env bash
# Measure the cartridge's own per-frame cost in fixed test scenes.
#
#   scripts/perf.sh            # table of instructions / modelled ms per frame
#
# Each scene is a wrapper that sets the G2007_TEST_* switches (src/config.h)
# and is built with the normal PRG32 builder. The cartridges run on the
# PRG32-QT core, whose "Accurate ESP32-C6" profile charges every guest
# instruction a class cost at 160 MHz (ALU 1, load 3, multiply 4, divide 16).
# That models the cartridge's compute only: the firmware blit and the SPI
# transfer of the dirty area are estimated separately from the pixels sent
# (see docs/performance.md). It is a deterministic, uncalibrated proxy, good
# for comparing changes; real numbers need a board.
#
# Environment: PRG32_REPO (default ../PRG32), PRG32_QT_REPO (default ../PRG32-QT)
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
prg32="$(cd "${PRG32_REPO:-$repo/../PRG32}" && pwd)"
qt="$(cd "${PRG32_QT_REPO:-$repo/../PRG32-QT}" && pwd)"
out="$repo/build/perf"
mkdir -p "$out"
if ! command -v riscv32-esp-elf-gcc >/dev/null; then
  tc="$(ls -d "$HOME"/.espressif/tools/riscv32-esp-elf/*/riscv32-esp-elf/bin 2>/dev/null | tail -1)"
  [[ -n "$tc" ]] && export PATH="$tc:$PATH"
fi
[[ -f "$repo/build/audio.block" ]] || python3 "$prg32/tools/prg32audio_pack.py" "$repo/audio/audio.json" --out "$repo/build/audio.block" >/dev/null

if [[ ! -f "$out/qt-core/libprg32qt_core.a" ]]; then
  cmake -S "$qt" -B "$out/qt-core" -G Ninja -DPRG32QT_BUILD_APP=OFF >/dev/null
  cmake --build "$out/qt-core" >/dev/null
fi
c++ -std=c++20 -O2 -I "$qt/src/core" "$repo/tools/qt-perf/perf.cpp" "$out/qt-core/libprg32qt_core.a" -o "$out/perf"

# Each scene is measured twice: "full" = a full-resolution frame rendered
# every frame (G2007_TEST_NO_SKIP), "moving" = turning on the spot, which is
# what the player sees while walking (dynamic half resolution).
# PERF_DEFINES="-DG2007_FLAT_Y_STEP=1 ..." overrides rendering knobs.
# name          x     y     angle flags
scenes=(
  "tunnel       200   1100  90    0"
  "cistern      1000  1700  60    0"
  "vehicle_hall 200   4300  100   WF_GATE_A_OPEN"
  "shelter      600   6200  160   WF_GATE_A_OPEN"
  "cavity       -500  6500  180   WF_GATE_A_OPEN|WF_WALL_OPEN"
  "stairs       -900  7000  90    WF_GATE_A_OPEN|WF_WALL_OPEN"
)
measure() {   # name noskip input -> "instr ms"
  local name="$1" x="$2" y="$3" angle="$4" flags="$5" noskip="$6" input="$7"
  local tag="$name-$noskip"
  {
    echo "#define G2007_TEST_SCENE 1"
    echo "#define G2007_TEST_NO_SKIP $noskip"
    echo "#define G2007_TEST_X ($x)"
    echo "#define G2007_TEST_Y ($y)"
    echo "#define G2007_TEST_ANGLE ($angle)"
    echo "#define G2007_TEST_FLAGS ($flags)"
    for d in ${PERF_DEFINES:-}; do d="${d#-D}"; echo "#define ${d%%=*} ${d#*=}"; done
    echo "#include \"$repo/src/galleria2007.c\""
  } > "$out/perf_$tag.c"
  (cd "$prg32" && PRG32_REPO="$prg32" python3 "$repo/tools/prg32_cli.py" cartridge build "$out/perf_$tag.c" \
    --portable --required-feature sprites --entry-prefix galleria2007 --name "perf" \
    --cart-ram-kib 64 --audio-block "$repo/build/audio.block" --out "$out/$tag.prg32") > "$out/$tag.log" 2>&1 \
    || { tail -3 "$out/$tag.log" >&2; exit 1; }
  local line
  line="$("$out/perf" "$out/$tag.prg32" 10 40 "0:$input")"
  echo "$(sed -E 's/instr\/frame=([0-9]+).*/\1/' <<< "$line") $(sed -E 's/.*\(([0-9.]+) ms modelled.*/\1/' <<< "$line")"
}
printf '%-14s %14s %10s %14s %10s\n' scene "full instr" "full ms" "moving instr" "moving ms"
sum_full=0; sum_move=0
for row in "${scenes[@]}"; do
  read -r name x y angle flags <<< "$row"
  read -r fi fm <<< "$(measure "$name" "$x" "$y" "$angle" "$flags" 1 0)"
  read -r mi mm <<< "$(measure "$name" "$x" "$y" "$angle" "$flags" 0 1)"
  printf '%-14s %14s %10.1f %14s %10.1f\n' "$name" "$fi" "$fm" "$mi" "$mm"
  sum_full="$(python3 -c "print($sum_full + $fm)")"; sum_move="$(python3 -c "print($sum_move + $mm)")"
done
printf '%-14s %14s %10.1f %14s %10.1f\n' average "" "$(python3 -c "print($sum_full / ${#scenes[@]})")" "" "$(python3 -c "print($sum_move / ${#scenes[@]})")"
