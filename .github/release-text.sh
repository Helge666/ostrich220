#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The install instructions, in one place: INSTALL.txt in each zip, and the release notes.
#
#   .github/release-text.sh windows|linux
#   .github/release-text.sh notes VERSION

set -euo pipefail

roms() {
	cat <<'EOF'
You need your own ROM images: one U-220 firmware (v1.02 recommended) and the six wave ROMs
A-F. They are recognised by their SHA-1, so the file names do not matter. The list, with
checksums: roms/README.md in the source repository. No ROMs are included, and none ever
will be.

The easy way: load the plugin. While images are missing, the panel shows them; click
CHOOSE FILE and pick any one of your ROM images. The others in the same folder are found
by their contents and everything is copied to the ROM folder below; then it starts.

Optional: a U-220 factory SysEx dump whose name starts with "U220Factory" (.syx) next to
the ROMs. A new instance then starts with the 64 factory patches.

The status line under the panel says which images were found, or which are missing and
where they were looked for.
EOF
}

windows() {
	cat <<'EOF'
Ostrich 220 for Windows (64-bit)

VST3: copy the folder Ostrich220.vst3 to
  C:\Program Files\Common Files\VST3\
CLAP: copy Ostrich220.clap to
  C:\Program Files\Common Files\CLAP\
Standalone: Ostrich220.exe, anywhere.

ROMs go in (the panel copies them there for you)
  C:\ProgramData\Ostrich 220\roms\

EOF
	roms
}

linux() {
	cat <<'EOF'
Ostrich 220 for Linux (x86-64)

VST3: copy the folder Ostrich220.vst3 to ~/.vst3/
CLAP: copy Ostrich220.clap to ~/.clap/
Standalone: Ostrich220 (JACK), anywhere.

ROMs go in (the panel copies them there for you)
  ~/.local/share/Ostrich 220/roms/

EOF
	roms
}

notes() {
	local version="$1"
	cat <<EOF
Ostrich 220 ${version}: an emulation of the Roland U-220 RS-PCM Sound Module, running its
original firmware.

| File | Platform | Contents |
|---|---|---|
| \`Ostrich220-Windows.zip\` | Windows x86-64 | VST3, CLAP, standalone |
| \`Ostrich220-Linux.zip\` | Linux x86-64 | VST3, CLAP, standalone (JACK) |

Each zip has an INSTALL.txt. You need your own ROM images; none are included.

The reverb, delay and chorus are a model fitted to measurements of a real U-220, not an
emulation of its effect chip. Known limitations: see the README.
EOF
}

case "${1:-}" in
	windows) windows ;;
	linux)   linux ;;
	notes)   notes "${2:?version}" ;;
	*) echo "usage: $0 windows|linux|notes VERSION" >&2; exit 2 ;;
esac
