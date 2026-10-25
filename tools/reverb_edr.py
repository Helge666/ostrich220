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

    hardware:  HW - g * EMU_DRY      (g and the latency from the first, reverb-free slot)
    Ostrich:   g * (EMU_WET - EMU_DRY)

and both go through the same octave filters and time windows after each hit.  Every cell
is the Ostrich level minus the hardware level in dB.  What the subtraction leaves behind
on the hardware side -- the dry sound not cancelling exactly -- is measured on the first
slot (reverb level 0) and is the floor: a cell whose hardware level is less than 6 dB
above it is not trusted and shows as "." (and is left out of the JSON fit targets).

Per slot also the decay time per band (from the band's own energy decay between -5 and
-25 dB, only where it stays above the floor), for both.  Needs numpy and scipy.
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


def load(path):
    w = wave.open(path)
    x = np.frombuffer(w.readframes(w.getnframes()), '<i2').reshape(-1, w.getnchannels())
    return x.astype(np.float64) / 32768, w.getframerate()


def band_energy(x, sr):
    """Octave-band energy envelope of a stereo signal (L + R energy), per band."""
    out = []
    for fc in BANDS:
        lo, hi = fc / np.sqrt(2), min(fc * np.sqrt(2), 0.45 * sr)
        sos = butter(3, [lo, hi], btype='band', fs=sr, output='sos')
        y = sosfiltfilt(sos, x, axis=0)
        out.append((y ** 2).sum(axis=1))
    return np.array(out)


def windows_db(env, sr, t0):
    cells = np.empty((len(BANDS), len(WINDOWS)))
    for j, (a, b) in enumerate(WINDOWS):
        seg = env[:, int((t0 + a) * sr):int((t0 + b) * sr)]
        cells[:, j] = 10 * np.log10(seg.mean(axis=1) + 1e-20)
    return cells


def decay_time(env, sr, t0, floor_db):
    """T20 per band, x3: the backward integral from the hit, between -5 and -25 dB,
    cut where the band sinks to its floor."""
    out = []
    for k in range(len(BANDS)):
        e = env[k, int((t0 + 0.02) * sr):int((t0 + 4.8) * sr)]
        # Smooth to 10 ms and drop what is under the floor plus the margin.
        n = int(0.01 * sr)
        e = np.convolve(e, np.ones(n) / n, 'same')
        lev = 10 * np.log10(e + 1e-20)
        ok = np.nonzero(lev < floor_db[k] + TRUST_DB)[0]
        e = e[:ok[0]] if len(ok) else e
        if len(e) < n * 5:
            out.append(float('nan'))
            continue
        c = np.cumsum(e[::-1])[::-1]
        edc = 10 * np.log10(c / c[0] + 1e-20)
        i5, i25 = np.argmax(edc < -5), np.argmax(edc < -25)
        out.append(3 * (i25 - i5) / sr if i25 > i5 else float('nan'))
    return out


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

    hw, sr = load(a.hw)
    dry, esr = load(a.dry)
    wet, _ = load(a.wet)
    if esr != sr:
        dry = resample_poly(dry, sr, esr, axis=0)
        wet = resample_poly(wet, sr, esr, axis=0)
    rows = [l.rstrip('\n').split('\t') for l in open(a.list) if l.strip()]

    # Latency and level from the first slot, which has no reverb.
    h0 = float(rows[0][0])
    x = hw[int((a.hw_lead + h0 - 0.1) * sr):int((a.hw_lead + h0 + 0.4) * sr)].mean(1)
    y = dry[int((a.emu_offset + h0 - 0.1) * sr):int((a.emu_offset + h0 + 0.4) * sr)].mean(1)
    lag = np.argmax(fftconvolve(x, y[::-1])) - (len(y) - 1)
    hw_off = a.hw_lead + lag / sr
    g = np.sqrt((x ** 2).sum() / (y ** 2).sum())
    print(f'hardware latency {lag / sr * 1000:.1f} ms, Ostrich x {g:.3f} ({20 * np.log10(g):+.1f} dB) to match')

    span = a.slot - 0.2

    def cut(h):
        i = int((hw_off + h - 0.1) * sr)
        j = int((a.emu_offset + h - 0.1) * sr)
        n = int(span * sr)
        if i + n > len(hw) or j + n > len(dry):
            return None
        hr = hw[i:i + n] - g * dry[j:j + n]
        er = g * (wet[j:j + n] - dry[j:j + n])
        return band_energy(hr, sr), band_energy(er, sr)

    first = cut(h0)
    floor = windows_db(first[0], sr, 0.1)
    floor_db = np.median(floor[:, 2:], axis=1)
    print('floor (hw - dry, reverb level 0), dB per band:',
          ' '.join(f'{b}:{v:.0f}' for b, v in zip(BANDS, floor_db)))

    results = []
    hdr = 'window ' + ' '.join(f'{f"{a_*1000:.0f}-{b_*1000:.0f}":>9s}' for a_, b_ in WINDOWS)
    for hit, label in rows[1:]:
        if a.only and a.only not in label:
            continue
        c = cut(float(hit))
        if c is None:
            continue
        he, ee = c
        hc, ec = windows_db(he, sr, 0.1), windows_db(ee, sr, 0.1)
        trust = hc > floor + TRUST_DB
        diff = np.where(trust, ec - hc, np.nan)
        rt_h = decay_time(he, sr, 0.1, floor_db)
        rt_e = decay_time(ee, sr, 0.1, -200 * np.ones(len(BANDS)))
        rms = np.sqrt(np.nanmean(diff ** 2)) if trust.any() else float('nan')
        results.append(dict(label=label, diff=np.where(trust, diff, None).tolist(),
                            hw=hc.tolist(), emu=ec.tolist(), trust=trust.tolist(),
                            rt_hw=rt_h, rt_emu=rt_e, rms=rms))
        # One line per slot: per band, the error over the trusted windows (mean), and RT.
        band_mean = [np.nanmean(diff[k]) if trust[k].any() else np.nan for k in range(len(BANDS))]
        print(f'{label:28s} rms {rms:5.1f} dB | band ' +
              ' '.join('    .' if np.isnan(v) else f'{v:+5.1f}' for v in band_mean) +
              ' | RT hw/emu ' + ' '.join(
                  ('  .  ' if np.isnan(p) else f'{p:.2f}') + '/' + ('.' if np.isnan(q) else f'{q:.2f}')
                  for p, q in zip(rt_h, rt_e)))
        if a.detail:
            print('   ' + hdr)
            for k, fc in enumerate(BANDS):
                print(f'   {fc:5d} ' + ' '.join(
                    '        .' if not trust[k, j] else f'{diff[k, j]:+9.1f}' for j in range(len(WINDOWS))))

    if a.json:
        with open(a.json, 'w') as f:
            json.dump(dict(bands=BANDS, windows=WINDOWS, floor=floor.tolist(), gain=g,
                           slots=results), f, indent=1,
                      default=lambda v: None if isinstance(v, float) and np.isnan(v) else v)


if __name__ == '__main__':
    main()
