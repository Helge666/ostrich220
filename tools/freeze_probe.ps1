# Ostrich panel freeze probe (Windows, Cubase).
#
# The panel "freezes" when the host's plugin frame loses WS_VISIBLE while it stays on
# screen -- what WM_SETREDRAW(FALSE) does: no WM_PAINT reaches the view any more.
#
#   freeze_probe.ps1 -Status            the frame chain, physical pixels
#   freeze_probe.ps1 -Thaw              WM_SETREDRAW(TRUE) on the frame, redraw
#   freeze_probe.ps1 -Freeze            WM_SETREDRAW(FALSE) on the frame (the bug, by hand)
#   freeze_probe.ps1 -Watch [-Heal]     log every change of the chain (and thaw it)
#   freeze_probe.ps1 -Stress [-Count n] resize the frame n times, report any freeze
#   freeze_probe.ps1 -Press CUR>,CUR<   click panel keys (posted, works in the background)
#
# Key names: EDIT DATA MARK JUMP EXIT ENTER PART< PART> CUR< CUR> VAL- VAL+.  -Press
# prints the UI's key count after each click (ostrich-ui.log), so a click the panel did
# not take shows at once.

param([switch]$Status, [switch]$Thaw, [switch]$Freeze, [switch]$Watch, [switch]$Heal,
      [switch]$Stress, [int]$Count = 20, [int]$IntervalMs = 250, [string[]]$Press,
      [string]$Log = "$env:ProgramData\Ostrich\freeze-probe.log")

Add-Type -TypeDefinition @'
using System; using System.Text; using System.Runtime.InteropServices; using System.Collections.Generic;
public class FP {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr h, EnumProc p, IntPtr l);
  [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetParent(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern int GetWindowLong(IntPtr h, int i);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr c);
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool RedrawWindow(IntPtr h, IntPtr r, IntPtr g, uint f);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr a, int x, int y, int cx, int cy, uint f);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
  public static string Cls(IntPtr h){var s=new StringBuilder(256);GetClassName(h,s,256);return s.ToString();}
  public static string Txt(IntPtr h){var s=new StringBuilder(256);GetWindowText(h,s,256);return s.ToString();}
  public static bool Styled(IntPtr h){ return (GetWindowLong(h,-16) & 0x10000000) != 0; }
  public static List<IntPtr> Views(){
    var res=new List<IntPtr>();
    EnumWindows((t,l)=>{ EnumChildWindows(t,(c,l2)=>{ if(Cls(c).StartsWith("Ostrich")) res.Add(c); return true;},IntPtr.Zero); return true;},IntPtr.Zero);
    return res;
  }
  public static IntPtr Frame(IntPtr view){ IntPtr c=view, p; while((p=GetParent(c))!=IntPtr.Zero) c=p; return c; }
  public static string Desc(IntPtr h){ RECT r; GetWindowRect(h,out r);
    return String.Format("{0:X}[{1}] vis={2}{3}{4} {5}x{6} dpi={7}", h.ToInt64(), Cls(h), Styled(h)?"S":"-", IsWindowVisible(h)?"V":"-", IsIconic(h)?" iconic":"", r.R-r.L, r.B-r.T, GetDpiForWindow(h)); }
}
'@
[void][FP]::SetThreadDpiAwarenessContext([IntPtr]-4)   # per-monitor v2: physical pixels

function Chain($view) {
    $parts = @(); $c = $view
    while ($c -ne [IntPtr]::Zero) { $parts += [FP]::Desc($c); $c = [FP]::GetParent($c) }
    $parts -join ' < '
}
function Say($line) {
    $s = (Get-Date -Format 'yyyy-MM-dd HH:mm:ss.fff') + ' ' + $line
    $s; Add-Content -Path $Log -Value $s -Encoding utf8
}
function ThawFrame($frame) {
    [void][FP]::SendMessage($frame, 0x000B, [IntPtr]1, [IntPtr]0)
    [void][FP]::RedrawWindow($frame, [IntPtr]::Zero, [IntPtr]::Zero, 0x0185)
}
function Frozen($frame) { -not [FP]::Styled($frame) -and -not [FP]::IsIconic($frame) }

$views = [FP]::Views()
if ($views.Count -eq 0) { "no Ostrich view open"; exit 1 }

if ($Press) {
    # Key centres in the panel's design units (1150 wide, OstrichUI.cpp kKeyRects).
    $keys = @{ 'EDIT' = 699, 110; 'DATA' = 771, 110; 'MARK' = 699, 182; 'JUMP' = 771, 182;
               'EXIT' = 871, 110; 'ENTER' = 943, 110; 'PART<' = 1015, 110; 'PART>' = 1087, 110;
               'CUR<' = 871, 182; 'CUR>' = 943, 182; 'VAL-' = 1015, 182; 'VAL+' = 1087, 182 }
    $v = $views[0]
    $r = New-Object FP+RECT; [void][FP]::GetWindowRect($v, [ref]$r)
    # Physical pixels: this thread is per-monitor DPI aware (above).  A DPI-unaware sender
    # would see its posted coordinates scaled up by the window's DPI on the way in.
    $s = ($r.R - $r.L) / 1150.0
    foreach ($name in $Press) {
        $k = $keys[$name.ToUpper()]
        if (-not $k) { "unknown key $name"; continue }
        $x = [int]($k[0] * $s); $y = [int]($k[1] * $s)
        $lp = [IntPtr](($y -shl 16) -bor $x)
        [void][FP]::PostMessage($v, 0x0200, [IntPtr]0, $lp)
        [void][FP]::PostMessage($v, 0x0201, [IntPtr]1, $lp); Start-Sleep -Milliseconds 120
        [void][FP]::PostMessage($v, 0x0202, [IntPtr]0, $lp); Start-Sleep -Milliseconds 1200
        $last = Get-Content "$env:ProgramData\Ostrich\ostrich-ui.log" -Tail 1
        "$name -> " + ($last -replace '.*(keys \d+).*lcd (\[.*?\]\[.*?\]).*', '$1 $2')
    }
}

if ($Status -or -not ($Thaw -or $Freeze -or $Watch -or $Stress -or $Press)) {
    foreach ($v in $views) { Chain $v }
    $f = [FP]::GetForegroundWindow(); "foreground: " + [FP]::Txt($f)
}
if ($Freeze) { foreach ($v in $views) { [void][FP]::SendMessage([FP]::Frame($v), 0x000B, [IntPtr]0, [IntPtr]0) }; "frozen" }
if ($Thaw)   { foreach ($v in $views) { ThawFrame ([FP]::Frame($v)) }; "thawed" }

if ($Stress) {
    $v = $views[0]; $frame = [FP]::Frame($v)
    if (Frozen $frame) { ThawFrame $frame }
    $r = New-Object FP+RECT; [void][FP]::GetWindowRect($frame, [ref]$r)
    $w = $r.R - $r.L; $h = $r.B - $r.T
    Say ("stress start: " + (Chain $v))
    for ($i = 1; $i -le $Count; $i++) {
        $dw = Get-Random -Minimum -12 -Maximum 13
        [void][FP]::SetWindowPos($frame, [IntPtr]::Zero, 0, 0, $w + $dw, $h + [int]($dw / 4.6), 0x0016)  # NOMOVE|NOZORDER|NOACTIVATE
        Start-Sleep -Milliseconds $IntervalMs
        if (Frozen $frame) { Say ("FROZEN after resize $i (dw $dw): " + (Chain $v)); if ($Heal) { ThawFrame $frame } else { break } }
    }
    [void][FP]::SetWindowPos($frame, [IntPtr]::Zero, 0, 0, $w, $h, 0x0016)
    Start-Sleep -Milliseconds $IntervalMs
    Say ("stress end: " + (Chain $v))
}

if ($Watch) {
    $last = ''
    Say "watch start (Ctrl+C to stop)"
    while ($true) {
        foreach ($v in [FP]::Views()) {
            $frame = [FP]::Frame($v)
            $now = (Chain $v) + ' | fg: ' + [FP]::Txt([FP]::GetForegroundWindow())
            if ($now -ne $last) { Say $now; $last = $now }
            if ($Heal -and (Frozen $frame)) { Start-Sleep -Milliseconds 1000; if (Frozen $frame) { Say "healing"; ThawFrame $frame } }
        }
        Start-Sleep -Milliseconds $IntervalMs
    }
}
