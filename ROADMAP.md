# Roadmap

The approach is low-level emulation: the U-220's own firmware on an emulated CPU, the chips
modelled after the hardware. The firmware brings voice allocation, the parts, MIDI and the
menus with it. The one exception is the effect processor IC25, which is modelled from
measurements (analysis/U220-EFFECTS.md).

## Where it stands

Done and checked against a real U-220 (details in [analysis/](analysis/README-ANALYSIS.md)):

- **MAME driver** `roland_u220`: boots firmware v1.00–v1.02, menus, MIDI in/out/thru, SysEx,
  the PCM chip, IC25's mixer (pan, sends, DIRECT outputs), two card slots.
- **Core** `U220Core`: the driver's machine without MAME's framework, real-time safe,
  bit-identical to MAME (null test, ±1 LSB).
- **Sound**: all timbres and drums as on the unit (level, envelopes, pitch); measured
  analogue output stage and DAC images; reverb/delay and chorus fitted to measurements and
  confirmed by ear.
- **Plugin** (VST3, CLAP, standalone; Windows and Linux), tested in Cubase 15: panel with
  display and keys, factory data on first start, session state, MIX/DIRECT outputs, two
  card slots, SysEx files, MIDI out.

## Before the first public release (0.1)

- [ ] GitHub: the repository (private first), a fork of `eliggett/mame` with branch `u220`
      and `.gitmodules` pointing to it; check that both submodule pins are pushed
      (BUILDING.md §6). Pushing is done together with Helge, step by step.
- [ ] CI: build Linux and Windows on GitHub, no ROMs needed; release zips on a `v*` tag.
      Written (`.github/workflows/build.yml`), smoke test checked on the build host; not
      yet run on GitHub.
- [x] The plugin's home page (`getHomePage`): https://github.com/Helge666/ostrich220
- [ ] Release 0.1: Windows zip (VST3, CLAP, standalone) and Linux zip, no ROMs.
- [ ] Make the repository public.

Not before the release, and only after checking each project's policy on LLM-written
contributions (if unknown: offer and let them decide; if unwanted: note it here and skip):
the `roland_u220` driver and the device fixes as a pull request to MAME; shared
improvements to emu110. Contact with emu110's author goes through Helge, not the LLM.

## Open

| What | Notes |
|---|---|
| SysEx out of the VST3 in Cubase | the plugin hands the messages over; Cubase 15 does not record or forward them, with or without its SysEx filter. Next: does any MIDI from the plugin reach Cubase (a test build sending notes/CCs)? Check a CLAP host (Reaper, Bitwig). |
| SysEx into the VST3 | DPF's VST3 input leaves SysEx out (commented out); a DPF patch would allow importing from the DAW |
| Effect processor IC25 | understand its program (EPROM `0x1A000`, the values the CPU writes while idle) and emulate it; would help MAME's CM-32P, D-70, KR-55 too |
| Effects, details | Room1's stereo image (model −0.29, unit ~0); what the unit does with Cho parts while the chorus is off |
| Patch library | single patches and timbres instead of the whole memory |
| Editing in the display | click a field, change it with the wheel: translated into key presses, closed-loop on the display's cursor position |
| ROM setup | a dialog that names a missing image and lets the user pick the file |
| Card dumps | recognise known dumps by SHA-1; SN-MV30 cards untested |
| ROM Play | keep unsaved temp edits across the demo songs (the unit loses them too) |
| P-01 and Dir1/Dir2 | with the factory data, P-01's sounding part stayed on Rev after Asgn = Dir1/Dir2; with a unit's own memory it follows at once. Check on the unit. |
| Panel | the MIDI LED |
| MAME | a display (needs the LCD controller's character ROM, never dumped, or a drawn one) |
| Platforms | the Windows standalone has seen little use; no macOS build |

## Reference recordings

The real unit is the reference. Recordings stay out of the repository (`*.wav` is ignored);
analyses (text, CSV) and notes go into `listen/`. A real unit's battery-backed memory should
be saved as a bulk dump before its battery gives out.
