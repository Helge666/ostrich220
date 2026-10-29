# Ostrich 220

## About the project

An emulation of the Roland U-220 RS-PCM Sound Module (1989): a MAME driver and a DAW plugin
(VST3, CLAP, standalone) running the unit's original firmware. Based on emu110, the U-110
emulation by Elliott H. Liggett. README.md says what it does, BUILDING.md how to build it,
ROADMAP.md what is open, analysis/ what is known about the hardware.

## Rules

- **Never commit Roland data**: no firmware, wave ROM, card dump, SysEx dump, manual or
  service notes, not even briefly. Recordings of a real unit stay out of the repository too.
  `.gitignore` covers the known formats; check `git status` before every commit.
- Make commits; do not push to the public repository. Pushing is done together with the
  owner, step by step.
- Do not contact other developers or projects. Pull requests to MAME or emu110 only after
  checking their policy on LLM-written contributions, and only with the owner.
- Compile with at most `-j5` and `nice -n 19`; the build host stays responsive.
- On the build host change no audio or video settings and open no audio device (it streams
  its desktop); listening and DAW tests happen on the owner's Windows machine.
- Ask before drawing windows on the owner's desktop.
- Do not remove anything in `analysis/` or `listen/`; mark what is obsolete instead. A new
  folder in `listen/` gets a short `README.txt` (one or two sentences: what was rendered or
  recorded, how).
- Record what is learnt about the hardware in `analysis/U220-HARDWARE.md` or
  `analysis/U220-EFFECTS.md`, and keep `analysis/README-ANALYSIS.md` up to date.
- Before a listening test, measure the pitch over the whole note, release included, against
  the hardware.
- Do not offer to run commands as root; say what needs installing.

## Facts that are easy to get wrong

- The core runs at 32 kHz; the plugin resamples. The core and the MAME driver are
  BSD-3-Clause, the plugin and the tools GPL-3.0-or-later.
- DPF is our fork Helge666/DPF (branch `logan`, pinned to a tag); nothing is patched at
  build time and `plugin/dpf` stays clean. Changes to DPF are commits in the fork.
- On Windows, files written by tools inside a packaged (MSIX) app under `%LOCALAPPDATA%` are
  invisible to the DAW; use `C:\ProgramData`.
