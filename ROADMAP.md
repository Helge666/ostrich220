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
- [x] Windows-VST3 für Cubase 15 (kann kein CLAP): `patches/0005` gibt DPFs VST3 den
      fehlenden Rückkanal DSP → Oberfläche (LCD), `make win` baut CLAP, VST3 und das
      Standalone-Programm (RtAudio statt JACK)
- [x] **VST3 in Cubase 15**: lädt, bootet, klingt, LCD aktualisiert sich (24.10.2026).
      Plugin unter `%LOCALAPPDATA%\Programs\Common\VST3\Voltaire110.vst3`, ROMs unter
      `C:\ProgramData\Voltaire110\roms\`
- [x] Selbsttest nennt Fehlercodes je Ordner und hat einen COPY-Knopf
- [x] Oberfläche vorab im U-220-Hardware-Look (`tools/restyle_all.sh`): Anthrazit-Platte
      statt Weltraumfoto, Barlow Semi Condensed (OFL, im Repo) statt der nicht
      weitergebbaren Earth-Mod, Beschriftungen zentriert, dunkle Tasten, Reiter weiß/amber.
      Das eigentliche U-220-Panel (24×2-LCD, zwei Kartenslots) bleibt Phase 4.
- [ ] Standalone `Voltaire110.exe` unter Windows testen (Audio- und MIDI-Geräteauswahl)
- [ ] `AGENTS.md` ist noch der Text von emu110 (U-110, dessen Rechner); für Ostrich anpassen
- [ ] **Vollständiges MAME** aus `mame/` bauen, nur mit den Roland-Treibern
      (`make SUBTARGET=roland SOURCES=src/mame/roland/roland_u110.cpp,src/mame/roland/roland_u20.cpp`).
      Der MAME-Debugger ist das wichtigste Werkzeug für Phase 1.
- [ ] Kontakt zu Elliott Liggett (emu110) aufnehmen: Wir bauen auf seiner Arbeit auf und
      würden Verbesserungen an `roland_lp.cpp` und `i8x9x.cpp` zurückgeben.
- [ ] GitHub-Repo anlegen, **zunächst privat**. Für MAME einen eigenen Fork von
      `eliggett/mame` anlegen, den Branch `u220` dorthin pushen und die URL in
      `.gitmodules` umstellen.

**Meilenstein 0:** Voltaire 110 läuft als VST3 in Cubase 15. **Erreicht am 24.10.2026.**

### Arbeitsweise

| Was | Wo |
|---|---|
| Quellcode bearbeiten, committen | Windows: `C:\Users\helge\Apps\U-220 Ostrich\ostrich` |
| Bauen, Tests, Offline-Renders | kellerkind2: `~/dev/ostrich`, Renders in `~/dev/renders` |
| Anhören, Plugin in der DAW | **nur Windows**, bis der Windows-Build den ersten erkennbaren Ton hat |

- **Achtung, Windows:** Alles, was Claude unter `%LOCALAPPDATA%` neu anlegt, leitet Windows
  in den privaten Bereich der Claude-App um (MSIX-Paket). Cubase sieht es nicht. ROMs für
  Tests deshalb nach `C:\ProgramData\Voltaire110\roms`; Plugins nach
  `%LOCALAPPDATA%\Programs\Common\VST3` funktionieren, weil der Ordner schon existierte.
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

1. [x] **Firmware disassemblieren** (`unidasm -arch i8x9x`). Vektoren: HSI0 → `0x2130`
   (LCD-Warteschlange), Software-Timer → `0x218B`, Seriell → `0x224E` (MIDI),
   **EXTINT → `0x22DD` (PCM-Chip)**, Reset → `0x2100` → Init `0x25BC`, Hauptschleife `0x27E7`.
   Der Skeleton-Treiber lag mit `HSI0` für den PCM-Chip falsch: HSI0 ist das LCD.
2. [x] **Speicherkarte.** `0x2000`–`0xFFFF` = untere ROM-Hälfte, fest (kein Code-Banking).
   `0x1000` (IC12) wählt, was im Fenster `0x0800`–`0x0FFF` erscheint:
   `00`–`0F` SRAM-Seiten, `10` LCD, `30` Tasten, `40` Effekt-Chip?, `60` PCM-Chip,
   `80`–`9F` obere ROM-Hälfte (Daten, Texte). `0x1002`–`0x1FFF` = SRAM-Seiten `0E`/`0F`, fest.
   Die Firmware lädt ihre Wählwerte aus ROM `0x1B256` — die Tabelle steht also im ROM selbst.
3. [x] **NVRAM:** 32 KB über MAMEs NVRAM-Gerät.
4. [~] **LCD 24×2:** Text wird mitgeschrieben (Log), Darstellung fehlt noch. MAMEs HD44780
   braucht ein nicht gedumptes Zeichen-ROM; Zeichensatz wie bei emu110 aus einer freien Schrift.
5. [x] **Tasten:** 14-Bit-Matrix über `0x080E`/`0x080D` (Bank `30`), aktiv low. Zuordnung per
   Versuch ermittelt und mit Gegenproben bestätigt: Bit 0 EDIT, 1 DATA, 2 EXIT, 3 ENTER,
   4 PART/INST ◀◀, 5 PART/INST ▶▶, 8 MARK, 9 JUMP, 10 CURSOR ◀, 11 CURSOR ▶, 12 VALUE ▼,
   13 VALUE ▲; Bits 6/7 frei. LEDs (MIDI) noch offen.
6. [x] **MIDI IN/OUT/THRU** wie beim U-110 (bitgenau 31250 Baud, THRU als Leitung).
   Getestet mit `tools/make_test_mid.py` über MAMEs `-min` (spielt ab 10 s): Program Change
   auf Kanal 16 schaltet auf P-06, eine Roland-RQ1-Anfrage (Gerät 10h, Modell 2Bh)
   beantwortet der U-220 mit einer korrekten DT1 auf MIDI OUT.
7. [x] PCM-Chip hängt an **EXTINT** (Handler `0x22DD` schreibt die Chip-Register).

Werkzeuge: `tools/u220_run.sh` (bauen + kopflos laufen lassen, LCD-Text aus dem Log),
`tools/u220_keys.lua` (Tasten nach Zeitplan drücken, `U220_PRESS="3@4"` = ENTER bei 4 s).

**Meilenstein 1:** Bootscreen, Menüs bedienbar, MIDI wird angenommen, alles noch stumm.
**Stand 24.10.2026:** Die Firmware v1.02 bootet („RS-PCM Sound Module“), bietet bei leerem
Speicher „Memory Initialize“ an, initialisiert auf ENTER und zeigt den Spielmodus
`P-01: Acoust Piano [001]`. EDIT-, DATA-, MARK- und JUMP-Menüs öffnen sich. MIDI läuft in
beide Richtungen. **Meilenstein 1 erreicht.**

Bekannt: Nach „Memory Initialize“ zeigt das LCD ab P-02 statt des Namens `############`.
Das bleibt auch mit funktionierendem PCM-Chip (Phase 2) so: Im Programm-ROM stehen nur
der Startpatch „Acoust Piano“ und das Starttimbre „A.Piano 2“, die Werksklänge stehen in
keinem ROM. Sie kommen per SysEx-Bulk-Dump (siehe „Werksdaten“ unten). „Data → Init → All“
am echten Gerät löscht also die Werksklänge, statt sie wiederherzustellen. Bank `0x70` wird
von der Firmware auf Anwesenheit getestet (Muster `7F`/`01`/`FF`), vermutlich eine Karte;
bleibt leer.

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

**Stand 24.10.2026 — erster Ton.** Der PCM-Chip aus dem emu110-Fork ist angeschlossen
(Bank `60`, Register n = Fensteradresse n, 32,768 MHz ÷ 1024, `PCM_FLOAT8_DELTA`, Hüllkurven
der Firmware über EXTINT). Die Wave-ROMs werden wie beim U-110 entschlüsselt (A–D intern,
E/F auf den Positionen der ersten beiden U-110-Kartenslots); der ROM-Lesekanal über Stimme 0
liefert korrekt „A.PIANO 1“ usw. Eine Tonleiter mit Akkord auf P-01 „Acoust Piano“ klingt:
`renders/u220_demo.wav` (gerendert mit `-wavwrite`, ohne Audiogerät).

Erkenntnis zu den Patches: Im ROM steht nur der Startpatch „Acoust Piano“. Die 64 Werkspatches
lädt der echte U-220 von einer **Factory-Preset-Karte** (Testmodus 11, „Insert Fact. Preset
Card“). Nach „Memory Initialize“ sind P-02 bis P-64 leer. Die Werkspatches holen wir uns
später per **Bulk Dump von Helges U-220** (DATA → Bulk, SysEx) und spielen sie ein.

Klang-Befund 24.10.2026 (Helge: „Piano erkennbar, aber phaser-/kammfilterartig“):
- Jede Piano-Note spielt **zwei Samples, leise und laut, per Velocity überblendet**
  (Velocity 10: nur das leise; 127: das laute überwiegt). Bei mittlerer Velocity laufen
  beide fast gleich laut, ~0,5 ms gegeneinander versetzt und minimal verstimmt (0,4 Cent).
  Das ergibt rechnerisch einen leicht wandernden Kammfilter.
- Tonhöhen der Demo-Töne: alle innerhalb ±3 Cent vom Soll (F4 schwebt ±6 Cent).
- Eine „Tonhöhenrampe am Ende“ war ein Artefakt des Stummschalt-Experiments `U220_MUTE`,
  nicht des normalen Betriebs.
- **Ursache gefunden und behoben (24.10.2026):** Die U-220-Firmware schreibt ein
  Hüllkurven-Segment als `07` (Ziel) **dann** `06` (Rate); der U-110 schreibt `06` dann `07`
  in einem 16-Bit-Zugriff, und das Chip-Modell startet das Segment bei `07`. Beim U-220 lief
  daher jedes Segment mit der Rate des vorherigen: Releases dauerten Sekunden statt ~60 ms,
  und die Velocity-Ebenen lagen falsch zueinander. Der U-220-Treiber hält `07` jetzt bis
  zum folgenden `06` zurück. Ergebnis gegen die Hardware-Aufnahme (T-001 A.Piano 2,
  Velocity-Reihe): Release deckungsgleich, Spektral-Ähnlichkeit 0,90–0,92 statt 0,60–0,79.
- **Zweiter Fehler (24.10.2026): Tonhöhe stieg im Release.** Die Firmware liest den Zustand
  einer Stimme als 16-Bit-Wort bei `0x0980[Stimme]`, auch für ungerade Stimmen. MAMEs MCS-96
  richtete solche Wortzugriffe auf gerade Adressen aus; die Firmware las so den Zustand der
  Partnerstimme und addierte nach deren Freigabe bei jedem Durchlauf erneut die
  Loop-Feinstimmung. Der echte U-220 tut das nicht. Fix: Geräteoption
  `set_unaligned_ext_words(true)` im MCS-96-Kern, nur vom U-220-Treiber eingeschaltet.
  Geprüft gegen die Hardware: Tonhöhe bis zur Stille stabil (Akkord-Ausklang 522–525 Hz wie
  Hardware 523–525 Hz), Release-Pegel ±1–3 dB.
- **Stand 24.10.2026: T-001 „A.Piano 2“ klingt laut Helge wie sein U-220** (A/B mit
  `renders/emu3_*.wav` gegen `renders/hw_*.wav`). Bekannte Restunterschiede: Emulation
  ~4 dB lauter und etwas heller (Ausgangsfilter/Analogstufe noch nicht nachgebildet).
- **Prüfregel:** Vor jeder Hörprobe Tonhöhe pro 50 ms über die ganze Note *einschließlich
  Release bis zur Stille* messen und mit der Hardware vergleichen.
- Hardware-Referenzen liegen in `renders/hw_*.wav` (Helges U-220, XR18 Kanal 9/10 per ASIO,
  MIDI über `MIDIOUT2 (MIDIFACE 2x2)`), aufgenommen mit `tools/hwcapture/hwcapture_asio.py`.
- Diagnose-Schalter im Treiber: `U220_TGTRACE` (jeder Chip-Schreibzugriff mit PC),
  `U220_MUTE=maske` (Stimmen stumm, verfälscht die Firmware-Logik!), `U220_WATCHPC=pc`.

### Werksdaten (SysEx)

Die 64 Patches, 128 Timbres und Rhythmus-Sets der Werksausstattung gibt es im Netz als
Bulk-Dump `U220Factory.Syx` (33 883 Byte). Er ist urheberrechtlich nicht unsere Sache: nie ins
Repo, der Benutzer legt ihn selbst ab. Die verbreitete Datei endet mit einer zweiten, unvollständigen
Kopie der letzten Pakete (`00 26`–`00 29`, das letzte abgeschnitten); `tools/syx_to_mid.py`
prüft Prüfsummen, wirft Doppeltes und Abgeschnittenes raus (247 Pakete, 33 398 Byte) und
schreibt eine MIDI-Datei mit 20 ms Pause zwischen den Paketen für MAMEs `-min`.
`tools/syx_send.py` schickt den geprüften Dump unter Windows an ein echtes Gerät.

- Emulation: lädt den Dump im Spielmodus („Receiving SysEx.“), danach P-01 Acoust Piano …
  P-64 Catastrophe, Namen wie im Werksheft. Das Temp-Patch des Dumps heißt „Future Pad“; die
  Speicherplätze sind davon unberührt. Ein so gefülltes NVRAM liegt auf kellerkind2 unter
  `~/dev/run/nvram_factory/` und soll Startzustand für Tests und später für das Plugin werden
  (Plugin: SysEx-Import beim ersten Start, siehe Phase 4).
- Empfang: `Edit → Setup → MIDI`: `Rx SysEx = On`, `SysEx Device ID = 17` (Werkseinstellung,
  passt zum Dump, der an Gerät `10h` geht). Ein Speicherschutz existiert nicht.
- Menübäume: `Edit` = Setup/Patch/Timbre/Rythm; `Setup` = M.Tune/Efct/LCD/MIDI/Map;
  `Data` = Write/Bulk/Init/Util; `Bulk` und `Init` = All/Setup/Temp/(Memory bzw. Jump Page);
  `Util` = ROM Play/MIDI Monitor.

### Timbre-A/B (24.10.2026)

`tools/make_timbre_ab.py` spielt alle 128 Timbres je einmal (C4, Vel. 100, 3½ min),
`tools/ab_compare.py` vergleicht Hardware-Aufnahme und Emulation Platz für Platz. Ergebnis
mit Werksdaten auf beiden Seiten, Effekte am Gerät aus:
- Pegel je Timbre bis auf ±0,5 dB gleich (nach dem gemeinsamen Versatz: Emulation +2 dB),
  Hüllkurven gleich, Tonhöhenverläufe einschließlich Vibrato und Release gleich. Die
  Werksdaten im echten Gerät sind also vollständig angekommen.
- Grundsätzlich etwas heller (Median 1,08), wie schon bei A.Piano 2 gemessen.
- **Deutlich anderes Spektrum** bei einigen Geräusch- und Effekt-Timbres: Soundtrack 1–3,
  Pomona 2, Native 4/5, Emergency 3–5 (Emulation viel dunkler), Deep 1 (viel heller),
  Lunar 2, Emergency 1/2, Catastrophe 1/3. Verdacht: falsche Wellenform oder falsche
  Lage von Wave-ROM E/F (siehe Punkt 3), oder Rauschen/Interpolation im Chip.
- Hörvergleich (Helge): fast alle markierten Paare praktisch identisch, auch Soundtrack 2
  und Native 5; die Spektralmaße überzeichnen bei Geräuschklängen. Einzig **T-114 Deep 1**
  ist hörbar anders: das Mischungsverhältnis der beiden Tones (Sample 1/2) weicht ab, der
  Charakter stimmt. **Vermutlich erklärt** (Helges Idee): Deep 1 spielt dasselbe Sample auf
  zwei Stimmen, eine fest (Schritt `1BC1`), die andere mit Zufalls-Tonhöhe (`3435`…`3DB2`,
  ~2,4 Halbtöne Streuung bei 4 Anschlägen). Das hörbare „Verhältnis“ der beiden ändert sich
  also von Anschlag zu Anschlag. Bei Gelegenheit mit 4 Hardware-Anschlägen bestätigen.
- Pitch-Markierungen bei Geräuschklängen sind Messartefakte.
- **Drum-A/B** (`make_timbre_ab.py --rhythm`, 4 Sets × Tasten 35–98, 3¼ min): belegt sind in
  allen Sets dieselben Tasten (35–58, 69, 79, 81, 84–93, F.X. mehr), auf beiden Seiten gleich.
  Hüllkurven und Tom-Tonhöhenverläufe gleich. Auffällig: (a) Drums in der Emulation gesamt
  **4 dB leiser**, die Timbres dagegen 2 dB lauter, also ~6 dB Unterschied zwischen
  Rhythmus- und Melodieteil (Rhythmus-Teilpegel, Panorama-Gesetz bei den einzeln gepannten
  Drums?); (b) Hi-Hats/Tamburin/Becken (42, 44, 54, 59, 94) relativ +3…5 dB in der
  Emulation, passend zur fehlenden Höhendämpfung der Ausgangsstufe; (c) einzelne F.X.-Tasten
  (35, 44, 69, 86) und Dry 35 mit anderem Spektrum. Hörprobe (Helge): nur F.X. 35/69/86
  weichen ab, um 4–8 Halbtöne. **Aufgeklärt:** F.X. 69 und 86 haben eine Zufalls-Tonhöhe
  (Firmware-PRNG `0x1084/0x1085`, Start 02/00 beim Einschalten, Routine `0x93B3`); je 4 Schläge
  streuen auf Gerät und Emulation gleich weit (F.X. 69 über ~10, F.X. 86 über ~3 Halbtöne).
  F.X. 35 ist bei Wiederholung auf beiden Seiten identisch; im A/B war es der erste Schlag
  0,25 s nach dem Set-Wechsel. Vermutung: das echte Gerät braucht länger zum Laden des Sets
  (Wave-ROM-Lesezugriffe über Stimme 0) und spielte noch mit altem Datensatz, die Emulation
  rechnet schneller (keine Wartezyklen für externe Zugriffe). Kein Klangfehler, aber ein
  Hinweis auf zu schnelle CPU-Zeiten; für Tests Set-Wechsel ≥ 0,5 s vor der ersten Note. Hardware-Aufnahme
  clippte nur bei F.X. 91 (dort daher −3,5 dB).
- **Vorsicht bei Drum-Vergleichen:** Laut Helge klingen einige Drums schon zwischen echten
  Geräten leicht verschieden (Aufnahmen seines ersten U-220 von Ende der 80er/Anfang der
  90er gegen das heutige Gerät; Firmware des ersten unbekannt, Alterung der Analogstufe
  möglich). Kleine Abweichungen gegen *ein* Gerät sind also kein sicherer Emulationsfehler. T-128 „Drums“ schweigt auf
  beiden Seiten bei C4.

Offen in dieser Phase: Tonhöhe gegen die Hardware messen (Punkt 2), Lage von E/F gegen
Tones mit Karten-Samples prüfen (Punkt 3), die zwei echten Kartenslots (Punkt 4),
Ausgänge MIX/DIRECT 1/2, Abgleich mit Aufnahmen des echten Geräts (Punkt 6).

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

**Was der U-220 an Effekten hat** (aus den Texten der Firmware v1.02 und den emulierten
Menüs `Edit → Patch → Efct`, 24.10.2026). Die Effekte gehören zum **Patch**; dazu kommen zwei
globale Schalter in `Edit → Setup → Efct` (Chorus On/Off, Reverb On/Off).

| Block | Typen | Parameter (Bereich) |
|---|---|---|
| Reverb/Delay | Room1, Room2, Room3, Hall1, Hall2, Gate, Delay, CrossDelay | Type, Time 0–31, Level 0–31, Delay FB 0–31 |
| Chorus | Chorus1, Chorus2, FB-Chorus, Flanger, Short Delay | Type, Out = Pre Rev / Post Rev, Level 0–31, Delay Time 0–31, Rate 0–31, Depth 0–31, Feedback ±31 (gesehen −14…+31) |

- „Out = Pre Rev“: der Chorus läuft in den Hall; „Post Rev“: hinter ihm, parallel zum Ausgang.
- Per MIDI-Controller steuerbar (Controller-Zuweisung im Patch): Chrs Level, Chrs Rate,
  Chrs Feedback, Rev Level, Delay Feedback.
- Vermutlich je Part/Rhythmus-Instrument ein Ausgangsweg; die Firmware kennt die Texte
  „Dry / Rev / Cho“ und „Dir1 / Dir2“ (Direktausgänge). Noch im Menü bestätigen.
- Werkspatch-Beispiele: P-01 Hall2 Time 4 + Chorus2 (Pre Rev, Level 19, Delay 15, Rate 16);
  P-02 Room3 Time 4 + Chorus2 (Pre Rev, Level 31, Delay 18, Rate 4).
- Testmodus der Firmware hat einen Punkt „.10. Effect Test“.
- **Lage im Patch** (Temp-Bulk-Dumps der Emulation verglichen, je ein Parameter geändert;
  `tools/make_reverb_probe.py` hat die Tabelle): Bytes 12–19 als 64-Bit-Wort,
  Chorus Rate 0–4, Level 6–10, Depth 11–15, Feedback 23–28 (+32 versetzt), Type 29–31,
  Delay 32–36, Out 63; Reverb Time 16–20, Delay FB 43–47, Level 48–52, Type 60–62.
  Ein DT1 an 00 06 00 (erstes Paket des Temp-Dumps) setzt sie am laufenden Gerät.
- Emulation erzeugt Bulk-Dumps selbst (`Data → Bulk → Temp`, `u220_render --midi-out`).

**Hall-Messung am Gerät (24.10.2026)**, `tools/make_reverb_probe.py` + `tools/reverb_decay.py`,
Aufnahme `renders/hw_reverb_probe3.wav` (Velocity 60, kein Clipping), Nachhallzeit RT60
(T20 × 3, Fenster 4,8 s, lange Hallen daher eher unterschätzt) für Time 0 → 31:
Room1 1,1 → 2,3 s · Room2 1,7 → 4,4 s · Room3 1,4 → 3,5 s · Hall1 2,7 → 6,6 s ·
Hall2 2,5 → 6,4 s · Gate ~0,8 s (unabhängig von Time) · Delay/CrossDelay: Time = Verzögerung,
Delay FB 31 ≈ endlos (Helge: „unendliches Feedback“). Level 0/10/20/31 → Fahne −27/−24/−19/−11 dB
unter dem Direktschall (Hall2 Time 4). Die erste Messung (138-Byte-Pakete) war wirkungslos,
siehe MIDIFACE-Hinweis in Phase 4.

**Wichtiger Befund (24.10.2026): Panorama und Effekt-Sends macht IC25, nicht der PCM-Chip.**
Die Emulation ist bisher mono (L = R exakt), das Gerät stereo (Drums verteilt, Korrelation
L/R 0,75 ohne Effekte). Die PCM-Register 00/01 bleiben bei allen Drums 0; dafür schreibt
die CPU bei jedem Anschlag Gruppen an IC25 (Fenster-Bank 0x40, U220_FXTRACE):
`00A = Kanal, 000 = F0, 001 = Adresse (stimmenabhängig), 002 = Wert, 006 = Kanal`.
Kanal FE/FF tragen für eine Stimme ein Gain-Paar (Kick 36/36, Hi-Hat 2B/3F, High Tom 1E/47
= Pan), Kanal 00/01 vermutlich die Reverb-/Chorus-Sends. Außerdem schreibt die CPU auch im
Leerlauf ~4000 Werte/s an 000/001/002/004/006/00A (Koeffizienten, LFO?). Folge: Der Mischer
von IC25 (pro Stimme L/R-Gain und Sends) muss vor dem Hall nachgebildet werden; der
PCM-Chip braucht dafür Einzelstimmen- statt Summenausgang.

**IC25-Mischer umgesetzt (24.10.2026)**, `mame/src/mame/roland/roland_u220_ic25.h` (Treiber
und Kern teilen ihn): IC25 ist ein DSP mit 256 Programmworten à 24 Bit (geladen aus EPROM
0x1A000 von Routine 0xBB10); jede der 32 Stimmen hat einen Block von Programmworten
(Adressen: Tabelle EPROM 0x1803E, 6 Bytes pro Stimme). Bei jedem Anschlag schreibt die
Firmware Gain L/R ins Low-Byte und das Ziel in Bits 1–5 des Mittelbytes: 0x1E/1F Dry,
0x12/13 Rev, 0x14/15 Cho, 0x18–1B Direct (Menü „Output Asgn“/„Pan“ pro Part bestätigt).
Gains linear, 0x1A = Eins. Der PCM-Chip gibt dafür sechs Busse über eine Gain-Matrix aus
(`roland_lp`: `set_gain_matrix`, `set_voice_gain`). Ergebnis: Stereo; Drum-Panorama
innerhalb ~0,8 dB am Gerät (Hi-Hat +2,9 / Gerät +3,7 dB, High Tom +7,5 / +7,8 dB);
Drums und Melodie jetzt beide +2 dB gegen das Gerät (vorher −4,2 / +2,0). Nulltest
MAME ↔ Kern in Stereo weiter ±1 LSB. Hall und Chorus bekommen die Send-Busse.

**Helges Gegenprobe in Cubase (24.10.2026):** gleicher MIDI-Track zeitgleich an U-220 und
Ostrich, eins davon phasengedreht: die beiden löschen sich weitgehend aus. Kanal 10 spielt
die Drums (nach DPF-Patch 0006), Stereo stimmt.

**Hall-Modul v1 (24.10.2026)**, `plugin/core/u220_reverb.{h,cpp}` (EffectsModule): FDN mit 8
Leitungen + 4 Diffusoren für Room1–3/Hall1–2, Nachhallzeit bei 250 Hz und 8 kHz aus den
gemessenen Tabellen über Time interpoliert; Gate = Netzwerk mit Gate (450 ms ab Anschlag);
Delay/CrossDelay = Echoleitung 10 + 12,9 ms × Time (CrossDelay rechts bei halber Periode).
Parameter liest der Kern aus dem RAM (Temp-Patch-Effektbytes 0x143C, Schalter 0x1401 Bit 4/5).
Impulsantworten: Messung gegen trockene Emulation + Hall-Send entfaltet (`renders/
u220_reverb_irs.npz`). Abgleich mit der Probe: Fahnenpegel pro Typ im Median ±1 dB,
Nachhallzeit Room/Hall/Delay ±2–8 % (Gate +27 %), Stereo-Korrelation der Fahne wie am
Gerät nahe 0 (Gerät −0,03…+0,05; Ostrich Room1 −0,29, sonst −0,09…+0,29; Gate 0,70 wie
Gerät 0,74). Nach dem Abgriff-Umbau Pegel neu angepasst: alle Typen ±1 dB.
Chorus fehlt noch; bei „Pre Rev“ geht der Chorus-Bus trocken in den Hall.

**TODO (Stand 24.10.2026, Helges Cubase-Test):** Hall und Delay klingen nah am Original,
sind am U-220 aber **generell etwas lauter beigemischt**. Kandidaten, in dieser Reihenfolge:
1. ~~Pegelgesetz zu steil~~ **erledigt 25.10.2026:** `make_reverb_probe.py --levels`
   (Hall2, Time 8, Level 0/5/…/31), Aufnahme `renders/hw_reverb_levels.wav`. Das Gerät
   fällt um etwa 0,8 dB je Stufe (gegen Level 31: 25 −4,9, 20 −8,2, 15 −11,9, 10 −16,2,
   5 −22,1 dB); (Level/31)^1,9 war bei Level 10 rund 4 dB zu leise. Jetzt Tabelle in dB,
   dazu Hall2 +1,3 dB. Fahne/Direktschall danach auf allen Stufen ±0,4 dB (Level 5 −0,9).
   **Klang abgeglichen 25.10.2026 (automatisch):** Helge hörte trotz Pegel große
   Unterschiede (Gerät Room1 T10 ≈ Ostrich Room3 T4). Neues Verfahren:
   - `tools/reverb_edr.py`: Energy Decay Relief in Oktavbändern 125 Hz–8 kHz × Zeitfenster
     0–4,8 s, Hall allein (Gerät − trockene Emulation, Ostrich nass − trocken), jeder Slot
     einzeln auf Bruchteile eines Samples ausgerichtet (MIDI-Weg jittert ±1,5 ms), Zellen
     unter Rauschgrenze + 6 dB verworfen.
   - Messfolge v2 (`make_reverb_probe.py --v2`): Anregung nur Kick 2/Rim Shot/Closed Hi-Hat
     (alle Bänder nach ~110 ms 45 dB unten), vorab der komplette Temp-Dump von P-01 in
     kurzen Paketen, damit Gerät und Ostrich dieselben Patchdaten spielen. Die Klavier-Anregung
     von v1 ließ sich nicht abziehen (weder gegen Emulation noch gegen das Gerät selbst) und
     verdeckte die ersten 300 ms. Rauschgrenze v2: −87…−98 dB.
   - `ReverbTuning` (`plugin/core/u220_reverb_tuning.inc`): pro Typ und Time-Punkt
     Oktav-Ausgangs-EQ, Abklingzeit tief/mitte/hoch (Shelving-Dämpfung in jeder Leitung),
     Gate-Haltezeit und -Release, Echo-Feedback-Skalierung. `u220_reverb_run` rendert nur den
     Hall aus aufgezeichneten Send-Bussen; `tools/reverb_fit.py` passt alles an (~17 s pro
     Iteration, kein Mensch in der Schleife).
   - Ergebnis: rms-Fehler über alle Bänder × Fenster 7,6 → 1,5 dB. Room3/Hall1/Hall2 ~1 dB,
     Room2 ~1,7, Room1 ~2,3 (die Fahne am Gerät klingt nicht exponentiell ab, späte Bässe
     sehr lang), Gate ~2,5, Delays < 0,6.
   - Gate am Gerät: kein harter Schnitt, flach ~100–200 ms (je nach Time), dann ~250 dB/s.
   - Room1 war bei Ostrich in den Höhen bis 15 dB zu laut, Room2 4–6 dB zu leise.
   - Delay/CrossDelay bei FB 31: am Gerät fast verlustfrei (gefittet 0,97 pro Wiederholung).
   - **Hörtest Helge** (Time 15, Level 9, FB 15; Delay Time 20; CrossDelay Time 30/FB 20):
     Room1, Hall1, Gate, Delay identisch bzw. nahezu identisch; Room3 nahezu (gegen Ende
     minimal mehr Höhen am Gerät, lohnt nicht); CrossDelay nahezu. Room2 und Hall2 am Gerät
     etwas mehr Hall → beide +1 dB nach Gehör (in der Tuning-Datei vermerkt).
   **Chorus (25.10.2026):** `plugin/core/u220_chorus.{h,cpp}`, zusammen mit dem Hall in
   `U220Fx` (`u220_fx.h`). Messung mit Helges Idee: gehaltener Sägezahn (P-02, Part 1 nur
   Dry+Cho, Program Change 59 = T-060 Saw Wave 1, Vibrato aus über Temp-Timbre-Byte 25 =
   Adresse 00 10 32), `tools/make_chorus_probe.py`; Auswertung `tools/chorus_analyse.py`
   liest τ(t), g, f an der einen steilen Flanke pro 30,6-ms-Schleife (Saw Wave 1 wiederholt
   sich alle zwei Perioden). Befund:
   - pro Seite eine verzögerte Kopie; Mindestverzögerung per Delay: Chorus1/FB-Chorus
     16,0 + 1,03 ms/Stufe, Chorus2 8,0 + 0,516, Flanger 0,06 + 0,516, Short Delay 0,5 + 2,05
     (ohne LFO)
   - LFO: Dreieck mit fester Geschwindigkeit (ms/s, Feld Bits 11–15: 0,24/4,1/7,3/16,7/31,6/
     60,1 bei 0/5/8/16/24/31) zwischen 0 und einer Spanne (Feld Bits 0–4: 16,0 − 0,5 ms/Stufe);
     Rate = Geschwindigkeit/(2·Spanne); links/rechts gegenphasig. Menü bestätigt (Helge,
     P-02: Rate 4, Depth 5): **Rate** = Bits 0–4 = Spanne (höher → enger und schneller),
     **Depth** = Bits 11–15 = Geschwindigkeit.
   - Gain 0,94 bei Level 31 (Short Delay 0,99), ~0,7 dB/Stufe darunter
   - Feedback 0,027/Stufe: FB-Chorus und Flanger nur rechts, Short Delay beidseitig
   - Nachbau gegen dieselbe Auswertung: Mindestverzögerung ±0,3 ms, Spanne < 1 %, Rate 2–3 %,
     g ±0,02, f ±0,03. A/B-Paare: `tools/make_ab.py` → `renders/chorus_ab/` (lokal).
   - Offen: was „Chorus Out = Pre Rev“ in den Hall schickt.
   - **Hörtest Helge (53 Paare):** „unglaublich nahe“; abweichend 02, 03, 16, 25, 26, 31,
     32, 35, 40, 41, 44. Behoben: Rate 31 lief zu schnell (Spanne 0,44 → 0,51 ms); bei
     Depth 0 parkt das Gerät den LFO oben (links 0,6 ms unter der längsten, rechts 0,6 ms über
     der kürzesten Verzögerung, wackelt 0,87 ms bei 0,26 Hz) – Ostrich ließ ihn liegen, wo er
     war, was mit starkem Feedback andere Kammfilter gab. Neue Paare: `renders/chorus_ab2/`.
   - **Hörtest 2:** Rate 31 nun zu langsam (0,51 ms) → 0,49 ms wie gemessen (≈17 Hz). FB±31
     „am Gerät intensiver“: links und rechts lesen am Gerät **eine** Leitung mit Rückkopplung
     vom rechten Abgriff – links liegt die Rezirkulation bei τL + τR (Flanger FB +31: 0,81).
     Ostrich jetzt ebenso (0,72–0,74 an derselben Stelle). Paare: `renders/chorus_ab3/`.
     Helge: „Intensität und LFO jetzt nahezu identisch“ → Chorus abgeschlossen bis auf
     Chorus2-Growl.
   - **Pre/Post Rev** laut Owner's Manual S. 54/61: Pre Rev = Chorus → Hall (der Hall bekommt
     den gechorusten Klang: Send + Kopien), Post Rev = kein Hall auf Cho-Parts. In `U220Fx`
     so umgesetzt; nicht gegen das Gerät gemessen.
   - **Chorus2 „Growl“ bleibt** (Helge: vermutlich analog, z. B. mitschwingende OpAmps);
     im README unter „Bekannte Abweichungen“.
   - **Growl, 2. Anlauf (Helge: macht im Mix den Unterschied „punchy“):** Chorus2 hat eine
     **zweite, dumpfe Kopie** (nach Abzug von Dry + Hauptkopie so laut wie die Hauptkopie;
     Chorus1/FB/Flanger: −14…−16 dB): Dreieck über dieselbe Spanne, 3,3× so schnell, 12 ms
     unter der kürzesten Hauptverzögerung, L/R gegenphasig, 0,63 unter 250 Hz. Nachgebaut
     (220-Hz-Tiefpass, 0,65). Paare `renders/chorus_ab4/`.
   - **Snare nur links (Helge):** IC25-Mischer las Pegel und Ziel aus einem Programmwort;
     die Firmware schreibt den Pegel nach Eintrag 0/1 und das Ziel per Read-Modify-Write nach
     Eintrag 3/5 der Stimmentabelle, bei Stimme 7/9/11/13/20 verschiedene Wörter → Stimme 20
     rechts stumm. Behoben in `roland_u220_ic25.h` (Wirbel: 0,00 dB Abweichung statt 4,4).
   - **DAC-Spiegelfrequenzen:** Das Gerät filtert hinter dem 32-kHz-Wandler nicht steil: zu
     f < 16 kHz kommt 32 kHz − f mit (−3 dB bei 16,3 k … −38 dB bei 23,5 k; Bassline: Spektren
     12,5–15,5 k und Spiegel korrelieren 0,61). `DacStage.hpp`: 32→64 kHz, Spiegel durch die
     gemessene Kurve, → Host; an mit HW-EQ. Dabei: Resampler schnitt bei 14,9 kHz, jetzt 15,5.
   - **Pre Rev: rechts ein Sample später.** Bei P-02 (Chorus2, Pre Rev) war am Gerät die Seite
     in den Höhen lauter als die Mitte (Bassline 12–16 kHz: S −33/M −39 dB). Messfolge
     `--prepost`: bei Pre Rev kommt der **rechte** Kanal der Cho-Parts (Dry und Chorus) 31 µs =
     1 Sample bei 32 kHz später, bei Chorus1 und Chorus2; bei Post Rev oder Chorus aus nicht.
     In `U220Fx` nachgebaut; Bassline 8–16 kHz L/R −0,24 (Gerät −0,29, vorher +0,58).
     Paare: `renders/prepost_ab2/`, `renders/growl_ab/` (04 = neu).
   - **Zweite Kopie gefittet an Helges Bassline** (Syn Bass 4, P-02; `growl_fit.py`, Pegel/
     L/R-Korrelation/Seite-Mitte/Modulation in 5 Bändern): breitbandig statt 220-Hz-Tiefpass
     (die Bandmessung war oben durch Zeitfehler verfälscht). Helge wählte „ungefiltert, 0,8“:
     „nahezu ununterscheidbar“, Pegel passt (08 in `renders/growl_ab/`). Growl damit gelöst.
   - (Frühere Notiz) **Chorus2 „Growl“** (02, 25): am Gerät sind L und R bei Chorus2 viel weniger
     korreliert (0,16/0,27 statt 0,57/0,63 bei Ostrich; Chorus1/FB/Flanger stimmen auf ±0,02).
     Verzögerung, Hub, LFO, Gain, Feedback und Klangfarbe der Kopie stimmen; das Fehlende
     steckt nicht in der einen gemessenen Kopie. Nächster Ansatz: Restsignal nach Abzug von
     Dry + Kopie pro Seite untersuchen (zweite, dumpfere Kopie? Kreuzkopplung L↔R?).
2. Chorus-Weg: am Gerät stand Chorus Off; Parts mit „Asgn = Cho“ gehen bei Ostrich nur bei
   „Pre Rev“ in den Hall. Klären, was das Gerät mit Cho-Parts tut, wenn der Chorus aus ist.
3. Einstellung, die nur am Gerät anders ist: Temp-Patch/Patch-Daten am Gerät (alte Edits,
   die Werksdaten kamen per SysEx nur teilweise an) gegen Ostrich vergleichen; das neu und
   in kurzen Paketen eingespielte Werks-Set würde das ausschließen.
4. Room1-Stereobild (−0,29 statt ~0), Gate etwas zu lang.

**Vorgehen (Entscheidung Helge, 24.10.2026):** Phase 4 kommt vor Phase 3. Der Effektteil wird
als eigenes, austauschbares Modul gebaut (klare Schnittstelle: Effekt-Typ und -Parameter rein,
Stereo-Audio rein/raus) und zunächst **nach Gehör angenähert**, nicht emuliert (Schritt 3).
Liegt eines Tages eine Analyse des Chips und seines Programms vor, wird nur das Modul
ausgetauscht (Schritt 4).

**Meilenstein 3:** Alle Effekttypen vorhanden, im Blindvergleich nah an der Hardware.

## Phase 4 — Das Plugin

1. `plugin/core/u220_core.cpp/.h` analog zu `u110_core`: echtzeitfähig, ohne MAME-Framework,
   BSD-3-Clause, 32 kHz nativ. Das Resampling erledigt das Plugin.
   **Stand 24.10.2026: fertig.** Bauen mit `plugin/tools/build_u220_core.sh` (→
   `plugin/build-u220/libu220core.a` und `u220_render`). Effekte über die Schnittstelle
   `core/u220_effects.h` (vorerst Bypass). Nulltest gegen MAME (`-samplerate 32000`, gleiche
   MIDI-Ankunftszeiten aus dem `MIDI IN`-Log, `u220_render --no-filter`): alle 128 Timbres
   (219 s) und alle 256 Drum-Tasten (206 s) bis auf 1 LSB Rundung gleich, MAMEs WAV beginnt
   ein Sample früher; Blockgröße 37 statt 512 bitgleich. Rendert ~39× Echtzeit.
   Ausgangsstufe vorerst mit den Sallen-Key/RC-Werten des U-110 (ohne dessen HF-Korrektur):
   Helligkeit gegen Helges Gerät damit 1,01 statt 1,07 (Median über alle Timbres).

   **Plugin „Ostrich“ (24.10.2026, v0.1):** `plugin/ostrich/` (DPF, teilt `plugin/dpf` samt
   Patches und `plugin/compat` mit Voltaire). `make` (Linux) bzw. `make win` (mingw:
   VST3, CLAP, Standalone in `plugin/ostrich/bin-win/`). ROMs werden per SHA-1 erkannt,
   Dateinamen egal; gesucht in `%PROGRAMDATA%\Ostrich\roms` (zuerst, wegen MSIX),
   `%LOCALAPPDATA%\Ostrich\roms`, `Dokumente\Ostrich\roms`, Linux
   `~/.local/share/Ostrich/roms`, oder `OSTRICH_ROMS`. Liegt `U220Factory*.syx` daneben (oder
   in `../sysex/`), bootet eine neue Instanz wie ab Werk: „Memory Initialize“ mit ENTER, dann
   der Dump (20 ms Pause pro Paket), dann P-01 — alles in emulierter Zeit in ~0,5 s. IC10
   wird im Projekt gespeichert (Zustand `nvram`). Panel v0.1: LCD 24×2 mit CGRAM-Zeichen,
   die 12 Tasten (als Parameter, also auch automatisierbar), Volume, Statuszeile.
   Headless-Test `plugin/tools/ostrich_clap_render.c`: C4 bei 48 und 44,1 kHz, Tonhöhe
   262,8 Hz stabil bis −73 dB im Release (Gerät 262,5, gleiches Messverfahren), Zustand
   gespeichert (66 KB, enthält `nvram`).

   **Erster Test in Cubase 15 (Helge, 24.10.2026):** lädt, klingt, Tasten gehen (nach dem
   Umbau auf Zustandsnachrichten: Parameter-Edits aus der UI erreichen unter VST3 den
   DSP nur über die Automation des Hosts). 10 Sounds quer durch die Presets „zwischen HW
   und Ostrich nahezu identisch“. LCD-Cursor ergänzt: die Firmware markiert den gewählten
   Part mit dem Unterstrich-Cursor des Controllers (Befehle 0C/0E um jedes Neuschreiben).

   **Ausgangsstufe gemessen (24.10.2026):** Helges Pro-Q-3-EQ-Match (`eq/Real_U220.ffp`,
   nicht im Repo) machte Ostrich hörbar gleich, hob aber über 6 kHz bis +30 dB an. Grund:
   die geliehenen U-110-Werte (Sallen-Key ~6,7 und ~11,7 kHz) schneiden viel zu steil ab;
   das Gerät hat echtes Signal bis über 16 kHz (40 dB über der Stille, oberhalb von 16 kHz
   Spiegelfrequenzen des 32-kHz-DACs). Gegen den ungefilterten Kern bleibt nur ein sanftes
   High-Shelf: RBJ 5,2 kHz, −3,3 dB, Q 0,5. Damit 63 Hz–14 kHz innerhalb ±0,8 dB, Helligkeit
   1,00. Im Plugin als Schalter **HW-EQ** (Standard an); aus = roher Kern wie MAME.
   Das .ffp-Format von Pro-Q 3 („FQ3p“, Version 4): 24 Bänder × 13 float (Used, Enabled,
   Frequenz als log2 Hz, Gain dB, Dyn. Range, Dyn. an, Threshold, Q normiert mit
   Q = 0,025·1600^x, Shape 0 = Bell / 1 = Low Shelf, Slope, Stereo, Speakers, Solo).

   **Erledigt (Helge, 24.10.2026):** „heller“ und „weniger Gewicht“ waren die geliehene
   U-110-Filterung. Mit der gemessenen Ausgangsstufe klingt Ostrich ohne Pro-Q „nahezu
   identisch“ zum Gerät; beide Punkte gestrichen.
2. **Eigenes Panel:** U-220-Bedienfeld mit 24×2-LCD. Die U-110-Grafik und alle Roland-Logos
   müssen raus, die Artwork ist neu zu zeichnen. Schrift: nur OFL-lizenzierte Fonts
   (Barlow Semi Condensed ist schon im Repo; der Stil aus `tools/restyle_panel.py` ist
   die Ausgangsbasis).
   Laut Service Notes hat die Front: **12 Taster** (SW1–12 auf dem Switch Board; im
   Testmodus genannt u. a. PART ◀ ▶, EDIT, EXIT, ENTER, JUMP, MARK, CURSOR, VALUE/DATA),
   **eine grüne MIDI-LED**, **VOLUME-Knopf**, **PHONES**, **zwei Kartenslots**, **24×2-LCD
   mit Beleuchtung**. Kein FILT-Knopf: der ist eine emu110-Zutat (Höhenkorrektur) und wandert
   in ein Einstellungsmenü. Welche Taste welche Leitung der Schaltmatrix ist, ergibt sich in
   Phase 1 aus der Firmware.

   Anordnung laut Foto (Helge, 24.10.2026; Foto nur Vorlage, nicht im Repo), von links:

   | Bereich | Inhalt |
   |---|---|
   | Logo | „Roland U-220 RS-PCM SOUND MODULE“ (für uns: eigener Schriftzug) |
   | links | PHONES-Buchse, VOLUME-Drehknopf mit Skala |
   | Mitte | grünes 24×2-LCD, z. B. `T-020 Strings 1  [020]` / `R: \|01\|03\|02\|04\|05\|06\|…` |
   | Tastenblock 1 (2×2) | oben EDIT, DATA; unten MARK, JUMP; dazwischen der Aufdruck CHORUS / REVERB (Tastenkombination) |
   | Tastenblock 2 (2×4) | oben EXIT, ENTER, PART/INST ◀◀ ▶▶; unten CURSOR ◀ ▶, VALUE ▼ ▲ |
   | rechts | zwei PCM-CARD-Slots übereinander, MIDI-MESSAGE-LED, POWER |

   Beschriftungen von Helge am Gerät bestätigt.
3. **ROM-Suche:** Dateien per SHA1 erkennen und alle bekannten Namen akzeptieren
   (`roms/README.md`); verständliche Meldung, wenn etwas fehlt.
   **Geführtes ROM-Setup für Erstbenutzer** (Wunsch von Helge): Fehlt eine Firmware- oder
   Wave-ROM-Datei, öffnet sich ein Dialog, der die gesuchte Datei beim Namen nennt (Chip,
   Inhalt, erwartete Größe) und eine Dateiauswahl anbietet. Die gewählte Datei wird per
   SHA1 geprüft und in den ROM-Ordner des Benutzers kopiert, danach bootet die Maschine.
   Lohnt sich schon für Voltaire 110, nicht erst für den U-220.
4. Formate: CLAP und VST3 unter Windows. Bei emu110 aktualisiert sich das VST3-Panel noch
   nicht, das müssen wir beheben. Standalone; macOS später.
5. Patch-Bibliothek, SysEx-Import und -Export (U-220-Bulk-Dumps).
6. **Mehrfachausgänge (umgesetzt 26.10.2026, in Cubase von Helge bestätigt: Ausgänge zuschaltbar, Dir1/Dir2-Parts klingen dort ohne Effekt).** Drei Stereopaare wie
   am Gerät: MIX OUT (1/2), DIRECT OUT 1 (3/4), DIRECT OUT 2 (5/6). Laut Owner's Manual (S. 27,
   61): Output Assign pro Part Dry/Rev/Cho (→ MIX) oder Dir1/Dir2 (→ DIRECT, ohne Effekt);
   Einzelausgänge entstehen allein über Assign + Pan (7< / >7), einen eigenen
   Multi-Out-Modus gibt es nicht (frühere Notiz hier war falsch). Ist keine Buchse in DIRECT OUT
   gesteckt, kommt der Klang aus MIX, ohne Effekt, mit dem Volume-Regler.
   - Firmware → IC25: Zielcodes 0x18/0x19 = DIRECT 1, 0x1A/0x1B = DIRECT 2 (je L/R), per
     Emulation bestätigt (Menü Edit/Patch/PartN/Output, `U220_GAINTRACE` zeigt die Wörter).
     Die Firmware schreibt die Direct-Ziele unabhängig von gesteckten Buchsen -- das
     Umleiten auf MIX ist analog (Schaltkontakte der Buchsen).
   - Mischer (`roland_u220_ic25.h`): zehn Busse; MAME legt DIRECT weiter auf die
     Lautsprecher (wie ohne Kabel). Kern: `U220Core::render(mix, dir1, dir2)`, nullptr = ohne
     Kabel. Plugin: sechs Ausgänge in drei Gruppen; ein Paar ist "gesteckt", wenn der Host
     ihm eigene Puffer gibt -- in VST3, wenn der Ausgang im Instrument-Rack aktiviert ist
     (Patch 0011: zusätzliche Main-Busse, anfangs nur MIX aktiv -- als Aux-Busse liess Cubase
     sie nicht zuschalten; DPF gibt abgeschalteten Ausgängen einen gemeinsamen
     Dummy-Puffer). Parameter "Direct outs" (aus = immer über MIX, für CLAP/LV2-Hosts, die
     alle Ports belegen). Volume nur auf MIX, DIRECT mit festem Pegel wie am Gerät.
   - Geprüft mit `ostrich_clap_render` (Demo-Speicher per `tools/swap_nvram.py`): Part 6 auf
     Dir1/Dir2 klingt gesteckt nur auf seinem Paar, ungesteckt auf MIX, gleicher Pegel.
   - Offen: In der Werkseinstellung (P-01) blieb der klingende Part trotz Asgn=Dir1/Dir2
     auf Rev geroutet; mit Helges Gerätespeicher folgt das Routing sofort. Ursache unklar
     (welcher Part klingt? Patch-Eigenheit?), am Gerät gegenprüfen, falls es auffällt.
7. **PCM-Card-Slots (Helge, 25.10.2026, für später).** Zwei Slots für Karten der Serie
   SN-U110 (dieselben wie beim U-110). Plan: Card-Dumps aus dem Dateisystem einstecken und
   wieder ziehen (Menü am Panel oder Drag & Drop auf die gezeichneten Slots), Pfad im
   Plugin-State, beim Laden des Projekts wieder eingesteckt. Laut Manual zeigt das Gerät
   beim Stecken/Ziehen kurz „Checking PCM Card...“; fehlt die Karte eines gewählten Tons:
   „No Card!“, kein Ton. Zu klären: wo die Karten im 4-MB-Raum des PCM-Chips liegen (die
   internen Waves E/F sitzen auf den ersten beiden Kartenplätzen, Phase 2), wie die Firmware
   eine Karte erkennt (ID/Kopfdaten) und ob die Karte auch Timbre-Daten trägt;
   SHA1-Erkennung bekannter Dumps wie bei den ROMs. Keine Card-Dateien im Repo.
8. **MIDI OUT** aus dem Plugin (umgesetzt 26.10.2026, offline geprüft; in Cubase kommt SysEx nicht an):
   was die Firmware an MIDI OUT sendet, als MIDI-Ausgang des Plugins – für Librarian-Arbeit
   in der DAW. Das Plugin zerlegt den Bytestrom wie die Leitung (Running Status, SysEx über
   Blockgrenzen, Echtzeitbytes) und gibt jede Nachricht zum passenden Sample aus.
   - DPF konnte unter VST3 weder SysEx noch Program Change ausgeben, unter CLAP kein SysEx:
     Patch 0012 (VST3: Data-Event bzw. Legacy-CC 130; CLAP: `CLAP_EVENT_MIDI_SYSEX`).
   - Prüfung: `ostrich_clap_render` mit `Data → Bulk → Temp` per Tastendruck
     (`2@2,9@2.5,9@3,10@3.5,6@4,9@4.5,9@5,10@5.5,10@6,6@6.5,6@7`): 21 DT1-Pakete,
     2322 Byte, alle Prüfsummen gültig, bei 44,1 und 48 kHz.
   - In Cubase 15 von Helge geprüft: kommt nicht an, weder in der Aufnahme noch am U-220,
     auch ohne SysEx-Filter. Ungelöst, siehe „Offene Fragen und Risiken“.
   - Offen: DPF nimmt unter VST3 auch kein SysEx **an** (Eingang ist auskommentiert). Für
     SysEx-Import aus der DAW (Punkt 5) wäre das der nächste Patch.

9. **Mausbedienung im Display (Helge, 25.10.2026, für später).** Felder im LCD anklicken
   und per Mausrad/Ziehen ändern, statt CURSOR/PART/VALUE zu klicken. Die Firmware kennt das
   nicht; Ostrich übersetzt es in Tastendrücke, **im geschlossenen Regelkreis** über die
   echte Cursorposition aus dem LCD (HD44780-Cursor, wie fürs Panel schon ausgelesen):
   - Klick auf ein Feld → CURSOR ◀/▶ drücken, bis der Cursor dort steht (Abbruch, wenn er
     sich nicht mehr bewegt) – funktioniert auf jeder Seite, ohne Seitenwissen
   - Klick auf einen Part in Zeile 2 → PART ◀◀/▶▶ ebenso
   - Mausrad/Ziehen → VALUE ▲/▼ je Raste; evtl. Doppelklick = ENTER, Rechtsklick = EXIT
   - Tempo: heute 125 ms emulierte Haltezeit je Druck; kürzeste sichere Zeit aus der
     Firmware-Tastenabfrage bestimmen (vermutlich 20–30 ms) und die Emulation während einer
     Tastenfolge schneller als Echtzeit laufen lassen
   - später ggf. Werte eintippen und per SysEx (DT1) setzen; dafür bräuchte es eine
     Feld→Adresse-Zuordnung je Seite

10. **Komfort: Temp-Zustand über ROM Play retten (für später, selten gebraucht).** Nach den
   Demo-Songs lädt die Firmware den aktuellen Patch neu aus dem Patch-Speicher, ungespeicherte
   Temp-Änderungen gehen verloren (in der Emulation geprüft, 25.10.2026: Temp danach = gespeicherter
   P-02, 80/80 Bytes). Das Gerät tut dasselbe. Ostrich könnte den Temp-Bereich vor ROM Play
   sichern und danach zurückschreiben – bewusst besser als das Gerät.

**Panel-Freeze (Ursache gefunden 26.10.2026, Fix unter Beobachtung):** Es war kein
Zeichenproblem, sondern die Maus. Klicks kommen in Fensterpixeln an, `onMouse` teilte durch
`getWidth() / kW` -- das stimmt nur, solange das Fenster genau die Mindestgröße hat. Bei 125 %
Skalierung (Fenster 1428-1437 px, DGL skaliert automatisch) landete jeder Klick ein Viertel zu
weit rechts unten und traf keine Taste: das Panel wirkte eingefroren, obwohl es zeichnete.
Cubase speichert die Fenstergröße im Projekt, daher "merkte" sich das Plugin den Freeze. Fix:
`mouseScale()` = Fensterbreite / Designbreite (cc98339); per Skript und von Helge geprüft.

Irrweg dabei, festgehalten: Die Phasen mit stehenden draws und `visible 0` sind normales
Verhalten -- Cubase versteckt Plugin-Fenster, solange es im Hintergrund ist (HIDE/SHOW im
Mitschnitt). Die Vermutung "Cubase lässt WM_SETREDRAW aus" war falsch.

Diagnose, die vorerst drin bleibt: `FrameWatch` (WM_SETREDRAW/Show/Hide an den Vorfahren,
Selbstheilung nach SETREDRAW), Klick-Zähler in pugl/DGL (Patches 0008/0009) und jede
Klick-Abbildung im `ostrich-ui.log`, Größen-Trace aus Patch 0010. Nach einigen Tagen ohne
Freeze ausbauen (die Größen-Toleranz und die skalierte Mindestgröße in 0010 bleiben).
`tools/freeze_probe.ps1 -Press CUR>,CUR<` drückt Panel-Tasten per Skript, auch im Hintergrund.

**Fenstergröße (26.10.2026, erledigt):** Beim Öffnen schrumpfte das Panel in Cubase
schrittweise (1438x313 -> 1428x308). Ursache war 1150x250, das bei 125 % 1437,5 x 312,5
ergibt: Cubase rechnet in eigene Einheiten und zurück, setzt das Fenster einen Pixel niedriger,
als es meldet, und der nächste Layout-Durchgang startet von dort. Seither:
- Standardgröße 1196x260 (weiter 4,6 : 1, durch 4 teilbar, ganze Pixel bei 125/150/175/200 %).
- Patch 0010: DPF nimmt Größen innerhalb der Rundung an und meldet unter Windows die
  skalierte Mindestgröße (vorher unskaliert, Cubase fragte dutzendfach nach). Außerdem
  protokolliert er alle VST3-Größenaufrufe (`vst3 ...` im `ostrich-ui.log`).
- Das Panel merkt sich seine Größe im Projekt (State `uisize`, unskaliert), von Helge geprüft.
- Offen, harmlos: Cubase fragt in Schüben `checkSizeConstraint 292x12` (Antwort: die
  Mindestgröße). `setContentScaleFactor` kommt oft erst Sekunden nach dem Öffnen -- mit dem
  alten Klick-Code war das vermutlich der Moment, ab dem das Panel "einfror".

**Meilenstein 4:** Ostrich 0.1 läuft in der DAW.

## Phase 5 — Veröffentlichung und Rückfluss

- [ ] **Doku auf Englisch und auf die Fakten reduziert (Helge, 26.10.2026, für später).**
      README, ROADMAP, Analysen: Englisch, ohne Verlaufsprosa und Irrwege, nur was gilt
      und wie man es prüft.
- [ ] **Aufräumen (Helge, 26.10.2026, für später):** lokale Dateien und Ordner, die nicht
      mehr gebraucht werden (alte Builds, Proben-WAVs, Logs, Einmal-Skripte), hier unter
      Windows und auf kellerkind2. Vorher Liste zeigen; Hörproben-Ordner und Analysen
      bleiben nach AGENTS.md.
- [ ] **Umbenennung in „Ostrich 220“ (kurz O-220)**, Idee von Helge, erst viel später. Der
      Bezug zum Roland U-220 steht dann in der README. Betrifft Plugin-Name, VST3/CLAP-IDs
      (neue IDs, damit Sessions mit Voltaire 110 nicht verwechselt werden), Datenordner.
- [ ] Vor jedem öffentlichen Push: Prüfung, dass die Historie keine ROM-, Wave- oder
      Handbuch-Datei enthält (`git log --all --stat` gegen die `.gitignore`-Regeln).
- [ ] Repo öffentlich machen, Releases nur mit Binärdateien ohne ROMs. Die
      Selbsttests in CI dürfen keine ROMs brauchen; emu110 hat sie im Repo, wir nicht.
- [ ] Treiber `roland_u220.cpp` und Chip-Verbesserungen als PR an mamedev (BSD-3-Clause). Achtung! Zuvor prüfen, wie mamedev mit durch LLM erzeugten PRs umgeht. Wenn Policy unbekannt, PR erzeugen und Projekt selbst entscheiden lassen. Wenn gesichert unerwünscht, hier vermerken und Schritt überspringen. 
- [ ] Gemeinsame Verbesserungen als PR an emu110. Auch hier prüfen, wie emu110 mit durch LLM erzeugten PRs umgeht. Wenn Policy unbekannt, PR erzeugen und Projekt selbst entscheiden lassen. Wenn gesichert unerwünscht, hier vermerken und überspringen.

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
| **Ungelöst: SysEx aus dem VST3 kommt in Cubase nicht an** (26.10.2026). Ostrich übergibt die Pakete (`out 331 (dropped 0)` im `ostrich-dsp.log`, Data-Events F0…F7), Cubase 15 nimmt sie nicht auf und leitet sie nicht weiter, auch mit abgeschaltetem SysEx-Filter (Aufnahme und Thru). Vermutlich nimmt Cubase von VST3-Plugins kein SysEx an; Berichte im JUCE-Forum widersprechen sich. | offen | später: prüfen, ob Cubase überhaupt MIDI aus dem Plugin weitergibt (Testversion, die Noten/CCs sendet); CLAP-Host (z. B. Reaper, Bitwig) gegenprüfen; Ausweg wäre Export als .syx-Datei aus dem Plugin |

## Quellen

- MAME: `src/mame/roland/roland_u20.cpp`, `roland_cm32p.cpp`, `src/devices/sound/roland_lp.cpp`
- emu110: `EMULATOR-PLAN.md`, `IMPLEMENTATION-PLAN.md`, `WILD_IDEAS.md`, `analysis/`
- U-220 Service Notes (lokal unter `../upstream/emu110/reference/`, nicht in diesem Repo)
