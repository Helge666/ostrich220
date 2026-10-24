#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Compile MAME's U-220 device sources against plugin/compat/emu.h into libu220core.a,
# and build the u220_render tool.  build_core.sh does the same for the U-110 and explains
# the choices (no copies of MAME sources, mame/src/emu NOT on the include path, separate
# object directories per target); this is that script with the U-220's file list.
set -u
HERE="$(cd "$(dirname "$0")/../.." && pwd)"
MAME="$HERE/mame"

CXX="${CXX:-g++}"
AR="${AR:-ar}"
OUT="${U220_CORE_BUILD:-$HERE/plugin/build-u220}"
mkdir -p "$OUT"

MACHINE="$("$CXX" -dumpmachine)"
case "$MACHINE" in *mingw*|*windows*) CROSS_WINDOWS=1 ;; *) CROSS_WINDOWS=0 ;; esac

GEN="$OUT/generated"
INCS=(-I "$HERE/plugin/compat"
      -I "$MAME/src/devices"
      -I "$MAME/src/lib/util"
      -I "$MAME/src/osd"
      -I "$MAME/src/mame/roland"
      -I "$GEN"
      -I "$HERE/plugin/core")

SOURCES=(
  "$MAME/src/devices/sound/roland_lp.cpp"
  "$MAME/src/devices/cpu/mcs96/mcs96.cpp"
  "$MAME/src/devices/cpu/mcs96/i8x9x.cpp"
  "$MAME/src/devices/sound/flt_biquad.cpp"
  "$MAME/src/devices/sound/flt_rc.cpp"
  "$MAME/src/devices/cpu/mcs96/mcs96d.cpp"
  "$MAME/src/devices/cpu/mcs96/i8x9xd.cpp"
  "$HERE/plugin/compat/emu_shim.cpp"
  "$HERE/plugin/core/u220_core.cpp"
  "$MAME/src/lib/util/strformat.cpp"
  "$MAME/src/lib/util/disasmintf.cpp"
)

CXXFLAGS=(-std=c++20 -O2 -g -Wall -Wno-unused-variable -Wno-unused-but-set-variable
          -Wno-unused-function -fno-strict-aliasing)
if [ "$CROSS_WINDOWS" = 1 ]; then
  CXXFLAGS+=(-posix -mstackrealign -D__USE_MINGW_ANSI_STDIO=1)
else
  CXXFLAGS+=(-fPIC)
fi

MK="$MAME/src/devices/cpu/mcs96/mcs96make.py"
LST="$MAME/src/devices/cpu/mcs96/mcs96ops.lst"
mkdir -p "$GEN/cpu/mcs96"
for spec in "s mcs96" "s i8x9x" "d i8x9x"; do
  set -- $spec
  out="$GEN/cpu/mcs96/$2$([ "$1" = d ] && echo d).hxx"
  if [ ! -f "$out" ] || [ "$LST" -nt "$out" ]; then
    echo "generating $(basename "$out")"
    python3 "$MK" "$1" "$2" "$LST" "$out" || exit 1
  fi
done

OBJS=()
fail=0
for src in "${SOURCES[@]}"; do
  obj="$OUT/$(basename "${src%.cpp}").o"
  printf '  %-24s ' "$(basename "$src")"
  if [ -f "$obj" ] && [ "$obj" -nt "$src" ] && [ "$obj" -nt "$HERE/plugin/core/u220_core.h" ] \
     && [ "$obj" -nt "$HERE/plugin/compat/emu.h" ]; then
    echo "up to date"
    OBJS+=("$obj")
    continue
  fi
  if "$CXX" "${CXXFLAGS[@]}" "${INCS[@]}" -c "$src" -o "$obj" 2> "$obj.log"; then
    echo "ok"
    OBJS+=("$obj")
  else
    echo "FAILED  ($(grep -c 'error:' "$obj.log") errors, see $obj.log)"
    fail=1
  fi
done
[ $fail -ne 0 ] && exit 1

rm -f "$OUT/libu220core.a"
"$AR" rcs "$OUT/libu220core.a" "${OBJS[@]}" && echo "  archived                 $OUT/libu220core.a"

EXE=""
[ "$CROSS_WINDOWS" = 1 ] && EXE=".exe"
printf '  %-24s ' "u220_render"
if "$CXX" "${CXXFLAGS[@]}" "${INCS[@]}" "$HERE/plugin/tools/u220_render.cpp" "$OUT/libu220core.a" \
        -o "$OUT/u220_render$EXE" 2> "$OUT/u220_render.log"; then
  echo "built"
else
  echo "FAILED  (see $OUT/u220_render.log)"
  fail=1
fi
exit $fail
