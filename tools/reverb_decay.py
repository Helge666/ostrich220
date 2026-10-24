#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Per setting of a make_reverb_probe.py recording: how loud the tail is and how fast it
decays, so it shows at a glance whether the reverb settings took effect at all.

    tools/reverb_decay.py REC.wav LIST.txt [--offset 0.54] [--slot 6.0]

LIST.txt is the --list output (hit time, label).  `offset` is where time 0 of the MIDI
file sits in the recording (hwcapture_asio: lead 0.5 s plus ~40 ms latency).  For each
hit: the level of the first 100 ms (the direct sound), the tail level 0.3-0.6 s after
the hit relative to it, and T20 -- the time from -5 to -25 dB on the Schroeder curve,
times 3, i.e. an RT60 estimate.  Needs numpy.
"""

import argparse
import wave

import numpy as np


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('wav')
    ap.add_argument('list')
    ap.add_argument('--offset', type=float, default=0.54)
    ap.add_argument('--slot', type=float, default=6.0)
    a = ap.parse_args()

    w = wave.open(a.wav)
    sr, ch = w.getframerate(), w.getnchannels()
    x = np.frombuffer(w.readframes(w.getnframes()), '<i2').reshape(-1, ch).astype(np.float64) / 32768
    mono = x.mean(axis=1)
    rows = [l.rstrip('\n').split('\t') for l in open(a.list) if l.strip()]

    print(f'{"setting":32s} direct dB  tail/direct dB   RT60 s')
    for hit, label in rows:
        t0 = float(hit) + a.offset
        i0 = int(t0 * sr)
        seg = mono[i0:i0 + int((a.slot - 1.2) * sr)]
        if len(seg) < sr:
            continue
        direct = 10 * np.log10(np.mean(seg[:int(0.1 * sr)] ** 2) + 1e-20)
        tail = 10 * np.log10(np.mean(seg[int(0.3 * sr):int(0.6 * sr)] ** 2) + 1e-20)
        e = np.cumsum((seg ** 2)[::-1])[::-1]
        edc = 10 * np.log10(e / e[0] + 1e-20)
        try:
            t5 = np.argmax(edc < -5) / sr
            t25 = np.argmax(edc < -25) / sr
            rt = 3 * (t25 - t5) if t25 > t5 else float('nan')
        except ValueError:
            rt = float('nan')
        print(f'{label:32s} {direct:8.1f}  {tail - direct:12.1f}   {rt:7.2f}')


if __name__ == '__main__':
    main()
