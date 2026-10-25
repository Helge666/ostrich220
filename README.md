# Ostrich — eine Emulation des Roland U-220

Ein nicht-kommerzielles Fan-Projekt: Der Roland U-220 (RS-PCM Sound Module, 1989) soll als
Standalone-Programm und als Plugin (CLAP/VST3) weiterleben, wenn die Hardware einmal versagt.

**Stand:** Der U-220 läuft emuliert in MAME und als Plugin „Ostrich“ (VST3/CLAP), mit
Hall/Delay und Chorus, die an einem echten Gerät gemessen und nachgebaut sind. Was wann
kommt, steht in [ROADMAP.md](ROADMAP.md).

## Bekannte Abweichungen vom Gerät

- **Chorus2 „Growl“:** Am U-220 klingt Chorus2 rauer, „growliger“ als in Ostrich (z. B. im
  Werkspatch P-02). Messbar ist, dass linker und rechter Kanal dort am Gerät viel weniger
  korreliert sind; Verzögerung, LFO, Pegel, Feedback und Klangfarbe der Chorus-Kopie stimmen.
  Vermutlich ein analoger Effekt des Geräts. Bleibt vorerst so.
- **Effekte allgemein:** Hall, Delay und Chorus sind nach Messungen eines Geräts angenähert,
  nicht aus dem Effektchip (IC25) emuliert. Einzelgeräte können durch Alterung abweichen.

## Herkunft

Ostrich basiert auf **[emu110 / Voltaire 110](https://github.com/eliggett/emu110)** von
Elliott H. Liggett, einem Emulator des Roland U-110. U-110 und U-220 verwenden dieselben
PCM-Chips (MB87419/MB87420) und dieselben Wave-ROMs A–D. Übernommen ist der Stand von
emu110-Commit `5990a16` (siehe [`.upstream-base`](.upstream-base)); die ursprüngliche
README liegt unter [README-emu110.md](README-emu110.md).

Die Historie wurde **neu begonnen** statt geforkt, weil emu110 die Roland-ROMs in seiner
Git-Historie trägt. Ostrich enthält keine einzige Roland-Datei, auch nicht in der
Vergangenheit. Updates von emu110 kommen per `tools/sync_upstream.sh` herein.

Dazu kommen zwei Submodule:

| Pfad | Quelle | Zweck |
|---|---|---|
| `mame/` | [eliggett/mame](https://github.com/eliggett/mame), Branch `u110`; lokal Branch `u220` | CPU, PCM-Chip, LCD, später unser `roland_u220.cpp` |
| `plugin/dpf/` | [DISTRHO/DPF](https://github.com/DISTRHO/DPF) | Plugin-Framework (CLAP, VST3, LV2) |

## ROMs

**Dieses Repository enthält keine ROMs und wird nie welche enthalten.** Du brauchst
Dumps deines eigenen Geräts. Welche Dateien es gibt, wie sie in den verschiedenen
Namensschemata heißen und wohin sie gehören, steht in [roms/README.md](roms/README.md).
Die [.gitignore](.gitignore) schließt alle bekannten ROM-, Wave- und Handbuch-Formate aus.

## Lizenzen

- `plugin/src`, `plugin/tools`, `tools/`: GPL-3.0-or-later (wie emu110)
- `plugin/core`, die MAME-Geräte und MAME-Treiber: BSD-3-Clause, damit Erkenntnisse zurück
  in MAME fließen können
- Siehe [LICENSE](LICENSE)

„Roland“, „U-220“ und „U-110“ sind Marken der Roland Corporation. Dieses Projekt steht in
keiner Verbindung zu Roland.
