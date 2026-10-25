#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Cut a probe recording of the U-220 and Ostrich's render of the same MIDI into one pair of
WAV files per setting, numbered, for listening A/B without a DAW:

    NN_<setting>_U220.wav   NN_<setting>_Ostrich.wav

    tools/make_ab.py HW.wav EMU.wav LIST.txt OUTDIR [--emu-offset 4.0] [--lead 0.5]
        [--post 1.0]

LIST.txt is the probe's --list: "on<TAB>hold<TAB>label" (make_chorus_probe.py) or
"hit<TAB>label" (make_reverb_probe.py, then --hold sets how much to keep).  Ostrich is
resampled to the recording's rate, level-matched on the first slot (its dry reference)
and aligned to the hardware slot by slot (the MIDI path jitters).  Each file runs from
0.2 s before the note to `post` seconds after its end.  Needs numpy and scipy.
"""

import argparse
import os
import re
import wave

import numpy as np
from scipy.signal import fftconvolve, resample_poly


def load(path):
    w = wave.open(path)
    x = np.frombuffer(w.readframes(w.getnframes()), '<i2').reshape(-1, w.getnchannels())
    return x.astype(np.float64) / 32768, w.getframerate()


def save(path, x, sr):
    w = wave.open(path, 'wb')
    w.setnchannels(2)
    w.setsampwidth(2)
    w.setframerate(sr)
    w.writeframes((np.clip(x, -1, 1) * 32767).astype('<i2').tobytes())
    w.close()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('hw')
    ap.add_argument('emu')
    ap.add_argument('list')
    ap.add_argument('outdir')
    ap.add_argument('--emu-offset', type=float, default=4.0)
    ap.add_argument('--lead', type=float, default=0.5)
    ap.add_argument('--hold', type=float, default=5.0)
    ap.add_argument('--post', type=float, default=1.0)
    a = ap.parse_args()

    hw, sr = load(a.hw)
    emu, esr = load(a.emu)
    if esr != sr:
        emu = resample_poly(emu, sr, esr, axis=0)
    rows = []
    for line in open(a.list):
        f = line.rstrip('\n').split('\t')
        if len(f) == 3:
            rows.append((float(f[0]), float(f[1]), f[2]))
        elif len(f) == 2:
            rows.append((float(f[0]), a.hold, f[1]))

    def align(t):
        """Hardware sample offset of slot time t, from the cross-correlation of the first
        300 ms against Ostrich's."""
        i = int((a.lead + t - 0.1) * sr)
        j = int((a.emu_offset + t - 0.1) * sr)
        x = hw[i:i + int(0.4 * sr)].mean(1)
        y = emu[j:j + int(0.3 * sr)].mean(1)
        c = fftconvolve(x, y[::-1], 'valid')
        return i + int(np.argmax(np.abs(c))) - j

    # Level: the first slot, steady part, both sides together.
    t0, h0, _ = rows[0]
    d = align(t0)
    j = int((a.emu_offset + t0 + 0.3) * sr)
    n = int(max(h0 - 0.4, 0.5) * sr)
    g = np.sqrt((hw[j + d:j + d + n] ** 2).sum() / ((emu[j:j + n] ** 2).sum() + 1e-20))
    os.makedirs(a.outdir, exist_ok=True)
    print(f'Ostrich x {g:.3f} ({20 * np.log10(g):+.1f} dB) to the recording\'s level')

    for k, (t, hold, label) in enumerate(rows, 1):
        d = align(t)
        j = int((a.emu_offset + t - 0.2) * sr)
        n = int((hold + 0.2 + a.post) * sr)
        if j + d + n > len(hw) or j + n > len(emu):
            continue
        name = re.sub(r'[^A-Za-z0-9+-]+', '_', label).strip('_')
        save(os.path.join(a.outdir, f'{k:02d}_{name}_U220.wav'), hw[j + d:j + d + n], sr)
        save(os.path.join(a.outdir, f'{k:02d}_{name}_Ostrich.wav'), g * emu[j:j + n], sr)
    with open(os.path.join(a.outdir, 'index.txt'), 'w') as f:
        for k, (t, hold, label) in enumerate(rows, 1):
            f.write(f'{k:02d}  {label}\n')
    print(f'{len(rows)} pairs in {a.outdir}')


if __name__ == '__main__':
    main()
