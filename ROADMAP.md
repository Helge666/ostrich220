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
  card slots (known dumps recognised, zips too), SysEx files, MIDI out, MIDI MESSAGE lamp,
  computer keyboard and mouse wheel for the panel keys, a click on a display field selects
  it, guided ROM setup in the panel
  (loose files or zips). SN-MV30 cards untested.

## Before the first public release (0.1)

- [x] GitHub: [Helge666/ostrich220](https://github.com/Helge666/ostrich220),
      the MAME fork [Helge666/mame](https://github.com/Helge666/mame), branch `u220`; both
      submodule pins are pushed.
- [x] CI: builds Linux and Windows on GitHub without ROMs, smoke test, zips (~4 min);
      a `v*` tag publishes a release.
- [x] The plugin's home page (`getHomePage`): https://github.com/Helge666/ostrich220
- [x] Release 0.1.0 (26.10.2026): Windows zip (VST3, CLAP, standalone) and Linux zip, no ROMs.
- [x] Release 0.1.1 (27.10.2026): guided ROM setup in the panel; a new view in Cubase at
      125 % fills its window.
- [x] Release 0.1.2 (27.10.2026): ROMs and cards from zips, known card dumps recognised,
      MIDI MESSAGE lamp, computer keyboard and mouse wheel; Memory Initialize without a
      factory dump; files from the panel written once under VST3.
- [x] Release 0.1.3 (27.10.2026): card dumps with address lines A8–A15 reversed are put
      back in order; known cards show their name again; the card and SysEx dialogs start in
      the folder last picked from.
- [x] Release 0.1.4 (27.10.2026): file paths with umlauts under VST3 (DPF patch 0014);
      the display by mouse: a click selects a field, the arrows turn the page, the title
      goes up, a double click adds ENTER.
- [x] The repository is public (26.10.2026).

Not before the release, and only after checking each project's policy on LLM-written
contributions (if unknown: offer and let them decide; if unwanted: note it here and skip):
the `roland_u220` driver and the device fixes as a pull request to MAME; shared
improvements to emu110. Contact with emu110's author goes through Helge, not the LLM.

## Open

Priorities as agreed on 27.10.2026.

### Next

| What | Notes |
|---|---|
| Editing in the display | stage 1 (keyboard, wheel over the display) and stage 2 (a click on a field selects it: CURSOR, or PART/INST on the main page, until the firmware's selection stands on it; the arrows turn the page, the title goes up with EXIT, a double click adds ENTER) are in and tested in Cubase. Open: it sometimes snags while MIDI plays through the plugin (guesses: the firmware answers keys later under load and the loop gives up after 0.8 s, or MIDI changes the display and the loop takes it for a new page; reproduce on kk2 with a MIDI file playing). Clicks on the main page feel unexpected; Helge will describe what he expects. |

### Nice to have

| What | Notes |
|---|---|
| SysEx into the VST3 | DPF's VST3 input leaves SysEx out (commented out); a DPF patch would allow importing from the DAW |
| Patch library | single patches and timbres instead of the whole memory |
| SysEx out of the VST3 in Cubase | the plugin hands the messages over; Cubase 15 does not record or forward them, with or without its SysEx filter. Next: does any MIDI from the plugin reach Cubase (a test build sending notes/CCs)? Check a CLAP host (Reaper, Bitwig). |
| ROM Play | keep unsaved temp edits across the demo songs (the unit loses them too) |
| Platforms | the Windows standalone has seen little use; the Windows CLAP and the Linux builds are untested; no macOS build |

### Needs the real unit

| What | Notes |
|---|---|
| P-01 and Dir1/Dir2 | with the factory data, P-01's sounding part stayed on Rev after Asgn = Dir1/Dir2; with a unit's own memory it follows at once. Check on the unit. |
| Effects, details | Room1's stereo image (model −0.29, unit ~0); what the unit does with Cho parts while the chorus is off |

### Some day, or for another enthusiast

| What | Notes |
|---|---|
| Effect processor IC25 | understand its program (EPROM `0x1A000`, the values the CPU writes while idle) and emulate it; would help MAME's CM-32P, D-70, KR-55 too |
| MAME | a display (needs the LCD controller's character ROM, never dumped, or a drawn one) |

## Reference recordings

The real unit is the reference. Recordings stay out of the repository (`*.wav` is ignored);
analyses (text, CSV) and notes go into `listen/`. A real unit's battery-backed memory should
be saved as a bulk dump before its battery gives out.
