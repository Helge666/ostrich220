#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Write a Standard MIDI File that plays every timbre once, for a quick A/B of the real
U-220 against the emulation.

    tools/make_timbre_ab.py OUT.mid [--first 1] [--count 128] [--note 60] [--hold 0.6]
        [--period 1.6] [--velocity 100]
    tools/make_timbre_ab.py OUT.mid --rhythm [--first 1] [--count 4] [--period 0.75]

--rhythm instead hits every key 35..98 of each rhythm set on channel 10 (Program Change n-1
on channel 10 picks R-n, Rx Rhythm Change = Dir); slot k is set k // 64, key 35 + k % 64.

Program Change 1 on the control channel (16) selects P-01 Acoust Piano, whose parts all
listen on channel 1; then, per timbre, Program Change n-1 on channel 1 (Rx Timbre Change =
Dir) and one note.  Slot k (0-based) starts at LEAD + k * period seconds, the Program
Change PC_AHEAD seconds before its note, so a WAV can be cut into slots by time alone.
"""

import argparse
import struct

PPQN = 480
TEMPO = 500000
LEAD = 1.0                          # first note, seconds after the start of the file
PC_AHEAD = 0.25
FIRST_KEY, KEYS = 35, 64            # rhythm set key range


def vlq(n):
    out = [n & 0x7F]
    n >>= 7
    while n:
        out.append(0x80 | (n & 0x7F))
        n >>= 7
    return bytes(reversed(out))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('mid')
    ap.add_argument('--first', type=int, default=1)
    ap.add_argument('--count', type=int, help='timbres (128) or rhythm sets (4)')
    ap.add_argument('--note', type=int, default=60)
    ap.add_argument('--hold', type=float, default=0.6)
    ap.add_argument('--period', type=float, default=1.6)
    ap.add_argument('--velocity', type=int, default=100)
    ap.add_argument('--rhythm', action='store_true')
    a = ap.parse_args()
    if a.count is None:
        a.count = 4 if a.rhythm else 128

    ev = [(0.0, bytes([0xCF, 0]))]                                  # P-01 on control ch 16
    if a.rhythm:
        a.count *= KEYS
        for k in range(a.count):
            t = LEAD + k * a.period
            if k % KEYS == 0:
                ev.append((t - PC_AHEAD, bytes([0xC9, a.first - 1 + k // KEYS])))
            ev.append((t, bytes([0x99, FIRST_KEY + k % KEYS, a.velocity])))
            ev.append((t + min(a.hold, a.period / 2), bytes([0x89, FIRST_KEY + k % KEYS, 0])))
    for k in range(0 if a.rhythm else a.count):
        t = LEAD + k * a.period
        ev.append((t - PC_AHEAD, bytes([0xC0, a.first - 1 + k])))
        ev.append((t, bytes([0x90, a.note, a.velocity])))
        ev.append((t + a.hold, bytes([0x80, a.note, 0])))
    end = LEAD + a.count * a.period

    track = bytearray(vlq(0) + bytes([0xFF, 0x51, 3]) + TEMPO.to_bytes(3, 'big'))
    now = 0
    for t, msg in sorted(ev, key=lambda e: e[0]):
        tk = int(round(t * 1e6 / TEMPO * PPQN))
        track += vlq(tk - now) + msg
        now = tk
    track += vlq(int(round(end * 1e6 / TEMPO * PPQN)) - now) + bytes([0xFF, 0x2F, 0])
    with open(a.mid, 'wb') as f:
        f.write(b'MThd' + struct.pack('>IHHH', 6, 0, 1, PPQN))
        f.write(b'MTrk' + struct.pack('>I', len(track)) + track)
    print(f'{a.count} {"keys" if a.rhythm else "timbres"} from {"R-" if a.rhythm else "T-"}{a.first}, {end:.0f} s')


if __name__ == '__main__':
    main()
