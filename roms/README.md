# ROMs — was wohin gehört

Hier liegen bei der Entwicklung deine eigenen ROM-Dumps. Git ignoriert alles in diesem
Ordner außer dieser Datei. **Niemals mit `git add -f` hinzufügen.**

Erkannt werden die Dateien später am Inhalt (SHA1), nicht am Namen. Die Tabelle nennt die
Namen, unter denen sie im Umlauf sind.

## U-220

| Chip | Inhalt | Größe | SHA1 | MAME-Name | d-tech-Name |
|---|---|---|---|---|---|
| IC8 | Firmware v1.00 | 128 KB | `e1c6f1ee29cebe8663cb0c6989b8faebb3d841fd` | `u-220_roland_1-0-0.ic8` | `roland_u220_pgm_v1-00.bin` |
| IC8 | Firmware v1.01 | 128 KB | `d0b66c0ea0e3af284a1806226aed79d8da2f3dd4` | `u-220_roland_1-0-1.ic8` | `roland_u220_pgm_v1-01.bin` |
| IC8 | Firmware v1.02 | 128 KB | `9d72b1688a173505b5e1c5ddac01efa78ee76c7f` | `u-220_roland_1-0-2.ic8` | `roland_u220_pgm_v1-02_(15209245_LH530847).bin` |
| IC19 | Wave ROM A | 512 KB | `55d36199ea489969a729782afe4cbff5b1cede0e` | `roland-a_r15179892f_mb834000a-20_226-aa.bin` | `roland_t110_u110_u220_waverom0.bin` |
| IC20 | Wave ROM B | 512 KB | `8b8067fbc6fa7b2e8694a3785f123627ea458d1f` | `roland-b_r15179893f_mb834000a-20_227-aa.bin` | `roland_t110_u110_u220_waverom1.bin` |
| IC21 | Wave ROM C | 512 KB | `8dd9826d7d3e67b9a568eadeaaf7856a05f3e068` | `roland-c_r15179894f_mb834000a-20_228-aa.bin` | `roland_t110_u110_u220_waverom2.bin` |
| IC22 | Wave ROM D | 512 KB | `35b98a0fb5cf643fb7b761a74581486eee3bd8c3` | `roland-d_r15179895f_mb834000a-20_229-aa.bin` | `roland_t110_u110_u220_waverom3.bin` |
| IC23 | Wave ROM E = Karte SN-U110-08 | 512 KB | `71b6f97df6dd4e573db5743c10c77e86d67965a3` | `roland-e_r15179947_mb834000a-20_3a1-aa.bin` | `roland_u220_waverom4_(sn-u110-08).bin` |
| IC24 | Wave ROM F = Karte SN-U110-09 | 512 KB | `aea715a0a554c071a2e912894805a8d819802e1d` | `roland-f_r15179948_mb834000a-20_3a2-aa.bin` | `roland_u220_waverom5_(sn-u110-09).bin` |

Für den U-220 brauchst du **eine** Firmware (empfohlen v1.02) und **alle sechs** Wave-ROMs.
Optional: Dumps weiterer SN-U110-Karten für die beiden Kartenslots.

## U-110 (nur für Phase 0: den unveränderten emu110-Build testen)

| Chip | Inhalt | SHA1 | emu110-Name | d-tech-Name |
|---|---|---|---|---|
| IC9 | Firmware v2.03 | `ec0d76d325f27eddd7d8ac5530221137dbde6db1` | `U110v203.BIN` | `roland_u110_pgm_(15179960).bin` |
| — | Wave ROMs A–D | wie oben | `roland_t110_u110_u220_waverom0..3.bin` | gleich |

## Wohin

| Zweck | Ort | Namen |
|---|---|---|
| Entwicklung (Plugin, Tools) | dieser Ordner `roms/` | d-tech- oder emu110-Namen |
| Voltaire 110 / Ostrich, installiert unter Windows | `%LOCALAPPDATA%\Voltaire110\roms\` (später `…\Ostrich\roms\`) | wie oben |
| MAME (Treiberentwicklung, Debugger) | `mame/roms/u220/` oder ein `-rompath` deiner Wahl | **MAME-Namen** aus der Tabelle |

## Nicht benötigt

- `*_waves.wav`: von Dritten entpackte Samples, nur als Referenz
- `demo_songs/*.raw`: U-110-Demodaten aus den Wave-ROMs, auf dem U-220 nicht abspielbar
