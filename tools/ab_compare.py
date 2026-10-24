#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Compare a hardware take and an emulation render of tools/make_timbre_ab.py, slot by slot.

    tools/ab_compare.py HW.wav EMU.wav [--hw-first 1.5] [--emu-first 11.0] [--period 1.6]
        [--count 128] [--names names.txt] [--cut-dir DIR]

`*-first` is the time of the first note in each file (hwcapture_asio: lead + LEAD; MAME
-min: 10 s + LEAD).  The offset is then refined by cross-correlating the level envelopes.
Per slot: level (dB), spectral distance after level matching, brightness (centroid) ratio,
and pitch in 50 ms windows from attack to -50 dB, as median and worst deviation from the
slot's own reference in cents.  The overall level/brightness offset is removed before
judging, and slots that stand out are marked.  --cut-dir writes each marked slot as
hw/emu stereo WAV pair for listening.  Needs numpy.
"""

import argparse
import wave

import numpy as np


def load(path):
    w = wave.open(path)
    sr, ch = w.getframerate(), w.getnchannels()
    x = np.frombuffer(w.readframes(w.getnframes()), '<i2').reshape(-1, ch).astype(np.float32) / 32768
    return x.mean(axis=1), x, sr


def env(x, sr, hop=0.01):
    n = int(sr * hop)
    m = len(x) // n
    return np.sqrt((x[:m * n].reshape(m, n) ** 2).mean(axis=1) + 1e-12)


def f0_track(x, sr, win=0.05):
    n = int(sr * win)
    out = []
    for i in range(0, len(x) - 2 * n, n):
        seg = x[i:i + 2 * n] - x[i:i + 2 * n].mean()
        e = np.sqrt((seg ** 2).mean())
        lo, hi = int(sr / 1200), int(sr / 50)
        spec = np.fft.rfft(seg * np.hanning(len(seg)), 4 * len(seg))
        ac = np.fft.irfft(np.abs(spec) ** 2)[:hi + 1]
        if ac[0] <= 0:
            out.append((e, np.nan))
            continue
        ac = ac / ac[0]
        k = lo + int(np.argmax(ac[lo:hi]))
        if ac[k] < 0.5:
            out.append((e, np.nan))
            continue
        a, b, c = ac[k - 1], ac[k], ac[k + 1]
        d = 0.5 * (a - c) / (a - 2 * b + c) if (a - 2 * b + c) else 0
        out.append((e, sr / (k + d)))
    return out


def spectrum(x, sr):
    n = 8192
    s = np.abs(np.fft.rfft(x[:n] * np.hanning(min(n, len(x))), n)) + 1e-9
    f = np.fft.rfftfreq(n, 1 / sr)
    edges = np.geomspace(60, 16000, 33)
    bands = np.array([s[(f >= lo) & (f < hi)].mean() for lo, hi in zip(edges[:-1], edges[1:])])
    cen = (s * f).sum() / s.sum()
    return 20 * np.log10(bands), cen


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('hw')
    ap.add_argument('emu')
    ap.add_argument('--hw-first', type=float, default=1.5)
    ap.add_argument('--emu-first', type=float, default=11.0)
    ap.add_argument('--period', type=float, default=1.6)
    ap.add_argument('--count', type=int, default=128)
    ap.add_argument('--names')
    ap.add_argument('--cut-dir')
    a = ap.parse_args()

    hw, hw2, sr = load(a.hw)
    emu, emu2, sr2 = load(a.emu)
    assert sr == sr2, 'sample rates differ'
    names = open(a.names, encoding='utf-8').read().splitlines() if a.names else []

    # refine the offset on the first 20 s of level envelopes
    span = int(20 / 0.01)
    eh = np.log(env(hw[int((a.hw_first - 0.5) * sr):], sr)[:span])
    ee = np.log(env(emu[int((a.emu_first - 0.5) * sr):], sr)[:span])
    eh, ee = eh - eh.mean(), ee - ee.mean()
    lags = range(-20, 21)
    best = max(lags, key=lambda L: np.dot(eh[max(0, L):span + min(0, L)], ee[max(0, -L):span - max(0, L)]))
    hw_first = a.hw_first + best * 0.01
    print(f'hw offset refined by {best * 10:+d} ms')

    rows = []
    for k in range(a.count):
        def cut(x, first):
            i = int((first + k * a.period - 0.02) * sr)
            return x[i:i + int((a.period - 0.05) * sr)]
        h, e = cut(hw, hw_first), cut(emu, a.emu_first)
        lh, le = 20 * np.log10(np.sqrt((h ** 2).mean()) + 1e-9), 20 * np.log10(np.sqrt((e ** 2).mean()) + 1e-9)
        sh, ch = spectrum(h[int(0.05 * sr):], sr)
        se, ce = spectrum(e[int(0.05 * sr):], sr)
        d = (sh - sh.max()) - (se - se.max())
        pitch = []
        for x in (h, e):
            tr = f0_track(x, sr)
            peak = max(t[0] for t in tr)
            fs = [f for en, f in tr if en > peak * 10 ** (-50 / 20) and not np.isnan(f)]
            pitch.append(np.array(fs))
        ph, pe = pitch
        if len(ph) > 2 and len(pe) > 2:
            ref = np.median(ph)
            cents_med = 1200 * np.log2(np.median(pe) / ref)
            drift_h = np.max(np.abs(1200 * np.log2(ph / np.median(ph))))
            drift_e = np.max(np.abs(1200 * np.log2(pe / np.median(pe))))
        else:
            cents_med = drift_h = drift_e = np.nan
        eh_, ee_ = env(h, sr), env(e, sr)
        m = min(len(eh_), len(ee_))
        envc = np.corrcoef(np.log(eh_[:m]), np.log(ee_[:m]))[0, 1]
        rows.append(dict(k=k, lh=lh, le=le, sd=d, cen=ce / ch, cm=cents_med, dh=drift_h, de=drift_e, envc=envc))

    dl = np.array([r['le'] - r['lh'] for r in rows])
    off = np.median(dl)
    sdm = np.median([r['sd'] for r in rows], axis=0)        # common spectral tilt (output stage)
    cenm = np.median([r['cen'] for r in rows])
    print(f'median level emu-hw {off:+.1f} dB, median brightness ratio {cenm:.2f}')
    print(' T-   name           hw dB  emu-hw  spec  bright  pitch(c) drift hw/emu  env   flags')
    marked = []
    for r, l in zip(rows, dl):
        spec = np.sqrt(((r['sd'] - sdm) ** 2).mean())
        flags = []
        if r['lh'] < -60:
            flags.append('HW-STILL')
        if r['le'] < -60:
            flags.append('EMU-STILL')
        if abs(l - off) > 4:
            flags.append('LEVEL')
        if spec > 6:
            flags.append('SPECTRUM')
        if abs(r['cm']) > 30:
            flags.append('PITCH')
        if r['de'] - r['dh'] > 40:
            flags.append('DRIFT')
        if r['envc'] < 0.8:
            flags.append('ENVELOPE')
        name = names[r['k']] if r['k'] < len(names) else ''
        print(f"{r['k'] + 1:03d}  {name:12s} {r['lh']:6.1f}  {l - off:+5.1f}  {spec:5.1f}  {r['cen'] / cenm:5.2f}"
              f"  {r['cm']:+7.1f}  {r['dh']:5.0f}/{r['de']:<5.0f}  {r['envc']:4.2f}  {' '.join(flags)}")
        if flags:
            marked.append(r['k'])
    print(f'{len(marked)} of {len(rows)} marked: {" ".join(f"T-{k + 1:03d}" for k in marked)}')

    if a.cut_dir:
        import os
        os.makedirs(a.cut_dir, exist_ok=True)
        g = 10 ** (-off / 20)
        for k in marked:
            parts = []
            for x, first, gain in ((hw2, hw_first, 1.0), (emu2, a.emu_first, g)):
                i = int((first + k * a.period - 0.05) * sr)
                parts.append(np.clip(x[i:i + int(a.period * sr)] * gain, -1, 1))
            pcm = (np.concatenate([parts[0], np.zeros((int(0.3 * sr), 2)), parts[1]]) * 32767).astype('<i2')
            name = (names[k] if k < len(names) else '').strip().replace(' ', '_').replace('.', '')
            with wave.open(os.path.join(a.cut_dir, f'T-{k + 1:03d}_{name}_hw_dann_emu.wav'), 'wb') as w:
                w.setnchannels(2)
                w.setsampwidth(2)
                w.setframerate(sr)
                w.writeframes(pcm.tobytes())


if __name__ == '__main__':
    main()
