# Ostrich — Roadmap zum U-220

Stand: 24.10.2026. Grundlage: der unveränderte emu110-Stand (`.upstream-base`), der
MAME-Skeleton-Treiber `roland_u20.cpp` und die U-220 Service Notes.

Der Ansatz ist derselbe wie bei emu110 und Munt: **Low-Level-Emulation.** Die echte
U-220-Firmware läuft auf einer emulierten CPU, und die Chips werden nachgebildet. So
kommen alle Eigenheiten der Firmware (Stimmenverteilung, Rhythm-Part, MIDI, Menüs)
automatisch mit, und wir müssen sie nicht nachprogrammieren.

---

## Was wir übernehmen und was fehlt

| Baustein | U-110 (emu110) | U-220 | Aufwand |
|---|---|---|---|
| CPU | 8097BH, 16-Bit-Bus | **P8098, 8-Bit-Bus** (wie CM-32P) | gering, Kern vorhanden |
| Firmware | 64 KB | **128 KB → Bank-Umschaltung** | mittel |
| Arbeits-RAM | IC10 | **HM62256, 32 KB** (IC10) | gering |
| I/O | Gate Array IC8 | **MB623157 (IC12)**: LCD, Tasten, LEDs? | mittel, unbekannt |
| Display | 16×2, MSM6222B | **24×2, DM047Z** mit Beleuchtung, Controller zu klären | gering–mittel |
| PCM-Chip | MB87419/MB87420, im Fork weit gebracht | **dieselben Chips** (IC17/IC18), Bus und Takt anders | gering–mittel |
| Wave-ROMs | A–D + 4 Kartenslots | **A–F + 2 Kartenslots**, Lage von E/F zu klären | gering |
| Stimmen | 31 | **30** | Firmware |
| Effekte | Chorus/Tremolo im PCM-Chip | **eigener Effekt-Chip TC23SC140AF-007 (IC25) + 2× DRAM M5M4464** | **hoch, nirgends emuliert** |
| DAC | PCM54HP | **PCM56P** | gering |
| Plugin | Voltaire 110 (DPF) | neues Panel, neuer Core | mittel |

Der Effekt-Chip ist das einzige echte Forschungsprojekt. Alles andere ist Portierungs- und
Detektivarbeit mit bekannten Werkzeugen.

---

## Phase 0 — Fundament

Ziel: Ein lokaler Build von Voltaire 110 spielt mit deinen U-110-ROMs. Das beweist,
dass die Toolchain funktioniert, bevor wir etwas ändern.

- [x] Repo mit frischer Historie, emu110 als Basis, keine ROMs (`.gitignore`, `roms/README.md`)
- [x] MAME-Fork als Submodul (shallow), lokaler Branch `u220`
- [x] `tools/sync_upstream.sh` für spätere emu110-Updates ohne ROMs
- [x] **Build-Host kellerkind2** (Ubuntu 24.04, per SSH), siehe „Arbeitsweise“ unten
- [x] Emulator-Kern gebaut (`make core`), Smoke-Test grün; U-110 bootet mit deiner
      Firmware v2.03 und rendert eine Testnote offline (`build/u110_render`)
- [x] Build-Pakete auf kellerkind2 installiert (45 neu, 0 Upgrades; Sunshine lief durch)
- [x] Voltaire 110 gebaut: Linux LV2/VST3/CLAP/JACK und Windows-CLAP (`make`, `make win`),
      je unter 30 s; `make selftest` offline grün (CLAP-UI-Test mangels Display übersprungen)
- [ ] Windows-CLAP hier in der DAW testen. Installiert für den Benutzer unter
      `%LOCALAPPDATA%\Programs\Common\CLAP\Voltaire110.clap`, ROMs unter
      `%LOCALAPPDATA%\Voltaire110\roms\`
- [ ] **Vollständiges MAME** aus `mame/` bauen, nur mit den Roland-Treibern
      (`make SUBTARGET=roland SOURCES=src/mame/roland/roland_u110.cpp,src/mame/roland/roland_u20.cpp`).
      Der MAME-Debugger ist das wichtigste Werkzeug für Phase 1.
- [ ] Kontakt zu Elliott Liggett (emu110) aufnehmen: Wir bauen auf seiner Arbeit auf und
      würden Verbesserungen an `roland_lp.cpp` und `i8x9x.cpp` zurückgeben.
- [ ] GitHub-Repo anlegen, **zunächst privat**. Für MAME einen eigenen Fork von
      `eliggett/mame` anlegen, den Branch `u220` dorthin pushen und die URL in
      `.gitmodules` umstellen.

**Meilenstein 0:** Voltaire 110 läuft als CLAP in deiner DAW.

### Arbeitsweise

| Was | Wo |
|---|---|
| Quellcode bearbeiten, committen | Windows: `C:\Users\helge\Apps\U-220 Ostrich\ostrich` |
| Bauen, Tests, Offline-Renders | kellerkind2: `~/dev/ostrich`, Renders in `~/dev/renders` |
| Anhören, Plugin in der DAW | **nur Windows**, bis der Windows-Build den ersten erkennbaren Ton hat |

- Übertragung übers LAN: `git push kk2 main` (Ostrich) und `git -C mame push kk2 u220` (MAME).
  Auf kellerkind2 steht `receive.denyCurrentBranch=updateInstead`, der Push aktualisiert
  also direkt die Arbeitskopie.
- **Auf kellerkind2 keine Audio- oder Video-Einstellungen ändern** (Sunshine/Artemis
  streamen von dort). Konkret: kein JACK-Standalone und nichts, das ein Audiogerät öffnet;
  kein `xvfb` (würde den X-Server aktualisieren), der CLAP-UI-Test wird daher übersprungen;
  Builds mit `nice -n 19`; vor jeder Paketinstallation `apt-get -s` und auf Upgrades oder
  Entfernungen prüfen.
- Pakete (am 24.10.2026 simuliert: 45 neu, keine Upgrades, keine Entfernungen, keine Dienste):

  ```bash
  sudo apt-get install --no-install-recommends build-essential lv2-dev libjack-jackd2-dev libgl1-mesa-dev libx11-dev libxext-dev libxcursor-dev libcairo2-dev libdbus-1-dev g++-mingw-w64-x86-64 libsdl2-dev libsdl2-ttf-dev libfontconfig-dev libpulse-dev qtbase5-dev qtbase5-dev-tools qtchooser qt5-qmake ccache
  ```

## Phase 1 — Die U-220-Firmware bootet

Ziel: Im MAME-Debugger startet die Firmware v1.02 und zeigt ihren Startbildschirm.
Der Testmodus meldet `U220 V1.02 90/02/16`.

Arbeitsort: neuer Treiber `mame/src/mame/roland/roland_u220.cpp`. Er übernimmt den
`u220`-Eintrag aus `roland_u20.cpp` und nimmt sich `roland_u110.cpp` (Aufbau) und
`roland_cm32p.cpp` (P8098, 8-Bit-Bus) zum Vorbild.

1. **Firmware disassemblieren.** Reset- und Interrupt-Vektoren lesen, die
   Bank-Umschaltung finden (welcher Port-Pin blendet welche 64-KB-Hälfte ein?) und die
   Adressen der Peripherie aus den Zugriffen ableiten. Werkzeug: MAMEs `unidasm`
   für MCS-96 oder Ghidra.
2. **Speicherkarte** aus Firmware-Zugriffen und dem Adressdecoder der Service Notes
   ableiten: ROM-Fenster, 32 KB RAM, PCM-Registerfenster, IC12, IC25.
3. **NVRAM:** Batterie-RAM sichern, damit eigene Patches einen Neustart überleben.
4. **LCD 24×2:** den Controller bestimmen (HD44780-kompatibel oder MSM6222B wie beim
   U-110?) und an IC12 anbinden.
5. **Bedienteil:** Tasten, Drehregler und LEDs über IC12 abbilden.
6. **MIDI IN/OUT** über die serielle Schnittstelle des P8098, wie beim U-110.
7. Prüfen, an welcher Interrupt-Leitung der PCM-Chip hängt. Das Skeleton sagt `HSI0`, der
   U-110 nutzt `EXTINT`. emu110 warnt ausdrücklich davor, das ungeprüft zu übernehmen.

**Meilenstein 1:** Bootscreen, Menüs bedienbar, MIDI wird angenommen, alles noch stumm.

## Phase 2 — Erster Ton (ohne Effekte)

1. Den `roland_lp`-Stand aus dem emu110-Fork anschließen: Hüllkurven, Chorus/Tremolo,
   Interpolation, 32 kHz. Für den 8-Bit-Bus gilt wie beim CM-32P `0x1400 + reg` statt
   `0x1400 + 2*reg`. Das ist zu verifizieren.
2. **Takt:** 32,768 MHz ÷ 1024 = 32 kHz. Gegen die Hardware messen; beim U-110 lag MAME
   genau an dieser Stelle +104 Cent daneben.
3. **Wave-ROM-Lage:** Wo liegen E und F im 4-MB-Adressraum? Laut MAME „TODO“ ist das
   geraten. Prüfbar über die Tone-Tabellen der Firmware und am Klang.
4. **Kartenslots:** zwei statt vier. Erkennung über Port-Pins wie beim U-110.
5. 30 Stimmen und Rhythm-Part prüfen; die Firmware erledigt das, wir prüfen nur das Ergebnis.
6. **A/B-Vergleich mit deinem echten U-220** (siehe „Referenzaufnahmen“).

**Meilenstein 2:** Alle 128 Tones und 64 Patches klingen tonhöhenrichtig, ohne Hall.

## Phase 3 — Effekte (das Forschungsprojekt)

IC25 (Roland 15239126, Toshiba TC23SC140AF-007) mit zwei M5M4464 (je 64K×4 DRAM, zusammen
64 KB, bei 32 kHz/16 Bit rund 1 s Verzögerungsspeicher). Derselbe Chip steckt im
CM-32P/CM-64, D-70, KR-55 und Rhodes 760. Eine Lösung hilft also auch MAME bei diesen
Geräten, und ein Austausch mit ValleyBell (MAME, CM-32P) lohnt sich.

1. **Was schreibt die Firmware in IC25?** Register-Trace pro Reverb- und Chorus-Typ und
   -Parameter. Daraus ergibt sich die Parameter-Abbildung.
2. **Klären, wer was macht:** Läuft der Chorus beim U-220 noch im PCM-Chip (wie beim U-110)
   oder schon in IC25?
3. **HLE zuerst:** Impulsantworten jedes Effekttyps am echten Gerät messen und einen
   algorithmischen Hall/Delay daran anpassen. Klingt ähnlich, ist aber nicht bitgenau.
4. **LLE später, falls möglich:** den Chip als programmierbaren DSP verstehen (Programm
   und Koeffizienten kommen vermutlich von der CPU). Das hängt vom Trace aus Schritt 1 ab.

**Meilenstein 3:** Alle Effekttypen vorhanden, im Blindvergleich nah an der Hardware.

## Phase 4 — Das Plugin

1. `plugin/core/u220_core.cpp/.h` analog zu `u110_core`: echtzeitfähig, ohne MAME-Framework,
   BSD-3-Clause, 32 kHz nativ. Das Resampling erledigt das Plugin.
2. **Eigenes Panel:** U-220-Bedienfeld mit 24×2-LCD. Die U-110-Grafik und alle Roland-Logos
   müssen raus, die Artwork ist neu zu zeichnen. Schrift: nur OFL-lizenzierte Fonts
   (emu110 hat hier ein Lizenzproblem mit `earth.ttf`, siehe `CLONING.md`).
3. **ROM-Suche:** Dateien per SHA1 erkennen und alle bekannten Namen akzeptieren
   (`roms/README.md`); verständliche Meldung, wenn etwas fehlt.
4. Formate: CLAP und VST3 unter Windows. Bei emu110 aktualisiert sich das VST3-Panel noch
   nicht, das müssen wir beheben. Standalone; macOS später.
5. Patch-Bibliothek, SysEx-Import und -Export (U-220-Bulk-Dumps).

**Meilenstein 4:** Ostrich 0.1 läuft in der DAW.

## Phase 5 — Veröffentlichung und Rückfluss

- [ ] Vor jedem öffentlichen Push: Prüfung, dass die Historie keine ROM-, Wave- oder
      Handbuch-Datei enthält (`git log --all --stat` gegen die `.gitignore`-Regeln).
- [ ] Repo öffentlich machen, Releases nur mit Binärdateien ohne ROMs. Die
      Selbsttests in CI dürfen keine ROMs brauchen; emu110 hat sie im Repo, wir nicht.
- [ ] Treiber `roland_u220.cpp` und Chip-Verbesserungen als PR an mamedev (BSD-3-Clause).
- [ ] Gemeinsame Verbesserungen als PR an emu110.

---

## Referenzaufnahmen mit dem echten U-220

Der echte U-220 ist die Messlatte. Was wir brauchen, in der Reihenfolge des Bedarfs:

1. **Batterie-RAM sichern** (Bulk-Dump per SysEx), bevor die Batterie es nimmt
2. Einzelne Noten pro Tone, ohne Effekte, gleiche MIDI-Datei für Hardware und Emulator
   (die Methode aus emu110 `listen/`)
3. Hüllkurven-Tests: lange Haltenoten, Release-Zeiten
4. **Impulsantworten** jedes Reverb- und Chorus-Typs bei verschiedenen Parametern (für Phase 3)
5. Testmodus-Ausgaben, falls er Wave-ROM- und Effekttests anbietet (Service Notes, Test 10
   „Effect Test“)

Aufnahmen kommen **nicht** ins Repo (`*.wav` ist ignoriert), sondern in einen Ordner
außerhalb, z. B. `../recordings/`. Ins Repo gehören nur die Auswertungen (CSV, Text).

---

## Offene Fragen und Risiken

| Frage / Risiko | Einschätzung | Umgang |
|---|---|---|
| Effekt-Chip lässt sich nicht entschlüsseln | mittel | HLE-Hall nach gemessenen Impulsantworten |
| IC12 (MB623157) ist undokumentiert | mittel | Verhalten aus Firmware-Zugriffen ableiten; Blockdiagramm der Service Notes |
| emu110 ändert sich schnell (seit Aug. 2026, ein Autor) | hoch | `sync_upstream.sh`, Kontakt zum Autor, Änderungen klein und rückführbar halten |
| U-220-Firmware braucht Timing, das die MCS-96-Emulation nicht trifft | gering | beim U-110 hat es mit kleinen CPU-Fixes funktioniert |
| Plugin-Panel des U-110 trägt Roland-Gestaltung | sicher | in Phase 4 durch eigene Grafik ersetzen |

## Quellen

- MAME: `src/mame/roland/roland_u20.cpp`, `roland_cm32p.cpp`, `src/devices/sound/roland_lp.cpp`
- emu110: `EMULATOR-PLAN.md`, `IMPLEMENTATION-PLAN.md`, `WILD_IDEAS.md`, `analysis/`
- U-220 Service Notes (lokal unter `../upstream/emu110/reference/`, nicht in diesem Repo)
