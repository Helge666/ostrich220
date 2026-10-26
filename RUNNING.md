# The U-220 in MAME

The MAME driver (`mame/src/mame/roland/roland_u220.cpp`) is where the hardware was worked
out, and the reference the plugin core is null-tested against. It has no display (the
LCD controller's character ROM has never been dumped); the display's text goes to the log.
For the U-110 driver, see [emu110](https://github.com/eliggett/emu110).

## Build

```sh
cd mame
make SUBTARGET=u220 \
     SOURCES=src/mame/roland/roland_u20.cpp,src/mame/roland/roland_u110.cpp,src/mame/roland/roland_u220.cpp \
     NOWERROR=1 USE_QTDEBUG=0 -j5
```

The result is `mame/u220`.

## ROMs

MAME's names, in a folder `u220` under the ROM path: [roms/README.md](roms/README.md).
Firmware v1.02 is the default; `-bios v101` or `-bios v100` choose the others.

```sh
./u220 u220 -rompath /path/to/mameroms
```

## Panel keys

| Key | Keyboard |
|---|---|
| EDIT, DATA | E, D |
| EXIT, ENTER | Backspace, Enter |
| PART/INST ◀◀ ▶▶ | Page Up, Page Down |
| MARK, JUMP | M, J |
| CURSOR ◀ ▶ | Left, Right |
| VALUE ▼ ▲ | Down, Up |

## MIDI

`-midiin PORT` and `-midiout PORT` connect real ports (`-listmidi` lists them). `-min` also
plays files:

```sh
./u220 u220 -min song.mid
```

Factory data: the U-220 keeps its patches, timbres and rhythm sets in battery-backed RAM
only; the program ROM holds one patch and one timbre. A factory bulk dump has to be sent
the first time, with a pause between packets (the unit needs time to store each):

```sh
python3 tools/syx_to_mid.py U220Factory.syx factory.mid --gap-ms 20
./u220 u220 -min factory.mid
```

Receiving needs `Edit > Setup > MIDI`: Rx SysEx On, Device ID 17 (the factory setting).
The memory is saved in MAME's NVRAM folder.

## PCM cards

Two slots, `-cart1` and `-cart2`: raw SN-U110 card dumps (512 KB at most; smaller dumps are
mirrored), or a short name from MAME's `u110_card` software list.

```sh
./u220 u220 -cart1 /path/to/sn-u110-02.bin
```

Card tones are media 01–31 on the Tone page (`Edit > Timbre > Tone`): `02-001 LATIN 1` for
SN-U110-02. Without the card the page shows `No Card!`.

## Headless runs and scripted keys

`tools/u220_run.sh [SECONDS] [-- MAME options]` builds the driver, runs it without video or
sound and prints the display's text from the log. `tools/u220_keys.lua` presses keys on a
schedule:

```sh
U220_PRESS="1@4,3@5" ./u220 u220 -video none -sound none -seconds_to_run 8 -log \
    -autoboot_script tools/u220_keys.lua
grep '] LCD' error.log
```

`U220_PRESS` is `bit@seconds[:hold],…` with the key-matrix bits: 0 EDIT, 1 DATA, 2 EXIT,
3 ENTER, 4/5 PART ◀◀/▶▶, 8 MARK, 9 JUMP, 10/11 CURSOR ◀/▶, 12/13 VALUE ▼/▲.

## Traces

Environment variables, all printed to stderr:

| Variable | Where | What |
|---|---|---|
| `U220_TGTRACE` | driver, core | every write to the PCM chip |
| `U220_MIDITRACE` | core | every MIDI byte delivered to the CPU |
| `U220_LCDTRACE` | core | every LCD command |
| `U220_FXTRACE` | core | every write to the effect processor (IC25) |
| `U220_GAINTRACE` | core | each voice's mixer words and gains |
| `U220_ROMTRACE` | core | every wave-ROM/card byte the CPU reads through voice 0 |

The core's are used through `plugin/build-u220/u220_render`, which renders without a host:
`u220_render --roms DIR [--nvram FILE] [--seconds N] [--out FILE.wav] [--lcd] [--press …]
[--midi-at FILE] [--midi-out FILE] [--card SLOT:FILE[@SEC]] [--reverb]`.
