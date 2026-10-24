#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Write a MIDI file that measures the U-220's reverb: for each setting, a SysEx rewrites the
effect fields of the TEMP patch, then one broadband hit (kick + snare + closed hat on the
rhythm channel, and a short piano chord on channel 1) and a tail long enough for the longest hall.

    tools/make_reverb_probe.py BASE.syx OUT.mid [--slot 6.0] [--list OUT.txt]

BASE.syx is the first packet (address 00 06 00) of a Temp bulk dump of P-01 from the
emulation (Data > Bulk > Temp, captured with u220_render --midi-out); it is factory data
and stays out of the repo.

The effect fields, found by changing one parameter at a time in the emulated menus and
diffing Temp dumps (ROADMAP.md, Phase 3).  Bytes 12..19 of the patch, read as one
little-endian 64-bit word:

    bits  0- 4 chorus rate      bits 32-36 chorus delay time
    bits  6-10 chorus level     bits 43-47 reverb delay feedback
    bits 11-15 chorus depth     bits 48-52 reverb level
    bits 16-20 reverb time      bits 60-62 reverb type (Room1..CrossDelay = 0..7)
    bits 23-28 chorus feedback  bit  63    chorus out (0 = pre reverb, 1 = post)
    bits 29-31 chorus type
"""

import argparse
import struct

TYPES = ['Room1', 'Room2', 'Room3', 'Hall1', 'Hall2', 'Gate', 'Delay', 'CrossDelay']
FIELDS = {'crate': (0, 5), 'clevel': (6, 5), 'cdepth': (11, 5), 'rtime': (16, 5),
          'cfb': (23, 6), 'ctype': (29, 3), 'cdelay': (32, 5), 'rfb': (43, 5),
          'rlevel': (48, 5), 'rtype': (60, 3), 'cout': (63, 1)}
PPQN, TEMPO = 480, 500000


def vlq(n):
    out = [n & 0x7F]
    n >>= 7
    while n:
        out.append(0x80 | (n & 0x7F))
        n >>= 7
    return bytes(reversed(out))


def load_patch(path):
    d = open(path, 'rb').read()
    i = d.index(b'\xf0')
    m = d[i:d.index(b'\xf7', i) + 1]
    assert m[1:8] == bytes([0x41, 0x10, 0x2B, 0x12, 0x00, 0x06, 0x00]), 'not the 00 06 00 packet'
    body = m[8:-2]
    return bytearray((body[k] & 15) | ((body[k + 1] & 15) << 4) for k in range(0, len(body), 2))


def packet(data):
    nib = bytearray()
    for b in data:
        nib += bytes([b & 15, b >> 4])
    addr = bytes([0x00, 0x06, 0x00])
    csum = (128 - (sum(addr) + sum(nib)) % 128) % 128
    return b'\xf0\x41\x10\x2b\x12' + addr + bytes(nib) + bytes([csum]) + b'\xf7'


def with_fields(base, **kv):
    data = bytearray(base)
    word = int.from_bytes(data[12:20], 'little')
    for k, v in kv.items():
        pos, width = FIELDS[k]
        word = (word & ~(((1 << width) - 1) << pos)) | ((v & ((1 << width) - 1)) << pos)
    data[12:20] = word.to_bytes(8, 'little')
    return data


def settings():
    """(label, fields): a dry reference, the level law, then every type at five times."""
    out = [('dry (rev level 0)', dict(rtype=4, rtime=4, rlevel=0, rfb=15))]
    for lv in (10, 20, 31):
        out.append((f'Hall2 time 4 level {lv}', dict(rtype=4, rtime=4, rlevel=lv, rfb=15)))
    for t, name in enumerate(TYPES):
        for tm in (0, 8, 16, 24, 31):
            out.append((f'{name} time {tm} level 31', dict(rtype=t, rtime=tm, rlevel=31, rfb=15)))
    for fb in (0, 31):
        out.append((f'Delay time 16 fb {fb}', dict(rtype=6, rtime=16, rlevel=31, rfb=fb)))
        out.append((f'CrossDelay time 16 fb {fb}', dict(rtype=7, rtime=16, rlevel=31, rfb=fb)))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('base')
    ap.add_argument('mid')
    ap.add_argument('--slot', type=float, default=6.0)
    ap.add_argument('--list')
    a = ap.parse_args()

    base = load_patch(a.base)
    ev = [(0.0, bytes([0xCF, 0x00]))]           # P-01 into the temp area, everything else as is
    labels = []
    t = 1.0
    for label, f in settings():
        # Chorus silenced for the whole run: level 0, so neither path carries it.
        msg = packet(with_fields(base, clevel=0, **f))
        ev.append((t, b'\xf0' + vlq(len(msg) - 1) + msg[1:]))
        hit = t + 1.0
        for key in (36, 38, 42):
            ev.append((hit, bytes([0x99, key, 110])))
            ev.append((hit + 0.1, bytes([0x89, key, 0])))
        # Plus a short piano chord on channel 1, in case the rhythm part is not sent to
        # the reverb: P-01's six parts all listen there.
        for key in (48, 72, 96):
            ev.append((hit, bytes([0x90, key, 110])))
            ev.append((hit + 0.15, bytes([0x80, key, 0])))
        labels.append((hit, label))
        t += a.slot
    end = t + 1.0

    track = bytearray(vlq(0) + bytes([0xFF, 0x51, 3]) + TEMPO.to_bytes(3, 'big'))
    now = 0
    for when, msg in sorted(ev, key=lambda e: e[0]):
        tk = int(round(when * 1e6 / TEMPO * PPQN))
        # A SysEx event is stored as F0 <len> <data>; the rest are plain messages.
        track += vlq(tk - now) + (msg if msg[0] == 0xF0 else msg)
        now = tk
    track += vlq(int(round(end * 1e6 / TEMPO * PPQN)) - now) + bytes([0xFF, 0x2F, 0])
    with open(a.mid, 'wb') as f:
        f.write(b'MThd' + struct.pack('>IHHH', 6, 0, 1, PPQN))
        f.write(b'MTrk' + struct.pack('>I', len(track)) + track)
    if a.list:
        with open(a.list, 'w') as f:
            for hit, label in labels:
                f.write(f'{hit:.3f}\t{label}\n')
    print(f'{len(labels)} settings, {end:.0f} s')


if __name__ == '__main__':
    main()
