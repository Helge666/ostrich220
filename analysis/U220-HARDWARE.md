# Roland U-220 — hardware and firmware notes

What the emulation relies on, found by disassembly (`unidasm -arch i8x9x`), by running the
firmware in the MAME driver and the core, and by measuring a real U-220 against the
emulation. Firmware v1.02 (`U220 V1.02 90/02/16`) unless noted. The U-110 analyses in this
folder (from emu110) cover the parts the two units share: the PCM chip, the wave-ROM and
card format, the address and data scrambling.

## Board

| Part | What |
|---|---|
| IC6 | P8098 CPU, 12 MHz, **8-bit bus** (as the CM-32P); no PORT1 |
| IC8 | program EPROM, 128 KB (HN27C101) |
| IC10 | HM62256 SRAM, 32 KB, battery-backed: all patches, timbres, rhythm sets and setup |
| IC12 | MB623157 gate array: address latch, chip selects, A14–A16, LCD bus, key matrix, LED |
| IC17/IC18 | MB87419/MB87420 PCM chip ("Roland LP"), as U-110 and CM-32P; own 32.768 MHz crystal |
| IC19–IC24 | wave ROMs A–F, 512 KB each; E and F are the SN-U110-08 and -09 card contents |
| IC25 | TC23SC140AF-007 effect processor (Roland 15239126) with two M5M4464 DRAMs |
| DAC | PCM56P |
| LCD | 24×2 characters, backlit, HD44780-compatible controller |

## Memory map

- `0x2000–0xFFFF`: the lower 64 KB of the EPROM, fixed. No code banking.
- `0x1000` (IC12) selects what the window `0x0800–0x0FFF` shows; `0x1001` is IC12's second
  register. `0x1002–0x1FFF` is SRAM pages `0E`/`0F`, fixed.

| `0x1000` | Window `0x0800–0x0FFF` |
|---|---|
| `00–0F` | SRAM, page × 2 KB |
| `10` | LCD: `0800` command, `0801` data |
| `30` | key matrix: `080E` (bits 0–7), `080D` (bits 8–13), active low |
| `40` | effect processor IC25 |
| `60` | PCM chip, register n at `0x0800 + n` (CM-32P layout) |
| `70` | a data card; tested at boot (patterns `7F`/`01`/`FF`), nothing fitted |
| `80–9F` | EPROM upper half, `0x10000 + (bank & 1F) × 2 KB`: data and texts |

The firmware loads its selector values from a table in the EPROM at `0x1B256`.

Vectors: HSI0 → `0x2130` (LCD queue), software timer → `0x218B`, serial → `0x224E` (MIDI),
EXTINT → `0x22DD` (PCM chip), reset → `0x2100` → init `0x25BC`, main loop `0x27E7`.

## Keys and card detection

The key matrix has 14 keys: bit 0 EDIT, 1 DATA, 2 EXIT, 3 ENTER, 4 PART/INST ◀◀,
5 PART/INST ▶▶, 8 MARK, 9 JUMP, 10 CURSOR ◀, 11 CURSOR ▶, 12 VALUE ▼, 13 VALUE ▲.

**Bits 6 and 7 of `0x080E` are the PCM card slots' detect lines**, low with a card in
(bit 6 slot 1, bit 7 slot 2). The U-110 reads its card contacts on PORT1, which the 8098
does not have. The firmware scans the two bits with the keys; on a change it shows
"Checking PCM Card..." and reads the card's header (routine `0x91EC`), through the same two
tables as the U-110 (address permutation at `0x1B200`, data permutation at `0x1B100`).

## PCM chip and wave ROMs

- 32.768 MHz ÷ 1024 = 32 kHz, `PCM_FLOAT8_DELTA` samples, envelopes run by the firmware
  through EXTINT.
- The CPU reads wave-ROM and card bytes through voice 0 (the U-110's method): address =
  `(phase >> 14) + 2`, wrapped at 18 bits, plus `(bank & 0x3C00) << 8`.
- Placement in the chip's 4 MB space, descrambled as on the U-110: A–D at `0x000000`,
  `0x100000`, `0x200000`, `0x300000`; E at `0x080000`, F at `0x180000` (the U-110's first two
  card positions); **card slot 1 at `0x280000`, slot 2 at `0x380000`**.
- An envelope segment is written as register `07` (target) **then** `06` (rate). The U-110
  writes `06` then `07` in one 16-bit access, and the chip starts the segment at `07`;
  emulated the U-110 way, every U-220 segment ran at the previous segment's rate. The
  emulation holds `07` until the following `06`.
- The firmware reads each voice's state as a 16-bit word at `0x0980[voice]`, also at odd
  addresses, and needs the bytes at that address. MAME's MCS-96 aligned such accesses to
  even addresses (the partner voice's state; the pitch rose in every release).
  `set_unaligned_ext_words(true)` in the MCS-96 core, enabled by the U-220 only.

## IC25: mixer and routing

Pan and effect sends are IC25's, not the PCM chip's. IC25 is a DSP with 256 program words
of 24 bits, loaded from EPROM `0x1A000` by routine `0xBB10`. Each of the 32 voices has a
block of program words (addresses: table at EPROM `0x1803E`, 6 bytes per voice). On every
note the firmware writes the gain into the low byte and the destination into bits 1–5 of
the middle byte:

| Destination | Bus |
|---|---|
| `0x1E/0x1F` | dry (L/R) |
| `0x12/0x13` | reverb send |
| `0x14/0x15` | chorus send |
| `0x18/0x19` | DIRECT OUT 1 |
| `0x1A/0x1B` | DIRECT OUT 2 |

Gains are linear, `0x1A` = unity. The level goes to entries 0/1 of the voice table and the
destination, read-modify-write, to entries 3/5. `roland_u220_ic25.h` models this; MAME and
the core share it. The PCM chip model gives each voice's output to ten buses through a gain
matrix.

**Outputs.** Per part, Output Assign is Dry/Rev/Cho (→ MIX OUT) or Dir1/Dir2 (→ DIRECT OUT,
no effect); separate outputs come from Assign plus Pan (7< / >7). There is no multi-output
mode. The firmware writes the Dir destinations whether or not a plug is in the jack: a
DIRECT jack without a plug sends its sound on to MIX (analogue switching contacts), without
effect, after the Volume knob.

## Analogue output

Measured against the unfiltered core, all sounding timbres averaged: a gentle high shelf,
RBJ 5.2 kHz, −3.3 dB, Q 0.5, fits within ±0.8 dB from 63 Hz to 14 kHz. The DAC is not
filtered steeply: below 16 kHz each frequency f brings an image at 32 kHz − f (−3 dB at
16.3 kHz to −38 dB at 23.5 kHz). The plugin's "HW-EQ" is the shelf plus these images
(`plugin/ostrich/DacStage.hpp`).

## MIDI and SysEx

- 31250 baud; THRU repeats the input line.
- Model ID `2B`, factory device ID 17 (`10h`). Receiving SysEx needs `Edit > Setup > MIDI`
  Rx SysEx On. There is no memory protect.
- An RQ1 is answered with a DT1. Bulk dumps (`Data > Bulk > All/Setup/Temp/Memory`) are
  DT1 packets of up to 128 data bytes (138 bytes on the wire). A full dump is 247 packets,
  33 398 bytes.
- Units need time between packets: 20 ms after each one's wire time is enough.
- Temporary timbre of part n (individual parameters): `10 10+n−1 00`; tone media at `+18h`
  (0 internal, 1–31 a card by its catalogue number), tone number at `+19h`. A change there
  sounds from the next note.
- Patch effect parameters: bytes 12–19 of the patch as a 64-bit word. Chorus Rate bits 0–4,
  Level 6–10, Depth 11–15, Feedback 23–28 (offset +32), Type 29–31, Delay 32–36, Out 63;
  Reverb Time 16–20, Delay FB 43–47, Level 48–52, Type 60–62. A DT1 to `00 06 00` (the first
  packet of the temp dump) sets them on a running unit.

## Data

- The EPROM holds one patch ("Acoust Piano") and one timbre ("A.Piano 2"); the factory
  patches came on a Factory Preset card (test mode 11). After "Memory Initialize" (and after
  `Data > Init > All` on a real unit) P-02 to P-64 are empty. The factory set comes back by
  SysEx.
- Card IDs are the catalogue numbers (`02` for SN-U110-02); the header is
  `RolandU-110 N` + `B1 53 AC`, the card's name at `0x10`, the ID at `0x20`.
- Card dumps exist in two address orders. Besides MAME's (`hash/u110_card.xml`), a set of
  all 19 cards in that list circulates with address lines A8–A15 reversed (A8↔A15, A9↔A14,
  A10↔A13, A11↔A12); data lines and the other address lines are the same. Put back in
  order, every one matches MAME's SHA-1. SN-U110-08 checks against the U-220's own wave ROM
  E (IC23) byte for byte. The header (`0x00`–`0xFF`) reads the same in both orders. Loaded
  as it is, such a dump shows the tone names as garbage (a black bar), and the parts play
  the wrong samples. The plugin recognises the order by the hash, or else by which order
  gives the first tone records (`0x1000 + 0x50*n`) readable names (27.10.2026).
- After the ROM Play demo songs the firmware reloads the current patch from memory; unsaved
  temp edits are lost, on the unit as in the emulation.
- A pseudo-random generator (`0x1084/0x1085`, seeded 02/00 at power-on, routine `0x93B3`)
  gives some drum sounds (F.X. 69, 86) and timbres (Deep 1) a random pitch per note.

## Menus

`Edit` = Setup / Patch / Timbre / Rythm; `Setup` = M.Tune / Efct / LCD / MIDI / Map;
`Data` = Write / Bulk / Init / Util; `Bulk` and `Init` = All / Setup / Temp / Memory (Init:
Jump Page); `Util` = ROM Play / MIDI Monitor. Menus remember their last choice.

## Panel

Twelve keys, a green MIDI LED, VOLUME, PHONES, two PCM card slots, the backlit 24×2 LCD. The
firmware marks the selected part with the controller's underline cursor.

How the firmware shows what is selected (u220_render with factory data, 27.10.2026):

- Menus and parameter pages: the selected field blinks by being written blank for ~0.3 s
  a second. After a CURSOR press the new field goes blank at once (within ~60 ms) and the old
  one comes back. The firmware writes a field a few characters per display update, so one
  blink takes two or three updates. Fields are separated by the controller's own glyphs
  (codes below 0x10) or by blanks, as in `Prm1=` `Off` `Timbre Level`. CURSOR stops at a
  menu's ends; on parameter pages it goes on to the next or previous page past the last
  field. Neither VALUE nor PART/INST moves the selection.
- Main page: the second row is one of five views (RX, OUT, EFT, PAN, RSV) that CURSOR pages
  through, without wrapping round. The first PART/INST press selects part 1 (first row
  `T-001:`). Each press moves the underline by one part (row 2 columns 5, 8, … 20, and 23
  for the rhythm part, `R-1:`). The underline stays until the page is left.
- Name pages (`Patch Name=`): the underline marks the character, shown for ~150 ms after
  each press; CURSOR moves it one character.

The MIDI LED ("MIDI MESSAGE", above POWER) is not the firmware's: traced while notes came in
(27.10.2026), the firmware writes nothing that follows MIDI IN, neither IC12's register at
`0x1001` (never changes after boot) nor any window bank it has no other use for (only bank
`0x70`, once, during boot). So the lamp is most likely driven from the MIDI IN line itself,
likely through IC12's LED0. Not checked on the board. The plugin lights it for any MIDI it
receives.

## Checked against the real unit

- All 128 timbres at C4: level within ±0.5 dB after a common offset, envelopes and pitch
  (vibrato, release) the same; drums the same, apart from F.X. 35/69/86 (random pitch, see
  above).
- A.Piano 2 pitch 262.8 Hz in the plugin, 262.5 Hz on the unit, stable to −73 dB in the
  release.
- Drum panorama within 0.8 dB. A MIDI track played to the unit and the plugin at once,
  one of them phase-inverted, largely cancels.
- Individual units differ a little: recordings of two U-220s, decades apart, already do in
  some drums.
