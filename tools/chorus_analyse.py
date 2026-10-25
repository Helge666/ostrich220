#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Read the chorus out of a make_chorus_probe.py recording: per slot and channel the delay
of the wet copy over time, tau(t), its gain g and the feedback f -- so base delay, LFO
depth, rate, waveform and the left/right relation can be read off and fitted.

    tools/chorus_analyse.py REC.wav LIST.txt [--json OUT.json] [--only SUBSTRING]
        [--lead 0.5] [--trace SLOT_SUBSTRING]

The held sawtooth has one steep edge per period.  Differentiated, the dry signal is a
sharp pulse at the same place in every period; the chorus adds a copy of that pulse tau
later, scaled by g -- so tau is read straight off, once per period (65 times a second),
however fast the LFO moves it.  (A spectral comb fit was tried first: a moving delay
smears the upper harmonics within any frame long enough to resolve them.)

  1. The dry slot (chorus level 0), folded over the period, gives the dry pulse shape.
  2. In each slot the first edge after the note-on is dry (the copy comes later); from
     there the dry edges are periodic and the folded dry shape is subtracted.
  3. What is left per period is the wet edge: its time after the dry edge is tau (mod
     the period, unwrapped along the slot and unfolded with the first wet edge after
     the onset), its height over the dry one is g, with sign.
  4. Feedback: the second edge, tau after the first wet one, over the first.

Prints per slot and side: median tau, LFO peak-to-peak depth, rate and waveform (the
3rd harmonic of tau(t): a triangle has -19 dB, a sine far less), g, f, and the
left/right correlation of tau(t).  Needs numpy.
"""

import argparse
import json
import wave

import numpy as np

NB = 4096           # bins per period for the folded shapes


def load(path):
    w = wave.open(path)
    x = np.frombuffer(w.readframes(w.getnframes()), '<i2').reshape(-1, w.getnchannels())
    return x.astype(np.float64) / 32768, w.getframerate()


def onset(x, sr, t_from, t_to):
    """First sample in [t_from, t_to) where the mono level passes 1/10 of that stretch's peak."""
    a, b = int(t_from * sr), int(t_to * sr)
    m = np.abs(x[a:b].mean(1))
    return a + int(np.argmax(m > 0.1 * m.max()))


def fine_f0(seg, sr, guess):
    n = np.arange(len(seg))
    h = np.arange(1, 9)
    best, bf = -1, guess
    for f in np.arange(guess - 0.6, guess + 0.6, 0.002):
        p = (np.abs(np.exp(-2j * np.pi * np.outer(h * f, n) / sr) @ seg) ** 2).sum()
        if p > best:
            best, bf = p, f
    return bf


def fold_f0(y, sr, guess, span=0.05, step=0.0005):
    """f0 at which y (a derivative) folds into the sharpest edge -- far finer than any
    spectral estimate: an error of 1e-4 smears the edge over hundreds of periods."""
    best, bf = -1.0, guess
    for _ in range(3):
        for f in np.arange(bf - span, bf + span, step):
            v = np.abs(fold(y, sr / f, 0)).max()
            if v > best:
                best, bf = v, f
        span, step = step * 2, step / 10
    return bf


def fold(y, P, n0):
    """Average of y over periods of P samples (fractional), phase 0 at sample n0."""
    ph = ((np.arange(len(y)) - n0) / P) % 1.0
    b = (ph * NB).astype(int)
    s = np.bincount(b, y, NB)
    c = np.bincount(b, None, NB)
    return s / np.maximum(c, 1)


def peak_interp(v, k):
    """Parabolic refinement of an extremum of v at index k."""
    if 0 < k < len(v) - 1:
        a, b, c = v[k - 1], v[k], v[k + 1]
        d = a - 2 * b + c
        if d != 0:
            return k + 0.5 * (a - c) / d
    return float(k)


def lfo(t, tau):
    """Rate (Hz), peak-to-peak (s), 3rd-harmonic level (dB) of a tau(t) trace."""
    if np.percentile(tau, 99) - np.percentile(tau, 1) < 2e-5:
        return 0.0, float(np.percentile(tau, 99) - np.percentile(tau, 1)), np.nan
    x = tau - tau.mean()
    n = 1 << 17
    X = np.abs(np.fft.rfft(x * np.hanning(len(x)), n))
    fr = np.fft.rfftfreq(n, t[1] - t[0])
    k = int(np.argmax(X[1:])) + 1
    k3 = int(np.argmin(np.abs(fr - 3 * fr[k])))
    h3 = 20 * np.log10(X[max(k3 - 3, 0):k3 + 4].max() / X[k] + 1e-12)
    return float(fr[k]), float(np.percentile(tau, 99) - np.percentile(tau, 1)), float(h3)


def analyse_side(y, i0, P, tpl, n_end):
    """tau (s, per period), g, f for one channel's derivative y from onset i0."""
    # The first strong edge after the onset is dry.
    thr = 0.5 * np.abs(tpl).max()
    k_first = i0 + int(np.argmax(np.abs(y[i0:i0 + int(2 * P)]) > thr))
    e_tpl = int(np.argmax(np.abs(tpl)))
    sign = np.sign(tpl[e_tpl])
    # Refine the dry edge time on the pulse itself.
    w = y[k_first - 3:k_first + 4] * sign
    t_first = k_first - 3 + peak_interp(w, int(np.argmax(w)))
    # Dry model: the folded dry pulse shape, placed so its edge falls on t_first + k P.
    n = np.arange(i0, n_end)
    ph = ((n - t_first) / P + e_tpl / NB) % 1.0
    model = np.interp(ph * NB, np.arange(NB + 1), np.append(tpl, tpl[0]))
    seg = y[i0:n_end]
    a = (seg @ model) / (model @ model)
    r = seg - a * model
    h0 = a * np.abs(tpl[e_tpl])

    taus, gs, fs, times = [], [], [], []
    k = 0
    while True:
        s0 = t_first + k * P - i0
        s1 = s0 + P
        if s1 + 2 >= len(r):
            break
        lo, hi = int(np.ceil(s0)), int(s1)
        win = r[lo:hi]
        if len(win) < 4:
            break
        j = int(np.argmax(np.abs(win)))
        jj = peak_interp(np.abs(win), j)
        tw = lo + jj
        taus.append((tw - s0) / P)          # fraction of a period
        gs.append(win[j] * sign / h0)
        # Second edge (feedback): largest after excluding +-6 samples of the first.
        w2 = np.abs(win).copy()
        w2[max(j - 6, 0):j + 7] = 0
        j2 = int(np.argmax(w2))
        fs.append((win[j2] * sign / h0, (j2 - j) / P))
        times.append((s0 + i0) / P)
        k += 1
    return t_first, np.array(taus), np.array(gs), fs, np.array(times)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('rec')
    ap.add_argument('list')
    ap.add_argument('--lead', type=float, default=0.5)
    ap.add_argument('--json')
    ap.add_argument('--only')
    ap.add_argument('--trace')
    a = ap.parse_args()

    x, sr = load(a.rec)
    rows = [l.rstrip('\n').split('\t') for l in open(a.list) if l.strip()]
    slots = [dict(on=float(r[0]), hold=float(r[1]), label=r[2]) for r in rows]

    dry = [s for s in slots if s['label'].startswith('dry')]
    s0 = dry[0]
    latency = onset(x, sr, a.lead + s0['on'], a.lead + s0['on'] + 0.3) / sr - s0['on']
    slots = [s for s in slots if (latency + s['on'] + s['hold'] + 0.1) * sr < len(x)]
    for s in slots:
        s['i'] = onset(x, sr, latency + s['on'] - 0.02, latency + s['on'] + 0.1)
    seg = x[s0['i'] + int(0.5 * sr):s0['i'] + int(1.5 * sr)].mean(1)
    f0 = fine_f0(seg, sr, 65.406)
    yd = np.diff(x[:, 0], prepend=0.0)[s0['i'] + int(0.2 * sr):s0['i'] + int((s0['hold'] - 0.05) * sr)]
    f0 = fold_f0(yd, sr, f0)
    P = sr / f0
    print(f'latency {latency * 1000:.1f} ms, f0 {f0:.3f} Hz (period {1000 / f0:.3f} ms)')

    # Dry pulse shapes per channel, folded from the dry slot's steady part.
    tpls = []
    for c in range(2):
        y = np.diff(x[:, c], prepend=0.0)
        i = s0['i'] + int(0.2 * sr)
        tpls.append(fold(y[i:s0['i'] + int((s0['hold'] - 0.05) * sr)], P, 0))

    results = []
    for s in slots:
        if s['label'].startswith('dry') or (a.only and a.only not in s['label']):
            continue
        res = dict(label=s['label'])
        traces = []
        n_end = s['i'] + int((s['hold'] - 0.05) * sr)
        for c in range(2):
            y = np.diff(x[:, c], prepend=0.0)
            tf, frac, g, fb, tm = analyse_side(y, s['i'], P, tpls[c], n_end)
            # Unwrap along the slot, then unfold: the first wet edge after the onset.
            ph = np.unwrap(frac * 2 * np.pi) / (2 * np.pi)
            # The wet copy of the first dry edge lands in period m = floor(tau / P): the
            # first period with a real wet edge in it.
            strong = np.nonzero(np.abs(g) > 0.3 * np.median(np.abs(g)))[0]
            first = int(strong[0]) if len(strong) else 0
            ph = ph - ph[first] + first + frac[first]
            tau = ph / f0
            t = tm / f0
            steady = t - t[0] > 0.25
            rate, pp, h3 = lfo(t[steady], tau[steady])
            gm = float(np.median(g[steady]))
            res[('L', 'R')[c]] = dict(tau_ms=float(np.median(tau[steady]) * 1000), pp_ms=pp * 1000,
                                      rate_hz=rate, h3_db=h3, g=gm,
                                      f=float(np.median([v for v, _ in fb]) / gm) if gm else 0.0)
            traces.append(tau[steady])
        n = min(len(traces[0]), len(traces[1]))
        cc = (np.corrcoef(traces[0][:n], traces[1][:n])[0, 1]
              if np.std(traces[0][:n]) > 1e-6 and np.std(traces[1][:n]) > 1e-6 else np.nan)
        res['lr_corr'] = float(cc)
        results.append(res)
        line = f'{s["label"]:36s}'
        for side in ('L', 'R'):
            q = res[side]
            line += (f' | {side} tau {q["tau_ms"]:6.2f} pp {q["pp_ms"]:5.2f} ms {q["rate_hz"]:5.2f} Hz'
                     f' h3 {q["h3_db"]:4.0f} g {q["g"]:+.2f} f {q["f"]:+.2f}')
        print(line + f' | LR {cc:+.2f}')
        if a.trace and a.trace in s['label']:
            for k in range(0, n, 8):
                print(f'   {k / f0:6.3f}  {traces[0][k] * 1000:7.3f}  {traces[1][k] * 1000:7.3f}')

    if a.json:
        json.dump(dict(f0=f0, latency=latency, slots=results), open(a.json, 'w'), indent=1)


if __name__ == '__main__':
    main()
