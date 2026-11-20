# Screenshot of one game window (by process id), PrintWindow only: cap.ps1 -ProcId 123 -Out x.png
# Picks the process's SDL window (the game may also own a console window).
param([int]$ProcId, [string]$Out)
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class WinCap5 {
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f, IntPtr l);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hwnd, out RECT r);
    [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr ctx);
    [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    public static IntPtr Find(uint want) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((h, l) => {
            uint pid; GetWindowThreadProcessId(h, out pid);
            if (pid != want || !IsWindowVisible(h)) return true;
            var sb = new StringBuilder(256); GetClassName(h, sb, 256);
            if (sb.ToString() != "SDL_app") return true;
            found = h; return false;
        }, IntPtr.Zero);
        return found;
    }
}
"@
[void][WinCap5]::SetThreadDpiAwarenessContext([IntPtr](-4))
$h = [WinCap5]::Find([uint32]$ProcId)
if ($h -eq [IntPtr]::Zero) { Write-Host "no window"; exit 1 }
if ([WinCap5]::IsIconic($h)) { [void][WinCap5]::ShowWindow($h, 4); Start-Sleep -Milliseconds 1500 }  # SW_SHOWNOACTIVATE
$r = New-Object WinCap5+RECT
[WinCap5]::GetClientRect($h, [ref]$r) | Out-Null
$bmp = New-Object System.Drawing.Bitmap ([Math]::Max(1, $r.Right)), ([Math]::Max(1, $r.Bottom))
$g = [System.Drawing.Graphics]::FromImage($bmp)
$hdc = $g.GetHdc()
[WinCap5]::PrintWindow($h, $hdc, 3) | Out-Null
$g.ReleaseHdc($hdc)
$small = New-Object System.Drawing.Bitmap ([int]($bmp.Width / 2)), ([int]($bmp.Height / 2))
$g2 = [System.Drawing.Graphics]::FromImage($small)
$g2.DrawImage($bmp, 0, 0, $small.Width, $small.Height)
$small.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$g2.Dispose(); $small.Dispose(); $g.Dispose(); $bmp.Dispose()
Write-Host "saved $Out"
