# Third-party code

Ostrich 220 as a whole is distributed under the GNU General Public License, version 3 or
later ([LICENSE](LICENSE)). It contains, or is built from, the following.

| Component | Where | Licence | Copyright |
|---|---|---|---|
| emu110 / Voltaire 110 | the base of this project; `plugin/compat`, the resampler, the U-110 analyses and tools | GPL-3.0-or-later (plugin, tools); BSD-3-Clause (core, MAME devices) | Elliott H. Liggett |
| MAME | `mame/` (submodule): the MCS-96 CPU, the Roland LP PCM chip, the drivers | BSD-3-Clause for the devices and drivers built here; MAME as a whole GPL-2.0-or-later | the MAME contributors (Olivier Galibert, Valley Bell, Elliott H. Liggett and others) |
| DPF | `plugin/dpf/` (submodule) | ISC | Filipe Coelho |
| pugl | `plugin/dpf/dgl/src/pugl-upstream` | ISC | David Robillard |
| NanoVG | `plugin/dpf/dgl/src/nanovg` | zlib | Mikko Mononen |
| stb_truetype, stb_image | `plugin/dpf/dgl/src/nanovg` | public domain / MIT | Sean Barrett |
| DejaVu Sans | the panel's font, built into DGL | Bitstream Vera / DejaVu licence (`plugin/dpf/dgl/src/resources/LICENSE-DejaVuSans.ttf.txt`) | Bitstream, DejaVu authors |
| RtAudio | the Windows standalone's audio, through DPF | MIT-style (RtAudio licence) | Gary P. Scavone |

Our own files say which licence they are under in their first lines: the core
(`plugin/core`), the compatibility layer (`plugin/compat`) and the MAME driver and devices
are BSD-3-Clause, so that they can go back to MAME; the plugin and the tools are
GPL-3.0-or-later.

No Roland firmware, wave ROM, card dump, SysEx data or manual is part of this project.
"Roland", "U-220" and "U-110" are trademarks of Roland Corporation; this project is not
affiliated with Roland.
