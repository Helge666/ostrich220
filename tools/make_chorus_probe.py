#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Write a MIDI file that measures the U-220's chorus: P-02 (whose part 1, on channel 1, is
routed to the chorus only -- dry plus chorus send, no reverb), part 1 switched to Saw
Wave 1 by Program Change 59, its vibrato off, and for each setting one held C2.

    tools/make_chorus_probe.py BASE.syx OUT.mid [--list OUT.txt]

BASE.syx as for make_reverb_probe.py (its effect bytes are only the starting word; every
chorus field is set explicitly per slot, reverb level 0).

Why a held sawtooth (Helge's idea): every harmonic is a fixed measuring point.  The
chorus adds a copy delayed by tau(t), so harmonic h of the mix is |1 + g exp(-j w_h tau)|
times the dry one -- a comb across the harmonics whose position gives tau, whose depth
gives the wet gain g (and its sign), and whose sharpness gives the feedback; following
it frame by frame gives the LFO.  At 65.4 Hz the comb is unambiguous up to 7.6 ms; the
note's onset (the wet copy starts tau after the dry one) gives the coarse delay beyond
that.  The dry reference is the unit itself: two slots at chorus level 0.

Settings: Chorus1 swept in Delay, Depth, Rate and Level one at a time; the other types
at a few points plus their feedback; P-02's own chorus as it is.
"""

import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_reverb_probe import PPQN, TEMPO, load_patch, packet, vlq, with_fields  # noqa: E402

CTYPES = ['Chorus1', 'Chorus2', 'FB-Chorus', 'Flanger', 'ShortDelay']
NOTE, VELOCITY = 36, 110


def no_vibrato():
    """DT1 setting byte 25 of part 1's Temp timbre (nibble address 00 10 32) to 0: the
    timbre's vibrato depth, found by zeroing each of the 32 bytes in the emulation.  Saw
    Wave 1 has a delayed vibrato of about +-1 % (from ~0.5 s, as Helge heard), which
    spoils the dry reference the analysis relies on.  Must follow every Program Change
    on channel 1."""
    addr, nib = bytes([0x00, 0x10, 0x32]), bytes([0, 0])
    cs = (128 - (sum(addr) + sum(nib)) % 128) % 128
    return bytes([0xF0, 0x41, 0x10, 0x2B, 0x12]) + addr + nib + bytes([cs, 0xF7])


def sysex_event(msg):
    return bytes([0xF0]) + vlq(len(msg) - 1) + msg[1:]


def settings():
    """(label, chorus fields or None for P-02 as stored, hold seconds)."""
    base = dict(rlevel=0, cout=1, cfb=32)          # cfb is stored +32: 32 = 0
    out = [('dry (chorus level 0)', dict(base, ctype=0, clevel=0, cdelay=16, crate=0, cdepth=0), 4.0),
           ('P-02 as stored', None, 6.0)]

    def c(t, lv=31, dl=16, rt=0, dp=0, fb=0):
        return dict(base, ctype=t, clevel=lv, cdelay=dl, crate=rt, cdepth=dp, cfb=fb + 32)
    for dl in (0, 4, 8, 16, 24, 31):
        out.append((f'Chorus1 delay {dl} depth 0', c(0, dl=dl), 4.0))
    for dp in (8, 16, 24, 31):
        out.append((f'Chorus1 delay 16 rate 16 depth {dp}', c(0, rt=16, dp=dp), 5.0))
    for rt in (0, 8, 24, 31):
        out.append((f'Chorus1 delay 16 rate {rt} depth 16', c(0, rt=rt, dp=16), 10.0 if rt < 8 else 6.0))
    for lv in (8, 16, 24):
        out.append((f'Chorus1 delay 16 level {lv}', c(0, lv=lv), 4.0))
    out.append(('Chorus1 delay 8 fb +31', c(0, dl=8, fb=31), 4.0))
    out.append(('dry 2 (chorus level 0)', c(0, lv=0), 4.0))
    for t in range(1, 5):
        name = CTYPES[t]
        for dl in (0, 16, 31):
            out.append((f'{name} delay {dl} depth 0', c(t, dl=dl), 4.0))
        out.append((f'{name} delay 16 rate 16 depth 16', c(t, rt=16, dp=16), 6.0))
        out.append((f'{name} delay 16 rate 31 depth 16', c(t, rt=31, dp=16), 5.0))
        if t >= 2:
            for fb in (-31, -16, 16, 31):
                out.append((f'{name} delay 8 fb {fb:+d}', c(t, dl=8, fb=fb), 4.0))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('base')
    ap.add_argument('mid')
    ap.add_argument('--list')
    a = ap.parse_args()

    base = load_patch(a.base)
    ev = [(0.0, bytes([0xCF, 0x01])),             # P-02 into the temp area
          (0.3, bytes([0xC0, 59])),               # part 1: Saw Wave 1
          (0.32, sysex_event(no_vibrato())),
          (0.35, bytes([0xB0, 0x01, 0x00])),      # no modulation
          (0.4, bytes([0xE0, 0x00, 0x40]))]       # no bend
    labels = []
    t = 1.0
    for label, f, hold in settings():
        if f is not None:
            msg = packet(with_fields(base, **f))
            ev.append((t, b'\xf0' + vlq(len(msg) - 1) + msg[1:]))
        else:
            # P-02's stored effects, as the Program Change loaded them.
            ev.append((t, bytes([0xCF, 0x01])))
            ev.append((t + 0.2, bytes([0xC0, 59])))
            ev.append((t + 0.25, sysex_event(no_vibrato())))
        on = t + 0.8
        ev.append((on, bytes([0x90, NOTE, VELOCITY])))
        ev.append((on + hold, bytes([0x80, NOTE, 0])))
        labels.append((on, hold, label))
        t = on + hold + 1.2
    ev.append((t, bytes([0xCF, 0x01])))          # back to the stored P-02
    end = t + 1.0

    track = bytearray(vlq(0) + bytes([0xFF, 0x51, 3]) + TEMPO.to_bytes(3, 'big'))
    now = 0
    for when, msg in sorted(ev, key=lambda e: e[0]):
        tk = int(round(when * 1e6 / TEMPO * PPQN))
        track += vlq(tk - now) + msg
        now = tk
    track += vlq(int(round(end * 1e6 / TEMPO * PPQN)) - now) + bytes([0xFF, 0x2F, 0])
    with open(a.mid, 'wb') as f:
        f.write(b'MThd' + struct.pack('>IHHH', 6, 0, 1, PPQN))
        f.write(b'MTrk' + struct.pack('>I', len(track)) + track)
    if a.list:
        with open(a.list, 'w') as f:
            for on, hold, label in labels:
                f.write(f'{on:.3f}\t{hold:.1f}\t{label}\n')
    print(f'{len(labels)} settings, {end:.0f} s')


if __name__ == '__main__':
    main()
