# ROM images

Your own ROM dumps go here during development. Git ignores everything in this folder except
this file. **Never add them with `git add -f`.**

The plugin recognises the images by their SHA-1, not by their names; the names below are
the ones in circulation. You need **one** firmware (v1.02 recommended) and **all six** wave
ROMs.

| Chip | Contents | Size | SHA-1 | MAME name | Other name |
|---|---|---|---|---|---|
| IC8 | firmware v1.00 | 128 KB | `e1c6f1ee29cebe8663cb0c6989b8faebb3d841fd` | `u-220_roland_1-0-0.ic8` | `roland_u220_pgm_v1-00.bin` |
| IC8 | firmware v1.01 | 128 KB | `d0b66c0ea0e3af284a1806226aed79d8da2f3dd4` | `u-220_roland_1-0-1.ic8` | `roland_u220_pgm_v1-01.bin` |
| IC8 | firmware v1.02 | 128 KB | `9d72b1688a173505b5e1c5ddac01efa78ee76c7f` | `u-220_roland_1-0-2.ic8` | `roland_u220_pgm_v1-02_(15209245_LH530847).bin` |
| IC19 | wave ROM A | 512 KB | `55d36199ea489969a729782afe4cbff5b1cede0e` | `roland-a_r15179892f_mb834000a-20_226-aa.bin` | `roland_t110_u110_u220_waverom0.bin` |
| IC20 | wave ROM B | 512 KB | `8b8067fbc6fa7b2e8694a3785f123627ea458d1f` | `roland-b_r15179893f_mb834000a-20_227-aa.bin` | `roland_t110_u110_u220_waverom1.bin` |
| IC21 | wave ROM C | 512 KB | `8dd9826d7d3e67b9a568eadeaaf7856a05f3e068` | `roland-c_r15179894f_mb834000a-20_228-aa.bin` | `roland_t110_u110_u220_waverom2.bin` |
| IC22 | wave ROM D | 512 KB | `35b98a0fb5cf643fb7b761a74581486eee3bd8c3` | `roland-d_r15179895f_mb834000a-20_229-aa.bin` | `roland_t110_u110_u220_waverom3.bin` |
| IC23 | wave ROM E = card SN-U110-08 | 512 KB | `71b6f97df6dd4e573db5743c10c77e86d67965a3` | `roland-e_r15179947_mb834000a-20_3a1-aa.bin` | `roland_u220_waverom4_(sn-u110-08).bin` |
| IC24 | wave ROM F = card SN-U110-09 | 512 KB | `aea715a0a554c071a2e912894805a8d819802e1d` | `roland-f_r15179948_mb834000a-20_3a2-aa.bin` | `roland_u220_waverom5_(sn-u110-09).bin` |

The images may also be in zips, under any names (MAME's `u220.zip`, with wave ROMs A–D
perhaps in a `u110.zip` beside it): each entry is matched by the CRC32 the zip lists for it
and then checked by SHA-1.

Optional:

- a factory SysEx dump whose name starts with `U220Factory` (`.syx`), next to the ROMs or in
  a `sysex` folder next to the `roms` folder: a new plugin instance then starts with the
  factory data;
- dumps of SN-U110 cards for the two card slots, anywhere (the panel asks for the file).

## Where

| What | Where |
|---|---|
| the plugin on Windows | `C:\ProgramData\Ostrich 220\roms\` (also searched: `%LOCALAPPDATA%\…`, `Documents\…`, and the same under `Ostrich`) |
| the plugin on Linux | `~/.local/share/Ostrich 220/roms/` (or `$XDG_DATA_HOME/…`, `/usr/share/…`) |
| any platform, overriding | the folder named by the environment variable `OSTRICH_ROMS` |
| development tools (`u220_render`, tests) | this folder, `roms/` |
| MAME | a folder `u220` under your `-rompath`, with the **MAME names** |
