#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The licence texts the binaries' parts ask to travel with them (THIRD-PARTY.md), into a
# folder of a release zip.
#
#   .github/licenses.sh <folder>

set -euo pipefail
cd "$(dirname "$0")/.."
out="${1:?folder}"
mkdir -p "$out"

# The BSD-3-Clause sources built into the core: MAME's devices and libraries, the
# compatibility layer and the core itself.  Each file names its copyright holders.
sources=(
  mame/src/devices/sound/roland_lp.cpp
  mame/src/devices/cpu/mcs96/mcs96.cpp
  mame/src/devices/cpu/mcs96/i8x9x.cpp
  mame/src/devices/cpu/mcs96/mcs96d.cpp
  mame/src/devices/cpu/mcs96/i8x9xd.cpp
  mame/src/devices/sound/flt_biquad.cpp
  mame/src/devices/sound/flt_rc.cpp
  mame/src/lib/util/strformat.cpp
  mame/src/lib/util/disasmintf.cpp
  mame/src/mame/roland/roland_u110_data.h
  mame/src/mame/roland/roland_u220_ic25.h
  plugin/compat/emu_shim.cpp
  plugin/core/u220_core.cpp
  plugin/core/u220_reverb.cpp
  plugin/core/u220_chorus.cpp
)
{
  echo "Ostrich 220's emulation core is built from these files, each under the BSD 3-Clause"
  echo "licence below, copyright by the holders named:"
  echo
  for f in "${sources[@]}"; do
    tagged="$f"
    grep -q "license:BSD-3-Clause" "$tagged" || tagged="${f%.cpp}.h"   # the header carries it
    grep -q "license:BSD-3-Clause" "$tagged" || { echo "no BSD-3-Clause tag in $f" >&2; exit 1; }
    printf '  %s\n      Copyright (c) %s\n' "$f" "$(grep -m1 -o 'copyright-holders:.*' "$tagged" | cut -d: -f2-)"
  done
  echo
  sed '1d' mame/docs/legal/BSD-3-Clause
} > "$out/BSD-3-Clause-MAME-and-core.txt"

cp plugin/dpf/LICENSE "$out/DPF-ISC.txt"
cp plugin/dpf/dgl/src/pugl-upstream/COPYING "$out/pugl-ISC.txt"
cp plugin/dpf/dgl/src/nanovg/LICENSE.txt "$out/NanoVG-zlib.txt"
cp plugin/dpf/dgl/src/resources/LICENSE-DejaVuSans.ttf.txt "$out/DejaVuSans.txt"
cp plugin/dpf/distrho/src/jackbridge/rtaudio/LICENSE "$out/RtAudio.txt"
cp plugin/dpf/distrho/src/jackbridge/rtmidi/LICENSE "$out/RtMidi.txt"
