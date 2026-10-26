#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Bring in what changed in emu110 since .upstream-base, WITHOUT its ROMs.
#
# Ostrich's history was started fresh because emu110 tracks Roland's ROMs (README.md,
# "Origin"), so a plain `git merge` from emu110 would drag them into ours. Instead this
# takes the upstream diff with the copyrighted paths cut out and applies it as a patch.
#
#   tools/sync_upstream.sh [path-to-emu110-clone]     default: ../upstream/emu110
#
# It applies with --3way and stages nothing it cannot apply cleanly; review, then commit
# with the new upstream SHA in the message. .upstream-base is updated for you.

set -euo pipefail

here="$(cd "$(dirname "$0")/.." && pwd)"
up="${1:-$here/../upstream/emu110}"
base="$(tr -d '[:space:]' < "$here/.upstream-base")"

git -C "$up" fetch --quiet origin
new="$(git -C "$up" rev-parse origin/main)"

if [ "$base" = "$new" ]; then
	echo "Already at upstream $new."
	exit 0
fi

echo "Upstream $base..$new:"
git -C "$up" log --oneline "$base..$new"

# Keep in step with the COPYRIGHTED MATERIAL block of .gitignore.
git -C "$up" diff --binary "$base" "$new" -- . \
	':(exclude)roms' \
	':(exclude)waves/*.wav' \
	':(exclude)reference/*.pdf' \
	':(exclude)reference/*_OM*' \
	':(exclude)*.jpg' \
	':(exclude)README.md' \
	| git -C "$here" apply --3way --index

printf '%s\n' "$new" > "$here/.upstream-base"
git -C "$here" add .upstream-base

# The ROM rules are the point of this script; refuse to leave anything staged that breaks them.
if git -C "$here" diff --cached --name-only | git -C "$here" check-ignore --no-index --stdin; then
	echo "ERROR: the files above match the ROM rules in .gitignore -- unstage them." >&2
	exit 1
fi

echo "Applied. README.md changes upstream go to README-emu110.md by hand, if wanted."
echo "Submodule pointers (mame, plugin/dpf) are not touched; see BUILDING.md."
