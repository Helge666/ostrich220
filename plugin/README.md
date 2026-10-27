# plugin/

The Ostrich 220 plugin and the emulation core it runs. Building: [../BUILDING.md](../BUILDING.md).

```
plugin/
  core/      BSD-3-Clause.  U220Core: the machine assembled from MAME's devices, with no
             MAME framework around it; u220_core.h is the interface.  The effect models
             (u220_reverb, u220_chorus, U220Fx) behind u220_effects.h.
  compat/    BSD-3-Clause.  The minimal emu.h that MAME's device sources compile against,
             unchanged.
  ostrich/   GPL-3.0-or-later.  The plugin (DPF): OstrichPlugin.cpp, OstrichUI.cpp, the
             DAC stage and the resampler from 32 kHz to the host's rate.
  patches/   the DPF patches (BUILDING.md lists them).
  tools/     build_u220_core.sh, u220_render, u220_reverb_run, ostrich_clap_render.
  dpf/       DPF, a submodule.
```

The licence boundary runs one way: the plugin may use the core; the core and the MAME
devices use nothing of the plugin, so they can go back to MAME.

## The core

- `U220Core` renders at 32 kHz, the unit's own rate: MIX OUT and the two DIRECT OUT pairs.
  `render()` is real-time safe (no allocation, no locks, no file access).
- Images: `loadProgramRom`, `loadWaveRom` (as dumped; descrambled here), `loadCard` /
  `prepareCard` + `installCard` / `ejectCard` for the card slots.
- MIDI in with sample offsets (delivered at 31250 baud), MIDI out with sample offsets, panel
  keys (`setButton`), a snapshot of the display, the 32 KB battery RAM as state.

## The plugin

- Six outputs in three groups (MIX, DIRECT 1, DIRECT 2). A DIRECT pair counts as plugged in
  when the host gives it buffers of its own; otherwise its sound joins MIX.
- Parameters: Volume, the twelve panel keys (automatable), HW-EQ, "Direct outs".
- State saved with the session: the battery RAM (`nvram`), the panel size (`uisize`), the
  card files' paths (`card1`, `card2`).
- Panel keys, from a click, the computer keyboard or the wheel over the display, queue up
  in the DSP and are played one press at a time; the next waits until the display has
  stood still for 120 ms (at most 600 ms), since the firmware reads no keys while it
  changes a patch (~0.4 s).
- The panel talks to the DSP by state messages: keys (`key`), card slots, SysEx files
  (`sysex`), a picked ROM file (`romfile`); the DSP sends the display (`panel`), the status
  line (`status`), the card slots' contents (`cardinfo`) and which ROM images it has
  (`romset`).
- ROM setup: while images are missing the panel shows them in place of the display. A
  picked file's folder is searched by SHA-1 on a thread of its own, loose files first,
  then zips (`ZipReader.hpp`: a zip's directory lists each entry's CRC32, so only an entry
  that matches a known image's is unpacked). What the ROM folders lack is written to the
  first install folder that takes it (ProgramData on Windows) as plain files, and the
  machine boots without a reload. A session's memory that arrives meanwhile is held.
- SysEx files are made on a copy of the machine on a thread of its own: SAVE presses
  Data > Bulk > All on the copy and writes its MIDI OUT; LOAD sends the file into the copy's
  MIDI IN and then moves the copy's memory into the machine.
- A new instance with no session behind it boots like a unit from the factory: Memory
  Initialize, then the factory dump if one is next to the ROMs, then P-01, all in emulated
  time before the first block.
