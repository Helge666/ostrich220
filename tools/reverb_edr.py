#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Compare the reverb of the U-220 and of Ostrich band by band and over time -- an energy
decay relief (EDR) in octave bands -- from one make_reverb_probe.py run on both.

    tools/reverb_edr.py HW.wav EMU_DRY.wav EMU_WET.wav LIST.txt [--emu-offset 4.0]
        [--json OUT.json] [--only SUBSTRING] [--detail]

HW.wav is the hardware recording (hwcapture_asio.py), EMU_DRY/EMU_WET the same MIDI
rendered by u220_render without and with the reverb module (same times file, so the
excitation is identical).  The reverb alone is then

    hardware:  HW - g * EMU_DRY
    Ostrich:   g * (EMU_WET - EMU_DRY)

and both go through the same octave filters and time windows after each hit.  Every cell
is the Ostrich level minus the hardware level in dB.

The MIDI path to the hardware jitters by +-1.5 ms from hit to hit, which is enough to
leave the dry sound standing in the subtraction.  So every slot is aligned on its own,
to a fraction of a sample, on the first 150 ms (where the dry sound dominates), and the
Ostrich side of that slot gets the same shift.  What the subtraction still leaves is
measured on the first slot (reverb level 0) and is the floor: a cell whose hardware
level is less than 6 dB above it is not trusted and shows as "." (and is left out of
the fit targets).

Per slot also the decay time per band (from the band's own energy decay between -5 and
-25 dB, only where it stays above the floor), for both.  Everything runs at the
emulation's 32 kHz.  Needs numpy and scipy.  tools/reverb_fit.py imports Probe.
"""

import argparse
import json
import wave

import numpy as np
from scipy.signal import butter, fftconvolve, resample_poly, sosfiltfilt

BANDS = [125, 250, 500, 1000, 2000, 4000, 8000]
WINDOWS = [(0.00, 0.05), (0.05, 0.15), (0.15, 0.30), (0.30, 0.60), (0.60, 1.20),
           (1.20, 2.40), (2.40, 4.80)]
TRUST_DB = 6.0
SR = 32000
PRE = 0.1           # seconds of each cut before the hit


def load(path):
    w = wave.open(path)
    x = np.frombuffer(w.readframes(w.getnframes()), '<i2').reshape(-1, w.getnchannels())
    return x.astype(np.float64) / 32768, w.getframerate()


_SOS = [butter(3, [fc / np.sqrt(2), min(fc * np.sqrt(2), 0.45 * SR)], btype='band', fs=SR,
               output='sos') for fc in BANDS]


def band_energy(x):
    """Octave-band energy envelope of a stereo signal (L + R energy), per band."""
    return np.array([(sosfiltfilt(s, x, axis=0) ** 2).sum(axis=1) for s in _SOS])


def windows_db(env):
    cells = np.empty((len(BANDS), len(WINDOWS)))
    for j, (a, b) in enumerate(WINDOWS):
        seg = env[:, int((PRE + a) * SR):int((PRE + b) * SR)]
        cells[:, j] = 10 * np.log10(seg.mean(axis=1) + 1e-20)
    return cells


def decay_time(env, floor_db):
    """T20 per band, x3: the backward integral from the hit, between -5 and -25 dB,
    cut where the band sinks to its floor."""
    out = []
    n = int(0.01 * SR)
    for k in range(len(BANDS)):
        e = np.convolve(env[k, int((PRE + 0.02) * SR):int((PRE + 4.8) * SR)], np.ones(n) / n, 'same')
        lev = 10 * np.log10(e + 1e-20)
        low = np.nonzero(lev < floor_db[k] + TRUST_DB)[0]
        e = e[:low[0]] if len(low) else e
        if len(e) < n * 5:
            out.append(float('nan'))
            continue
        c = np.cumsum(e[::-1])[::-1]
        edc = 10 * np.log10(c / c[0] + 1e-20)
        i5, i25 = np.argmax(edc < -5), np.argmax(edc < -25)
        out.append(3 * (i25 - i5) / SR if i25 > i5 else float('nan'))
    return out


def frac_shift(x, d):
    """Delay a (n, ch) signal by d samples (fractional), circularly; callers cut with margin."""
    n = len(x)
    f = np.fft.rfftfreq(n)
    return np.fft.irfft(np.fft.rfft(x, axis=0) * np.exp(-2j * np.pi * f * d)[:, None], n, axis=0)


class Probe:
    """The hardware side of one probe run, aligned slot by slot against the emulation's
    dry render; then emu_cells() measures any Ostrich reverb output the same way."""

    def __init__(self, hw_path, dry_path, list_path, emu_offset=4.0, hw_lead=0.5, slot=6.0):
        hw, sr = load(hw_path)
        if sr != SR:
            hw = resample_poly(hw, SR, sr, axis=0)
        dry, sr = load(dry_path)
        assert sr == SR, 'the emulation renders at 32 kHz'
        self.rows = [l.rstrip('\n').split('\t') for l in open(list_path) if l.strip()]
        self.emu_offset = emu_offset
        self.n = int((slot - 0.2) * SR)

        # Global level and a first latency from the reverb-free first slot.
        h0 = float(self.rows[0][0])
        x = hw[int((hw_lead + h0 - 0.1) * SR):int((hw_lead + h0 + 0.4) * SR)].mean(1)
        y = dry[int((emu_offset + h0 - 0.1) * SR):int((emu_offset + h0 + 0.4) * SR)].mean(1)
        lag0 = np.argmax(fftconvolve(x, y[::-1])) - (len(y) - 1)
        self.g = np.sqrt((x ** 2).sum() / (y ** 2).sum())
        self.latency = hw_lead + lag0 / SR

        self.slots = []
        for hit, label in self.rows:
            h = float(hit)
            j = int((emu_offset + h - PRE) * SR)
            i = int((self.latency + h - PRE) * SR)
            if i + self.n + 400 > len(hw) or j + self.n + 400 > len(dry):
                continue
            # This slot's own offset, to a fraction of a sample, on the dry attack.
            a = hw[i:i + int((PRE + 0.15) * SR)].mean(1)
            b = dry[j:j + int((PRE + 0.15) * SR)].mean(1)
            c = fftconvolve(a, b[::-1])
            k = int(np.argmax(c[len(b) - 1 - 200:len(b) - 1 + 200])) + len(b) - 1 - 200
            fr = 0.5 * (c[k - 1] - c[k + 1]) / (c[k - 1] - 2 * c[k] + c[k + 1])
            d = (k - (len(b) - 1)) + fr          # hw lags the emulation by d samples here
            self.slots.append(dict(label=label, hit=h, i=i, j=j, d=d))
        self.dry = dry
        self.hw = hw

        for s in self.slots:
            hseg = self.hw[s['i']:s['i'] + self.n]
            dseg = self._emu_seg(self.dry, s)
            s['hw_env'] = band_energy(hseg - self.g * dseg)
            s['hw_db'] = windows_db(s['hw_env'])
        self.floor = self.slots[0]['hw_db']
        self.floor_db = np.median(self.floor[:, 2:], axis=1)
        for s in self.slots:
            s['trust'] = s['hw_db'] > self.floor + TRUST_DB
            s['rt_hw'] = decay_time(s['hw_env'], self.floor_db)

    def _emu_seg(self, x, s):
        """The emulation's samples for slot s, shifted onto the hardware's time line."""
        m = 400
        seg = x[s['j'] - m:s['j'] + self.n + m]
        return frac_shift(seg, s['d'])[m:m + self.n]

    def emu_cells(self, rev, s):
        """Window levels (dB) and decay times of an Ostrich reverb-only signal (32 kHz,
        stereo, output stage applied, emulation time line) for slot s."""
        env = band_energy(self.g * self._emu_seg(rev, s))
        return windows_db(env), decay_time(env, -200 * np.ones(len(BANDS)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('hw')
    ap.add_argument('dry')
    ap.add_argument('wet')
    ap.add_argument('list')
    ap.add_argument('--emu-offset', type=float, default=4.0)
    ap.add_argument('--hw-lead', type=float, default=0.5)
    ap.add_argument('--slot', type=float, default=6.0)
    ap.add_argument('--json')
    ap.add_argument('--only')
    ap.add_argument('--detail', action='store_true')
    a = ap.parse_args()

    p = Probe(a.hw, a.dry, a.list, a.emu_offset, a.hw_lead, a.slot)
    wet, sr = load(a.wet)
    rev = wet - p.dry
    print(f'hardware latency {p.latency * 1000:.1f} ms (per-slot jitter '
          f'{np.std([s["d"] for s in p.slots]) / SR * 1000:.2f} ms rms), '
          f'Ostrich x {p.g:.3f} ({20 * np.log10(p.g):+.1f} dB) to match')
    print('floor (hw - dry, reverb level 0), dB per band:',
          ' '.join(f'{b}:{v:.0f}' for b, v in zip(BANDS, p.floor_db)))

    results = []
    hdr = 'window ' + ' '.join(f'{f"{a_*1000:.0f}-{b_*1000:.0f}":>9s}' for a_, b_ in WINDOWS)
    for s in p.slots[1:]:
        if a.only and a.only not in s['label']:
            continue
        ec, rt_e = p.emu_cells(rev, s)
        hc, trust, rt_h = s['hw_db'], s['trust'], s['rt_hw']
        diff = np.where(trust, ec - hc, np.nan)
        rms = np.sqrt(np.nanmean(diff ** 2)) if trust.any() else float('nan')
        results.append(dict(label=s['label'], diff=np.where(trust, diff, None).tolist(),
                            hw=hc.tolist(), emu=ec.tolist(), trust=trust.tolist(),
                            rt_hw=rt_h, rt_emu=rt_e, rms=rms))
        band_mean = [np.nanmean(diff[k]) if trust[k].any() else np.nan for k in range(len(BANDS))]
        print(f'{s["label"]:28s} rms {rms:5.1f} dB | band ' +
              ' '.join('    .' if np.isnan(v) else f'{v:+5.1f}' for v in band_mean) +
              ' | RT hw/emu ' + ' '.join(
                  ('  .  ' if np.isnan(x) else f'{x:.2f}') + '/' + ('.' if np.isnan(y) else f'{y:.2f}')
                  for x, y in zip(rt_h, rt_e)))
        if a.detail:
            print('   ' + hdr)
            for k, fc in enumerate(BANDS):
                print(f'   {fc:5d} ' + ' '.join(
                    '        .' if not trust[k, j] else f'{diff[k, j]:+9.1f}' for j in range(len(WINDOWS))))

    if a.json:
        with open(a.json, 'w') as f:
            json.dump(dict(bands=BANDS, windows=WINDOWS, floor=p.floor.tolist(), gain=p.g,
                           slots=results), f, indent=1,
                      default=lambda v: None if isinstance(v, float) and np.isnan(v) else v)


if __name__ == '__main__':
    main()
