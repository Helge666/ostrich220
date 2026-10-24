#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Send a SysEx bulk dump (.syx) to a MIDI output, one message at a time with a pause in
between, so the receiving Roland unit can write each packet to memory.  Windows only.

    python syx_send.py DUMP.syx [--midi-out "MIDIOUT2 (MIDIFACE 2x2)"] [--gap-ms 20]

The dump is checked first (see syx_to_mid.py): truncated, bad-checksum and repeated
messages are left out.  Nothing is sent before the check has passed.
"""

import argparse
import ctypes
import os
import sys
import time
from ctypes import wintypes

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from syx_to_mid import load_dump, split_dt1                         # noqa: E402

winmm = ctypes.WinDLL('winmm')


class MIDIOUTCAPSW(ctypes.Structure):
    _fields_ = [('wMid', wintypes.WORD), ('wPid', wintypes.WORD), ('vDriverVersion', wintypes.UINT),
                ('szPname', wintypes.WCHAR * 32), ('wTechnology', wintypes.WORD),
                ('wVoices', wintypes.WORD), ('wNotes', wintypes.WORD),
                ('wChannelMask', wintypes.WORD), ('dwSupport', wintypes.DWORD)]


class MIDIHDR(ctypes.Structure):
    _fields_ = [('lpData', ctypes.c_void_p), ('dwBufferLength', wintypes.DWORD),
                ('dwBytesRecorded', wintypes.DWORD), ('dwUser', ctypes.c_void_p),
                ('dwFlags', wintypes.DWORD), ('lpNext', ctypes.c_void_p),
                ('reserved', ctypes.c_void_p), ('dwOffset', wintypes.DWORD),
                ('dwReserved', ctypes.c_void_p * 8)]


def open_out(name):
    names = []
    for i in range(winmm.midiOutGetNumDevs()):
        caps = MIDIOUTCAPSW()
        winmm.midiOutGetDevCapsW(i, ctypes.byref(caps), ctypes.sizeof(caps))
        names.append(caps.szPname)
        if caps.szPname == name:
            h = ctypes.c_void_p()
            if winmm.midiOutOpen(ctypes.byref(h), i, None, None, 0):
                sys.exit(f'cannot open {name!r} (in use by another program?)')
            return h
    sys.exit(f'no MIDI output named {name!r}; there are: {names}')


def send_long(h, msg):
    buf = ctypes.create_string_buffer(bytes(msg), len(msg))
    hdr = MIDIHDR(lpData=ctypes.cast(buf, ctypes.c_void_p), dwBufferLength=len(msg),
                  dwBytesRecorded=len(msg))
    winmm.midiOutPrepareHeader(h, ctypes.byref(hdr), ctypes.sizeof(hdr))
    winmm.midiOutLongMsg(h, ctypes.byref(hdr), ctypes.sizeof(hdr))
    while not hdr.dwFlags & 1:                                      # MHDR_DONE
        time.sleep(0.001)
    winmm.midiOutUnprepareHeader(h, ctypes.byref(hdr), ctypes.sizeof(hdr))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('syx')
    ap.add_argument('--midi-out', default='MIDIOUT2 (MIDIFACE 2x2)')   # Helge's U-220
    ap.add_argument('--gap-ms', type=float, default=20.0)
    ap.add_argument('--max-data', type=int, default=64,
                    help='split DT1 packets to this many data bytes (0 = never); the '
                         'MIDIFACE truncates SysEx after ~128 bytes')
    a = ap.parse_args()

    msgs = load_dump(a.syx)
    if a.max_data > 0:
        msgs = [p for m in msgs for p in split_dt1(m, a.max_data)]
    h = open_out(a.midi_out)
    t0 = time.perf_counter()
    for n, m in enumerate(msgs, 1):
        send_long(h, m)
        time.sleep(a.gap_ms / 1000)
        print(f'\r{n}/{len(msgs)}', end='', flush=True)
    winmm.midiOutClose(h)
    print(f'\r{len(msgs)} messages, {sum(map(len, msgs))} bytes in '
          f'{time.perf_counter() - t0:.1f} s to {a.midi_out}')


if __name__ == '__main__':
    main()
