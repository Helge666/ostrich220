#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Play a Standard MIDI File into the real U-220 and record its audio through ASIO, for A/B
comparisons against the emulation.  Windows only.

    python hwcapture_asio.py MIDI.mid OUT.wav [--midi-out "MIDIOUT2 (MIDIFACE 2x2)"]
        [--asio "X-AIR ASIO Driver"] [--channels 9,10] [--lead 0.5] [--tail 3]

Needs the sounddevice package (PortAudio with ASIO); MIDI goes out through WinMM via
ctypes.  The recording starts first and the file `lead` seconds later, and runs `tail`
seconds past the last event, so the WAV holds the silence before the first note and the
whole release.  Written as 16-bit stereo at the device's rate.

The ASIO driver is exclusive: close Cubase (or anything else using it) first.
"""

import argparse
import array
import ctypes
import struct
import sys
import threading
import time
from ctypes import wintypes

import sounddevice as sd


# ------------------------------------------------------------------- MIDI out (WinMM)

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


def midi_out_open(name):
    for i in range(winmm.midiOutGetNumDevs()):
        caps = MIDIOUTCAPSW()
        winmm.midiOutGetDevCapsW(i, ctypes.byref(caps), ctypes.sizeof(caps))
        if caps.szPname == name:
            h = ctypes.c_void_p()
            r = winmm.midiOutOpen(ctypes.byref(h), i, None, None, 0)
            if r:
                sys.exit(f'midiOutOpen({name}) failed: {r}')
            return h
    sys.exit(f'no MIDI output named {name!r}')


def midi_send(h, msg):
    if msg[0] == 0xF0:
        buf = ctypes.create_string_buffer(bytes(msg), len(msg))
        hdr = MIDIHDR(lpData=ctypes.cast(buf, ctypes.c_void_p), dwBufferLength=len(msg),
                      dwBytesRecorded=len(msg))
        winmm.midiOutPrepareHeader(h, ctypes.byref(hdr), ctypes.sizeof(hdr))
        winmm.midiOutLongMsg(h, ctypes.byref(hdr), ctypes.sizeof(hdr))
        for _ in range(500):
            if hdr.dwFlags & 1:
                break
            time.sleep(0.001)
        winmm.midiOutUnprepareHeader(h, ctypes.byref(hdr), ctypes.sizeof(hdr))
    else:
        word = msg[0] | (msg[1] << 8 if len(msg) > 1 else 0) | (msg[2] << 16 if len(msg) > 2 else 0)
        winmm.midiOutShortMsg(h, word)


# ------------------------------------------------------------------- SMF

def vlq(b, p):
    v = 0
    while True:
        c = b[p]
        p += 1
        v = (v << 7) | (c & 0x7F)
        if not c & 0x80:
            return v, p


def read_smf(path):
    b = open(path, 'rb').read()
    ntrk, ppqn = struct.unpack('>HH', b[10:14])
    raw, tempo = [], []
    p = 14
    for _ in range(ntrk):
        length = struct.unpack('>I', b[p + 4:p + 8])[0]
        q, end, tick, status = p + 8, p + 8 + length, 0, 0
        while q < end:
            d, q = vlq(b, q)
            tick += d
            c = b[q]
            if c == 0xFF:
                kind = b[q + 1]
                n, q = vlq(b, q + 2)
                if kind == 0x51:
                    tempo.append((tick, int.from_bytes(b[q:q + 3], 'big')))
                q += n
            elif c in (0xF0, 0xF7):
                n, q = vlq(b, q + 1)
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
    events = []
    for tick, msg in sorted(raw, key=lambda e: e[0]):
        sec, last, us = 0.0, 0, 500000
        for t, u in tempo:
            if t > tick:
                break
            sec += (t - last) * us / 1e6 / ppqn
            last, us = t, u
        events.append((sec + (tick - last) * us / 1e6 / ppqn, msg))
    return events


# ------------------------------------------------------------------- run

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('midi')
    ap.add_argument('out')
    ap.add_argument('--midi-out', default='MIDIOUT2 (MIDIFACE 2x2)')   # Helge's U-220
    ap.add_argument('--asio', default='X-AIR ASIO Driver')
    ap.add_argument('--channels', default='9,10', help='1-based input channels, left,right')
    ap.add_argument('--lead', type=float, default=0.5)
    ap.add_argument('--tail', type=float, default=3.0)
    a = ap.parse_args()

    events = read_smf(a.midi)
    total = a.lead + (events[-1][0] if events else 0) + a.tail

    dev = next(i for i, d in enumerate(sd.query_devices())
               if d['name'] == a.asio and sd.query_hostapis(d['hostapi'])['name'] == 'ASIO')
    rate = int(sd.query_devices(dev)['default_samplerate'])
    chans = [int(c) - 1 for c in a.channels.split(',')]

    captured = bytearray()
    lock = threading.Lock()

    def on_audio(indata, frames, t, status):
        with lock:
            captured.extend(bytes(indata))

    h = midi_out_open(a.midi_out)
    with sd.RawInputStream(device=dev, channels=len(chans), samplerate=rate, dtype='float32',
                           extra_settings=sd.AsioSettings(channel_selectors=chans),
                           callback=on_audio):
        t0 = time.perf_counter()
        for at, msg in events:
            while time.perf_counter() - t0 < a.lead + at:
                time.sleep(0.0005)
            midi_send(h, msg)
        while time.perf_counter() - t0 < total:
            time.sleep(0.01)
    winmm.midiOutReset(h)
    winmm.midiOutClose(h)

    f = array.array('f')
    f.frombytes(bytes(captured))
    pcm = array.array('h', (max(-32768, min(32767, int(round(x * 32767)))) for x in f))
    peak = max((abs(x) for x in pcm), default=0)
    with open(a.out, 'wb') as w:
        data = pcm.tobytes()
        w.write(b'RIFF' + struct.pack('<I', 36 + len(data)) + b'WAVEfmt '
                + struct.pack('<IHHIIHH', 16, 1, len(chans), rate, rate * 2 * len(chans), 2 * len(chans), 16)
                + b'data' + struct.pack('<I', len(data)) + data)
    import math
    print(f'{a.out}: {len(pcm) / len(chans) / rate:.1f} s at {rate} Hz, {len(events)} MIDI events, '
          f'peak {peak} ({20 * math.log10(max(peak, 1) / 32768):.1f} dBFS)')


if __name__ == '__main__':
    main()
