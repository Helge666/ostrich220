#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The whole U-220 restyle, start to finish (tools/restyle_panel.py says what and why).
#
#   tools/restyle_all.sh [--preview DIR]
#
# Needs Inkscape and the Barlow Semi Condensed face installed for fontconfig
# (resources/fonts/BarlowSemiCondensed -> ~/.local/share/fonts, then fc-cache).
# Not idempotent on its own -- `centre` moves labels by a measured offset -- so it
# always starts from the committed sources unless told otherwise with KEEP=1.

set -euo pipefail

here="$(cd "$(dirname "$0")/.." && pwd)"
gfx="$here/resources/graphics"
preview=""
[ "${1:-}" = "--preview" ] && preview="${2:?--preview needs a directory}"

if [ "$(fc-match -f '%{family}' 'Barlow Semi Condensed' | cut -d, -f1)" != "Barlow Semi Condensed" ]; then
	echo "restyle_all: Barlow Semi Condensed is not installed for fontconfig" >&2
	exit 1
fi

if [ "${KEEP:-0}" != "1" ]; then
	git -C "$here" checkout -- resources/graphics
fi

python3 "$here/tools/restyle_panel.py" style

q="$(mktemp -d)"
trap 'rm -rf "$q"' EXIT
for f in "$gfx"/overall_panel_inkscape.svg "$gfx"/dive_*.svg; do
	inkscape --query-all "$f" > "$q/$(basename "$f").query" 2>/dev/null
done
python3 "$here/tools/restyle_panel.py" centre "$q"

if [ -n "$preview" ]; then
	mkdir -p "$preview"
	inkscape "$gfx/overall_panel_inkscape.svg" --export-type=png \
		--export-filename="$preview/preview_panel.png" --export-width=1100 \
		--export-background='#000000' --export-background-opacity=1 2>/dev/null
	for f in "$gfx"/dive_*.svg; do
		n="$(basename "$f" .svg)"
		inkscape "$f" --export-type=png --export-filename="$preview/preview_$n.png" \
			--export-width=1100 --export-background='#1b1c1e' \
			--export-background-opacity=1 2>/dev/null
	done
	echo "restyle_all: previews in $preview"
fi
