# Ostrich — eine Emulation des Roland U-220

Ein nicht-kommerzielles Fan-Projekt: Der Roland U-220 (RS-PCM Sound Module, 1989) soll als
Standalone-Programm und als Plugin (CLAP/VST3) weiterleben, wenn die Hardware einmal versagt.

**Stand:** Der U-220 läuft emuliert in MAME und als Plugin „Ostrich“ (VST3/CLAP) und ist im
Prinzip schon nutzbar: Klangerzeugung und Bedienung sind die echte Firmware, Hall/Delay und
Chorus sind an einem echten Gerät gemessen und nachgebaut. Was wann kommt, steht in
[ROADMAP.md](ROADMAP.md).

## Die Effekte sind eine Annäherung

Hall, Delay und Chorus des U-220 entstehen in einem eigenen Effektchip (IC25, Toshiba
TC23SC140), dessen Innenleben niemand dokumentiert hat. Ostrich emuliert diesen Chip
**nicht**, sondern ersetzt ihn durch einen Nachbau, der an einem echten U-220 vermessen und
abgeglichen ist (Messfolgen, automatischer Abgleich, Hörvergleiche). Im Hörvergleich liegt
das sehr nah am Original – es bleibt aber eine **vorläufige Annäherung**, bis jemand den
Effektchip und sein Programm analysiert. Dann wird nur dieses eine Modul ausgetauscht; der
Rest von Ostrich bleibt, wie er ist.

Einzelgeräte können außerdem durch Alterung der analogen Bauteile voneinander abweichen.

## Bekannte Abweichungen und offene Punkte

- **Chorus2 „Growl“ (gelöst, 25.10.2026):** Chorus2 hat eine zweite, schneller modulierte
  Kopie, und bei „Chorus Out = Pre Rev“ kommt der rechte Kanal der Chorus-Parts ein Sample
  später. Beides ist jetzt nachgebaut; an einer Syn-Bass-Line nach Helges Urteil „nahezu
  ununterscheidbar“.
- **Panel friert ein (unter Beobachtung):** In Cubase hörte das Plugin-Fenster mehrmals auf,
  sich neu zu zeichnen, während der Klang weiterlief. Eine Absicherung ist eingebaut, und die
  Diagnose schreibt nach `%PROGRAMDATA%\Ostrich\ostrich-dsp.log` und `ostrich-ui.log`; seither
  ist es nicht wieder aufgetreten.

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
