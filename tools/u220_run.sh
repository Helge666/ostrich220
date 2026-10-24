#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Phase 1 loop on the build host: take the pushed MAME branch, build the U-220 driver,
# run the firmware headless for a while, and summarise what it did.
#
#   tools/u220_run.sh [SECONDS] [-- extra MAME options]
#
# Expects ROMs under ~/dev/mameroms/u220 with MAME's names (roms/README.md).  Runs with
# -video none -sound none: nothing is drawn and no audio device is opened.

set -euo pipefail

here="$(cd "$(dirname "$0")/.." && pwd)"
mame="$here/mame"
secs="${1:-10}"; shift || true
[ "${1:-}" = "--" ] && shift

git -C "$mame" merge -q --ff-only incoming 2>/dev/null || true

export PATH=/usr/lib/ccache:$PATH
unset DISPLAY WAYLAND_DISPLAY
srcs=src/mame/roland/roland_u20.cpp,src/mame/roland/roland_u110.cpp,src/mame/roland/roland_u220.cpp
if ! nice -n 19 make -C "$mame" SUBTARGET=u220 SOURCES="$srcs" NOWERROR=1 USE_QTDEBUG=0 \
		-j10 > "$HOME/dev/build_u220.log" 2>&1; then
	grep -E "error" "$HOME/dev/build_u220.log" | head -20
	exit 1
fi

run="$HOME/dev/run"
mkdir -p "$run"
cd "$run"
rm -f error.log
nice -n 19 "$mame/u220" u220 -rompath "$HOME/dev/mameroms" -video none -sound none \
	-nothrottle -seconds_to_run "$secs" -log -skip_gameinfo \
	-nvram_directory "$run/nvram" -cfg_directory "$run/cfg" "$@" > run.out 2>&1 || {
		echo "u220 exited with $?"; tail -5 run.out; }

echo "== LCD"
grep '^LCD' error.log | head -40 || true
echo "== bank register writes: $(grep -c '^B ' error.log || true) distinct"
echo "== log lines: $(wc -l < error.log)"
