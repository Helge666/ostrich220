# Roland U-220 — effects

The U-220's reverb/delay and chorus run in IC25 (TC23SC140AF-007, two M5M4464 DRAMs, about
1 s of delay memory at 32 kHz). The same chip is in the CM-32P/CM-64, D-70, KR-55 and
Rhodes 760. Its program is not understood, so Ostrich 220 does not emulate it: it models the
effects from measurements of a real U-220. The models sit behind `plugin/core/u220_effects.h`
(effect type and parameters in, stereo buses in and out), to be replaced by an emulation
of the chip if that ever exists.

## What the unit has

The effects belong to the **patch**; `Edit > Setup > Efct` has a global on/off each for
chorus and reverb.

| Block | Types | Parameters |
|---|---|---|
| Reverb/Delay | Room1, Room2, Room3, Hall1, Hall2, Gate, Delay, CrossDelay | Time 0–31, Level 0–31, Delay FB 0–31 |
| Chorus | Chorus1, Chorus2, FB-Chorus, Flanger, Short Delay | Out = Pre Rev / Post Rev, Level, Delay Time, Rate, Depth 0–31, Feedback ±31 |

- Pre Rev: the chorus feeds the reverb (the reverb gets the chorused sound). Post Rev: no
  reverb on chorus parts (Owner's Manual pp. 54, 61).
- MIDI controllers can be assigned in the patch to Chorus Level, Rate and Feedback, Reverb
  Level and Delay Feedback.
- The core reads the parameters from RAM: the temp patch's effect bytes at `0x143C`, the
  global switches at `0x1401` bits 4/5. Where they sit in the patch: U220-HARDWARE.md, MIDI.

## Measuring

The unit is recorded through an audio interface while the same MIDI file plays it and the
emulation (`tools/hwcapture/`). The reverb is isolated by subtracting a dry render of the
emulation from the recording; excitation is short drums (Kick 2, Rim Shot, Closed Hi-Hat),
whose bands are 45 dB down after ~110 ms. The temp patch is sent first in short packets, so
that both play the same data. Each slot is aligned to a fraction of a sample (the MIDI path
jitters by ±1.5 ms).

- `tools/make_reverb_probe.py`: the MIDI sequences (`--v2`, `--levels`, `--prepost`).
- `tools/reverb_edr.py`: energy decay relief in octave bands 125 Hz–8 kHz × time windows
  0–4.8 s; cells within 6 dB of the noise floor are dropped.
- `tools/reverb_fit.py`: fits the model's tuning table against those cells, rendering only
  the reverb from recorded send buses (`u220_reverb_run`), about 17 s per iteration.
- `tools/make_chorus_probe.py`, `tools/chorus_analyse.py`: a held sawtooth (T-060 Saw Wave 1,
  vibrato off) through the chorus only; delay, gain and feedback read at the one steep edge
  per 30.6 ms loop.
- `tools/make_ab.py`: numbered A/B pairs (unit, then emulation) for listening.

## Reverb and delay model

`plugin/core/u220_reverb.{h,cpp}`, tuning `u220_reverb_tuning.inc`.

- Room1–3, Hall1–2: feedback delay network, 8 lines and 4 diffusers; per type and Time,
  an octave output EQ and decay times low/mid/high (shelving damping in each line).
- Gate: the same network, gated; on the unit not a hard cut but flat for ~100–200 ms
  (depending on Time), then about 250 dB/s.
- Delay / CrossDelay: an echo line, 10 + 12.9 ms × Time (CrossDelay: right at half the
  period). At FB 31 almost lossless (0.97 per repeat).
- Level: the tail falls about 0.8 dB per step below Level 31 (25: −4.9, 20: −8.2, 15: −11.9,
  10: −16.2, 5: −22.1 dB); a table in dB.

Reverb time on the unit (RT60, Time 0 → 31): Room1 1.1 → 2.3 s, Room2 1.7 → 4.4 s, Room3
1.4 → 3.5 s, Hall1 2.7 → 6.6 s, Hall2 2.5 → 6.4 s, Gate ~0.8 s.

Fit: rms error over all bands and windows 1.5 dB (Room3/Hall1/Hall2 ~1 dB, Room2 ~1.7,
Room1 ~2.3 — its tail does not decay exponentially, the late bass is very long — Gate ~2.5,
delays < 0.6 dB). Listening: Room1, Hall1, Gate and Delay identical or nearly; Room3 and
CrossDelay nearly; Room2 and Hall2 raised 1 dB by ear.

## Chorus model

`plugin/core/u220_chorus.{h,cpp}`; with the reverb in `U220Fx` (`u220_fx.h`).

- One delayed copy per side. Minimum delay per Delay step: Chorus1 and FB-Chorus 16.0 +
  1.03 ms, Chorus2 8.0 + 0.516 ms, Flanger 0.06 + 0.516 ms, Short Delay 0.5 + 2.05 ms (no LFO).
- LFO: a triangle of fixed speed between 0 and a span; left and right in opposite phase.
  **Rate** (bits 0–4 of its field) sets the span, 16.0 − 0.5 ms per step (higher: narrower
  and faster); **Depth** (bits 11–15) sets the speed: 0.24 / 4.1 / 7.3 / 16.7 / 31.6 /
  60.1 ms/s at 0 / 5 / 8 / 16 / 24 / 31. Rate = speed / (2 × span). At Depth 0 the unit
  parks the LFO near the top (left 0.6 ms under the longest delay, right 0.6 ms over the
  shortest) and wobbles 0.87 ms at 0.26 Hz.
- Gain 0.94 at Level 31 (Short Delay 0.99), about 0.7 dB less per step.
- Feedback 0.027 per step; FB-Chorus and Flanger right only, Short Delay both. Both sides
  read one line that recirculates from the right tap, so on the left the recirculation sits
  at τL + τR.
- Chorus2 has a second copy as loud as the first: a triangle over the same span, 3.3× as
  fast, 12 ms under the shortest main delay, left and right in opposite phase (on the other
  types it is 14–16 dB down). Modelled broadband at 0.8, fitted to a bass line.
- With Out = Pre Rev the right channel of the chorus parts (dry and chorus) comes one sample
  (31 µs) later, for Chorus1 and Chorus2.

Accuracy against the analysis of the same recordings: minimum delay ±0.3 ms, span < 1 %,
rate 2–3 %, gain ±0.02, feedback ±0.03. In listening tests of 53 pairs the differences that
remained were fixed; Chorus2 "nearly indistinguishable" on a bass line.

## Open

- What the unit does with parts assigned to Cho while the chorus is switched off.
- Room1's stereo image (correlation −0.29 in the model, about 0 on the unit).
- The chip itself: its program words are written from EPROM `0x1A000` at boot, and the CPU
  writes some 4000 values per second even when idle (coefficients, LFOs?). An analysis of
  that program would allow an emulation.
