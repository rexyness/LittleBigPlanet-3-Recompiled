# Screenshot the game window every few seconds into a 6-slot rotation, until the game exits:
#   rolling_capture.ps1 <tag> [seconds]
# Files: D:\Recomps\lbp3-recompiled\analysis\screenshots\<tag>_roll<N>.png, plus <tag>_roll.txt
# listing slot, time and frame count so the last frames before a freeze can be found.
param([string]$Tag = "roll", [int]$Every = 4)
$root = "D:\Recomps\lbp3-recompiled"
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class RollCap {
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hwnd, out RECT r);
    [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr ctx);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
"@
# Per-monitor DPI aware, so the client size is in physical pixels. Unaware, at 125% scaling the
# bitmap came out 80% of the window's size and PrintWindow filled only its top-left part.
[void][RollCap]::SetThreadDpiAwarenessContext([IntPtr](-4))
$dir = "$root\analysis\screenshots"
New-Item -ItemType Directory -Force $dir | Out-Null
$index = "$dir\${Tag}_roll.txt"
Set-Content $index "slot`ttime`tlast frames-flipped line"
$n = 0
while ($true) {
    $p = Get-Process lbp3 -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $p) { break }
    if ($p.MainWindowHandle -ne [IntPtr]::Zero) {
        $r = New-Object RollCap+RECT
        [void][RollCap]::GetClientRect($p.MainWindowHandle, [ref]$r)
        if ($r.Right -gt 1 -and $r.Bottom -gt 1) {
            # Half size keeps each capture small and quick.
            $bmp = New-Object System.Drawing.Bitmap $r.Right, $r.Bottom
            $g = [System.Drawing.Graphics]::FromImage($bmp)
            $hdc = $g.GetHdc()
            [void][RollCap]::PrintWindow($p.MainWindowHandle, $hdc, 3)
            $g.ReleaseHdc($hdc)
            $small = New-Object System.Drawing.Bitmap $bmp, ([int]($r.Right / 2)), ([int]($r.Bottom / 2))
            $slot = $n % 6
            $small.Save("$dir\${Tag}_roll$slot.png", [System.Drawing.Imaging.ImageFormat]::Png)
            $small.Dispose(); $g.Dispose(); $bmp.Dispose()
            $frames = (Select-String -Path "$root\analysis\remote_stderr.txt" -Pattern '^\[video\] (\d+) frames flipped' |
                Select-Object -Last 1).Matches.Groups[1].Value
            Add-Content $index ("{0}`t{1:HH:mm:ss}`t{2}" -f $slot, (Get-Date), $frames)
            $n++
        }
    }
    Start-Sleep -Seconds $Every
}
