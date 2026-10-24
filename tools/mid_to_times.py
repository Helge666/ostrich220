#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Turn a Standard MIDI File into the "seconds,HEXBYTE" lines u220_render --midi-at reads,
bytes spaced at the wire's 31250 baud, everything shifted by --offset seconds (the core
needs about 3 s to boot before it listens).

    tools/mid_to_times.py IN.mid OUT.txt [--offset 4.0]
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'hwcapture'))


def read_smf(path):
    # The SMF reader of hwcapture_asio.py, without its Windows-only imports.
    import struct
    b = open(path, 'rb').read()

    def vlq(p):
        v = 0
        while True:
            c = b[p]
            p += 1
            v = (v << 7) | (c & 0x7F)
            if not c & 0x80:
                return v, p
    ntrk, ppqn = struct.unpack('>HH', b[10:14])
    raw, tempo = [], []
    p = 14
    for _ in range(ntrk):
        length = struct.unpack('>I', b[p + 4:p + 8])[0]
        q, end, tick, status = p + 8, p + 8 + length, 0, 0
        while q < end:
            d, q = vlq(q)
            tick += d
            c = b[q]
            if c == 0xFF:
                kind = b[q + 1]
                n, q = vlq(q + 2)
                if kind == 0x51:
                    tempo.append((tick, int.from_bytes(b[q:q + 3], 'big')))
                q += n
            elif c in (0xF0, 0xF7):
                n, q = vlq(q + 1)
                raw.append((tick, (b'\xf0' if c == 0xF0 else b'') + b[q:q + n]))
                q += n
            else:
                if c & 0x80:
                    status, q = c, q + 1
                n = 1 if status & 0xF0 in (0xC0, 0xD0) else 2
                raw.append((tick, bytes([status]) + b[q:q + n]))
                q += n
        p = end
    tempo.sort()
    out = []
    for tick, msg in sorted(raw, key=lambda e: e[0]):
        sec, last, us = 0.0, 0, 500000
        for t, u in tempo:
            if t > tick:
                break
            sec += (t - last) * us / 1e6 / ppqn
            last, us = t, u
        out.append((sec + (tick - last) * us / 1e6 / ppqn, msg))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('mid')
    ap.add_argument('out')
    ap.add_argument('--offset', type=float, default=4.0)
    a = ap.parse_args()
    byte = 10.0 / 31250.0
    lines, free = [], 0.0
    for t, msg in read_smf(a.mid):
        t = max(t + a.offset, free)
        for b in msg:
            t += byte                       # the time a byte has fully ARRIVED
            lines.append(f'{t:.6f},{b:02X}')
        free = t
    open(a.out, 'w').write('\n'.join(lines) + '\n')
    print(f'{len(lines)} bytes')


if __name__ == '__main__':
    main()
