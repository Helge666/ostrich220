// SPDX-License-Identifier: GPL-3.0-or-later
//
// Play a Standard MIDI File into the real U-220 and record its audio at the same time,
// for A/B comparisons against the emulation.  Windows only, WinMM, no dependencies.
//
//   powershell -File tools\hwcapture\hwcapture.ps1 -Midi file.mid -Out take.wav
//       [-MidiOut "MIDIFACE 2x2"] [-WaveIn "IN 3-4"] [-Tail 3] [-Lead 0.5]
//
// The recording starts first and the MIDI file `Lead` seconds later, so the WAV carries
// its own silence before the first note; it runs on `Tail` seconds after the last event
// so releases are captured.  48 kHz, 16-bit, stereo -- what the XR18 delivers.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;

public static class HwCapture
{
    // ---------------------------------------------------------------- WinMM

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct MIDIOUTCAPS { public ushort wMid, wPid; public uint vDriverVersion;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string szPname;
        public ushort wTechnology, wVoices, wNotes, wChannelMask; public uint dwSupport; }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct WAVEINCAPS { public ushort wMid, wPid; public uint vDriverVersion;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string szPname;
        public uint dwFormats; public ushort wChannels, wReserved1; }

    [StructLayout(LayoutKind.Sequential)]
    struct WAVEFORMATEX { public ushort wFormatTag, nChannels; public uint nSamplesPerSec, nAvgBytesPerSec;
        public ushort nBlockAlign, wBitsPerSample, cbSize; }

    [StructLayout(LayoutKind.Sequential)]
    struct WAVEHDR { public IntPtr lpData; public uint dwBufferLength, dwBytesRecorded; public IntPtr dwUser;
        public uint dwFlags, dwLoops; public IntPtr lpNext, reserved; }

    [StructLayout(LayoutKind.Sequential)]
    struct MIDIHDR { public IntPtr lpData; public uint dwBufferLength, dwBytesRecorded; public IntPtr dwUser;
        public uint dwFlags; public IntPtr lpNext, reserved; public uint dwOffset;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 8)] public IntPtr[] dwReserved; }

    const uint WHDR_DONE = 1, MHDR_DONE = 1;

    [DllImport("winmm.dll")] static extern uint midiOutGetNumDevs();
    [DllImport("winmm.dll", CharSet = CharSet.Unicode)] static extern uint midiOutGetDevCaps(UIntPtr id, ref MIDIOUTCAPS c, uint size);
    [DllImport("winmm.dll")] static extern uint midiOutOpen(out IntPtr h, uint id, IntPtr cb, IntPtr inst, uint flags);
    [DllImport("winmm.dll")] static extern uint midiOutShortMsg(IntPtr h, uint msg);
    [DllImport("winmm.dll")] static extern uint midiOutLongMsg(IntPtr h, IntPtr hdr, uint size);
    [DllImport("winmm.dll")] static extern uint midiOutPrepareHeader(IntPtr h, IntPtr hdr, uint size);
    [DllImport("winmm.dll")] static extern uint midiOutUnprepareHeader(IntPtr h, IntPtr hdr, uint size);
    [DllImport("winmm.dll")] static extern uint midiOutReset(IntPtr h);
    [DllImport("winmm.dll")] static extern uint midiOutClose(IntPtr h);

    [DllImport("winmm.dll")] static extern uint waveInGetNumDevs();
    [DllImport("winmm.dll", CharSet = CharSet.Unicode)] static extern uint waveInGetDevCaps(UIntPtr id, ref WAVEINCAPS c, uint size);
    [DllImport("winmm.dll")] static extern uint waveInOpen(out IntPtr h, uint id, ref WAVEFORMATEX fmt, IntPtr cb, IntPtr inst, uint flags);
    [DllImport("winmm.dll")] static extern uint waveInPrepareHeader(IntPtr h, IntPtr hdr, uint size);
    [DllImport("winmm.dll")] static extern uint waveInUnprepareHeader(IntPtr h, IntPtr hdr, uint size);
    [DllImport("winmm.dll")] static extern uint waveInAddBuffer(IntPtr h, IntPtr hdr, uint size);
    [DllImport("winmm.dll")] static extern uint waveInStart(IntPtr h);
    [DllImport("winmm.dll")] static extern uint waveInReset(IntPtr h);
    [DllImport("winmm.dll")] static extern uint waveInClose(IntPtr h);

    [DllImport("winmm.dll")] static extern uint timeBeginPeriod(uint ms);
    [DllImport("winmm.dll")] static extern uint timeEndPeriod(uint ms);

    static uint FindMidiOut(string name)
    {
        for (uint i = 0; i < midiOutGetNumDevs(); i++)
        {
            var c = new MIDIOUTCAPS();
            midiOutGetDevCaps(new UIntPtr(i), ref c, (uint)Marshal.SizeOf(c));
            if (c.szPname == name) return i;
        }
        throw new Exception("no MIDI output named '" + name + "'");
    }

    static uint FindWaveIn(string prefix)
    {
        for (uint i = 0; i < waveInGetNumDevs(); i++)
        {
            var c = new WAVEINCAPS();
            waveInGetDevCaps(new UIntPtr(i), ref c, (uint)Marshal.SizeOf(c));
            if (c.szPname.StartsWith(prefix)) return i;
        }
        throw new Exception("no audio input starting with '" + prefix + "'");
    }

    // ---------------------------------------------------------------- SMF

    public struct Ev { public double t; public byte[] data; }

    static int Vlq(byte[] b, ref int p) { int v = 0; byte c; do { c = b[p++]; v = (v << 7) | (c & 0x7f); } while ((c & 0x80) != 0); return v; }

    // Format 0 or 1, tempo map applied across tracks (enough for the files tools/ writes).
    public static List<Ev> ReadSmf(string path)
    {
        byte[] b = File.ReadAllBytes(path);
        int ntrk = (b[10] << 8) | b[11], ppqn = (b[12] << 8) | b[13];
        var raw = new List<KeyValuePair<long, byte[]>>();
        var tempo = new List<KeyValuePair<long, int>>();
        int p = 14;
        for (int t = 0; t < ntrk; t++)
        {
            int len = (b[p + 4] << 24) | (b[p + 5] << 16) | (b[p + 6] << 8) | b[p + 7];
            int q = p + 8, end = q + len; long tick = 0; byte status = 0;
            while (q < end)
            {
                tick += Vlq(b, ref q);
                byte c = b[q];
                if (c == 0xff)
                {
                    byte type = b[q + 1]; q += 2; int l = Vlq(b, ref q);
                    if (type == 0x51) tempo.Add(new KeyValuePair<long, int>(tick, (b[q] << 16) | (b[q + 1] << 8) | b[q + 2]));
                    q += l;
                }
                else if (c == 0xf0 || c == 0xf7)
                {
                    q++; int l = Vlq(b, ref q);
                    var m = new byte[l + (c == 0xf0 ? 1 : 0)];
                    int o = 0; if (c == 0xf0) m[o++] = 0xf0;
                    Array.Copy(b, q, m, o, l); q += l;
                    raw.Add(new KeyValuePair<long, byte[]>(tick, m));
                }
                else
                {
                    if ((c & 0x80) != 0) { status = c; q++; }
                    int n = ((status & 0xf0) == 0xc0 || (status & 0xf0) == 0xd0) ? 1 : 2;
                    var m = new byte[n + 1]; m[0] = status; Array.Copy(b, q, m, 1, n); q += n;
                    raw.Add(new KeyValuePair<long, byte[]>(tick, m));
                }
            }
            p = end;
        }
        tempo.Sort((x, y) => x.Key.CompareTo(y.Key));
        raw.Sort((x, y) => x.Key.CompareTo(y.Key));
        var evs = new List<Ev>();
        foreach (var r in raw)
        {
            double sec = 0; long last = 0; int us = 500000;
            foreach (var tp in tempo) { if (tp.Key > r.Key) break; sec += (tp.Key - last) * us / 1e6 / ppqn; last = tp.Key; us = tp.Value; }
            sec += (r.Key - last) * us / 1e6 / ppqn;
            evs.Add(new Ev { t = sec, data = r.Value });
        }
        return evs;
    }

    // ---------------------------------------------------------------- run

    public static string Run(string midiPath, string outPath, string midiName, string wavePrefix,
                             double lead, double tail)
    {
        var evs = ReadSmf(midiPath);
        double last = evs.Count > 0 ? evs[evs.Count - 1].t : 0;
        double total = lead + last + tail;

        const int RATE = 48000, CH = 2, BUFMS = 50, NBUF = 16;
        var fmt = new WAVEFORMATEX { wFormatTag = 1, nChannels = CH, nSamplesPerSec = RATE,
            wBitsPerSample = 16, nBlockAlign = CH * 2, nAvgBytesPerSec = RATE * CH * 2, cbSize = 0 };
        uint bufBytes = (uint)(RATE * CH * 2 * BUFMS / 1000);

        IntPtr win, mout;
        uint r = waveInOpen(out win, FindWaveIn(wavePrefix), ref fmt, IntPtr.Zero, IntPtr.Zero, 0);
        if (r != 0) throw new Exception("waveInOpen failed: " + r);
        r = midiOutOpen(out mout, FindMidiOut(midiName), IntPtr.Zero, IntPtr.Zero, 0);
        if (r != 0) { waveInClose(win); throw new Exception("midiOutOpen failed: " + r); }

        var hdrs = new IntPtr[NBUF]; var datas = new IntPtr[NBUF];
        int hsize = Marshal.SizeOf(typeof(WAVEHDR));
        for (int i = 0; i < NBUF; i++)
        {
            datas[i] = Marshal.AllocHGlobal((int)bufBytes);
            hdrs[i] = Marshal.AllocHGlobal(hsize);
            Marshal.StructureToPtr(new WAVEHDR { lpData = datas[i], dwBufferLength = bufBytes }, hdrs[i], false);
            waveInPrepareHeader(win, hdrs[i], (uint)hsize);
            waveInAddBuffer(win, hdrs[i], (uint)hsize);
        }

        var pcm = new MemoryStream();
        timeBeginPeriod(1);
        var sw = Stopwatch.StartNew();
        waveInStart(win);
        int next = 0, ei = 0;
        try
        {
            while (sw.Elapsed.TotalSeconds < total)
            {
                double now = sw.Elapsed.TotalSeconds - lead;
                while (ei < evs.Count && evs[ei].t <= now)
                {
                    byte[] m = evs[ei].data;
                    if (m[0] == 0xf0) SendSysex(mout, m);
                    else midiOutShortMsg(mout, (uint)(m[0] | (m.Length > 1 ? m[1] << 8 : 0) | (m.Length > 2 ? m[2] << 16 : 0)));
                    ei++;
                }
                var h = (WAVEHDR)Marshal.PtrToStructure(hdrs[next], typeof(WAVEHDR));
                if ((h.dwFlags & WHDR_DONE) != 0)
                {
                    var tmp = new byte[h.dwBytesRecorded];
                    Marshal.Copy(h.lpData, tmp, 0, tmp.Length);
                    pcm.Write(tmp, 0, tmp.Length);
                    waveInUnprepareHeader(win, hdrs[next], (uint)hsize);
                    Marshal.StructureToPtr(new WAVEHDR { lpData = datas[next], dwBufferLength = bufBytes }, hdrs[next], false);
                    waveInPrepareHeader(win, hdrs[next], (uint)hsize);
                    waveInAddBuffer(win, hdrs[next], (uint)hsize);
                    next = (next + 1) % NBUF;
                }
                else Thread.Sleep(1);
            }
        }
        finally
        {
            midiOutReset(mout); midiOutClose(mout);
            waveInReset(win);
            for (int i = 0; i < NBUF; i++) { waveInUnprepareHeader(win, hdrs[i], (uint)hsize); Marshal.FreeHGlobal(hdrs[i]); Marshal.FreeHGlobal(datas[i]); }
            waveInClose(win);
            timeEndPeriod(1);
        }

        byte[] data = pcm.ToArray();
        using (var f = new BinaryWriter(File.Create(outPath)))
        {
            f.Write(new[] { (byte)'R', (byte)'I', (byte)'F', (byte)'F' }); f.Write(36 + data.Length);
            f.Write(new[] { (byte)'W', (byte)'A', (byte)'V', (byte)'E', (byte)'f', (byte)'m', (byte)'t', (byte)' ' });
            f.Write(16); f.Write((short)1); f.Write((short)CH); f.Write(RATE); f.Write(RATE * CH * 2);
            f.Write((short)(CH * 2)); f.Write((short)16);
            f.Write(new[] { (byte)'d', (byte)'a', (byte)'t', (byte)'a' }); f.Write(data.Length); f.Write(data);
        }

        // Peak, so a take that clipped or caught nothing says so at once.
        int peak = 0;
        for (int i = 0; i + 1 < data.Length; i += 2) { int v = Math.Abs((short)(data[i] | (data[i + 1] << 8))); if (v > peak) peak = v; }
        return string.Format("{0}: {1:F1} s, {2} MIDI events, peak {3} ({4:F1} dBFS)",
            outPath, data.Length / (double)(RATE * CH * 2), evs.Count, peak, 20 * Math.Log10(Math.Max(peak, 1) / 32768.0));
    }

    static void SendSysex(IntPtr mout, byte[] m)
    {
        IntPtr buf = Marshal.AllocHGlobal(m.Length);
        Marshal.Copy(m, 0, buf, m.Length);
        var hdr = new MIDIHDR { lpData = buf, dwBufferLength = (uint)m.Length, dwBytesRecorded = (uint)m.Length, dwReserved = new IntPtr[8] };
        int size = Marshal.SizeOf(typeof(MIDIHDR));
        IntPtr ph = Marshal.AllocHGlobal(size);
        Marshal.StructureToPtr(hdr, ph, false);
        midiOutPrepareHeader(mout, ph, (uint)size);
        midiOutLongMsg(mout, ph, (uint)size);
        for (int i = 0; i < 500; i++)
        {
            var h = (MIDIHDR)Marshal.PtrToStructure(ph, typeof(MIDIHDR));
            if ((h.dwFlags & MHDR_DONE) != 0) break;
            Thread.Sleep(1);
        }
        midiOutUnprepareHeader(mout, ph, (uint)size);
        Marshal.FreeHGlobal(ph); Marshal.FreeHGlobal(buf);
    }
}
