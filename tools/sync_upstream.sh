#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Bring in what changed in emu110 since .upstream-base, WITHOUT its ROMs.
#
# Ostrich's history was started fresh because emu110 tracks Roland's ROMs (README.md,
# "Origin"), so a plain `git merge` from emu110 would drag them into ours. Instead this
# takes the upstream diff with the copyrighted paths cut out and applies it as a patch.
#
#   tools/sync_upstream.sh [--apply] [path-to-emu110-clone]   default: ../upstream/emu110
#
# Without --apply it only shows what changed upstream: the commits and the files the patch
# would touch. Nothing is synced blindly: review that first, and pull in only what Ostrich
# needs -- its core has gone its own way since.
#
# With --apply it applies with --3way and stages nothing it cannot apply cleanly; review,
# then commit with the new upstream SHA in the message. .upstream-base is updated for you.

set -euo pipefail

apply=0
up=""
for arg in "$@"; do
	case "$arg" in
		--apply) apply=1 ;;
		-*) echo "usage: $0 [--apply] [path-to-emu110-clone]" >&2; exit 2 ;;
		*) up="$arg" ;;
	esac
done

here="$(cd "$(dirname "$0")/.." && pwd)"
up="${up:-$here/../upstream/emu110}"
base="$(tr -d '[:space:]' < "$here/.upstream-base")"

git -C "$up" fetch --quiet origin
new="$(git -C "$up" rev-parse origin/main)"

if [ "$base" = "$new" ]; then
	echo "Already at upstream $new."
	exit 0
fi

echo "Upstream $base..$new:"
git -C "$up" log --format='%h %ad %s' --date=short "$base..$new"

# Keep in step with the COPYRIGHTED MATERIAL block of .gitignore.
upstream_diff() {
	git -C "$up" diff "$@" "$base" "$new" -- . \
		':(exclude)roms' \
		':(exclude)waves/*.wav' \
		':(exclude)reference/*.pdf' \
		':(exclude)reference/*_OM*' \
		':(exclude)*.jpg' \
		':(exclude)README.md'
}

echo
echo "Files the patch would touch (ROMs and other copyrighted paths left out):"
upstream_diff --stat

if [ "$apply" = 0 ]; then
	echo
	echo "Nothing applied. Read the changes (git -C \"$up\" log -p $base..$new), decide what"
	echo "Ostrich needs, then run again with --apply."
	exit 0
fi

upstream_diff --binary | git -C "$here" apply --3way --index

printf '%s\n' "$new" > "$here/.upstream-base"
git -C "$here" add .upstream-base

# The ROM rules are the point of this script; refuse to leave anything staged that breaks them.
if git -C "$here" diff --cached --name-only | git -C "$here" check-ignore --no-index --stdin; then
	echo "ERROR: the files above match the ROM rules in .gitignore -- unstage them." >&2
	exit 1
fi

echo "Applied. README.md changes upstream go to README-emu110.md by hand, if wanted."
echo "Submodule pointers (mame, plugin/dpf) are not touched; see BUILDING.md."
