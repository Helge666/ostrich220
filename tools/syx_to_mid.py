#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Turn a SysEx bulk dump (.syx) into a Standard MIDI File that sends the messages with a
pause between them, and check the dump on the way.

    tools/syx_to_mid.py IN.syx OUT.mid [--gap-ms 20] [--clean OUT.syx]

Roland units need a moment between DT1 packets to write them to memory; sent back to
back, a 138-byte packet follows the last within 44 ms and the slow ones overrun.  The gap
is added AFTER each message's own transmission time.

Checked: every message is F0 ... F7, every Roland checksum is right, and a message whose
address was already sent is dropped (with a note) -- the circulating U220Factory.Syx ends
with a partial second copy of its last few packets, the very last one cut off.  --clean
writes the de-duplicated, complete messages back out as .syx.
"""

import argparse
import struct
import sys

PPQN = 480
TEMPO = 500000                      # us per quarter note


def ticks(seconds):
    return int(round(seconds * 1e6 / TEMPO * PPQN))


def vlq(n):
    out = [n & 0x7F]
    n >>= 7
    while n:
        out.append(0x80 | (n & 0x7F))
        n >>= 7
    return bytes(reversed(out))


def split(data):
    msgs, i = [], 0
    while i < len(data):
        if data[i] != 0xF0:
            i += 1
            continue
        nxt = data.find(b'\xf0', i + 1)
        end = data.find(b'\xf7', i)
        if end == -1 or (nxt != -1 and nxt < end):
            print(f'  dropped: message at byte {i} has no F7 (truncated)', file=sys.stderr)
            i = nxt if nxt != -1 else len(data)
            continue
        msgs.append(data[i:end + 1])
        i = end + 1
    return msgs


def load_dump(path):
    """The complete, checksum-correct messages of a .syx file, each address once."""
    good, seen = [], set()
    for m in split(open(path, 'rb').read()):
        if m[1] == 0x41 and len(m) > 10 and (sum(m[5:-2]) + m[-2]) % 128:
            print(f'  dropped: bad checksum at address {m[5:8].hex(" ")}', file=sys.stderr)
            continue
        key = bytes(m[1:8])
        if key in seen:
            print(f'  dropped: repeat of address {m[5:8].hex(" ")}', file=sys.stderr)
            continue
        seen.add(key)
        good.append(m)
    return good


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('syx')
    ap.add_argument('mid')
    ap.add_argument('--gap-ms', type=float, default=20.0)
    ap.add_argument('--clean')
    a = ap.parse_args()

    good = load_dump(a.syx)
    track = bytearray(vlq(0) + bytes([0xFF, 0x51, 3]) + TEMPO.to_bytes(3, 'big'))
    t = 0.0
    now = 0
    for m in good:
        tk = ticks(t)
        track += vlq(tk - now) + bytes([0xF0]) + vlq(len(m) - 1) + m[1:]
        now = tk
        t += len(m) * 10 / 31250 + a.gap_ms / 1000      # wire time + pause
    track += vlq(ticks(0.5)) + bytes([0xFF, 0x2F, 0])
    with open(a.mid, 'wb') as f:
        f.write(b'MThd' + struct.pack('>IHHH', 6, 0, 1, PPQN))
        f.write(b'MTrk' + struct.pack('>I', len(track)) + track)
    if a.clean:
        open(a.clean, 'wb').write(b''.join(good))
    print(f'{len(good)} messages, {sum(len(m) for m in good)} bytes, {t:.1f} s to send')


if __name__ == '__main__':
    main()
