#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Write a small Standard MIDI File for headless tests of the U-220 driver.

    tools/make_test_mid.py OUT.mid            the protocol test below
    tools/make_test_mid.py --demo OUT.mid     a C major scale and a chord on channel 1,
                                              for listening

Played into MIDI IN by MAME's midiin image device (-min OUT.mid).  It holds playback
back until emulated time 10 s, so file time 0 is machine time 10 s -- comfortably after
the boot and the memory initialise (ENTER at 4 s, see tools/u220_keys.lua):

     0.0 s   note on, channel 1, C4          the firmware should start a voice
     1.0 s   note off
     2.0 s   program change 5, channel 1     a part's tone, or nothing
     3.0 s   program change 5, channel 16    the U-220's patch change channel?
     4.0 s   Roland RQ1, device 10h, model 2Bh, address 00 00 00, size 00 00 10:
             an answer on MIDI OUT proves the transmit side too
"""

import struct
import sys

PPQN = 480
TEMPO = 500000                      # us per quarter note -> 1 tick = 1.0417 ms


def ticks(seconds):
    return int(round(seconds * 1e6 / TEMPO * PPQN))


def vlq(n):
    out = [n & 0x7f]
    n >>= 7
    while n:
        out.append(0x80 | (n & 0x7f))
        n >>= 7
    return bytes(reversed(out))


def roland_rq1(device, model, addr, size):
    body = bytes(addr) + bytes(size)
    checksum = (128 - sum(body) % 128) % 128
    return bytes([0xf0, 0x41, device, model, 0x11]) + body + bytes([checksum, 0xf7])


def demo_events():
    events = []
    t = 0.0
    for note in (60, 62, 64, 65, 67, 69, 71, 72):
        events += [(t, bytes([0x90, note, 100])), (t + 0.4, bytes([0x80, note, 0]))]
        t += 0.5
    for note in (48, 60, 64, 67, 72):
        events.append((t + 0.2, bytes([0x90, note, 90])))
    for note in (48, 60, 64, 67, 72):
        events.append((t + 3.0, bytes([0x80, note, 0])))
    return sorted(events, key=lambda e: e[0])


def main():
    demo = sys.argv[1] == '--demo'
    out = sys.argv[-1]
    events = demo_events() if demo else [
        (0.0, bytes([0x90, 60, 100])),
        (1.0, bytes([0x80, 60, 0])),
        (2.0, bytes([0xc0, 5])),
        (3.0, bytes([0xcf, 5])),
        (4.0, roland_rq1(0x10, 0x2b, (0, 0, 0), (0, 0, 0x10))),
    ]
    track = bytearray()
    track += vlq(0) + bytes([0xff, 0x51, 3]) + TEMPO.to_bytes(3, 'big')
    now = 0
    for at, msg in events:
        t = ticks(at)
        track += vlq(t - now)
        now = t
        if msg[0] == 0xf0:
            track += bytes([0xf0]) + vlq(len(msg) - 1) + msg[1:]
        else:
            track += msg
    track += vlq(ticks(1.0)) + bytes([0xff, 0x2f, 0])

    with open(out, 'wb') as f:
        f.write(b'MThd' + struct.pack('>IHHH', 6, 0, 1, PPQN))
        f.write(b'MTrk' + struct.pack('>I', len(track)) + track)


if __name__ == '__main__':
    main()
