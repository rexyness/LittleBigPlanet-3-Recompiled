# Picture of the game window with the game's own dialog windows on top, at their real screen
# positions, made from PrintWindow captures of each window of lbp3.exe. Unlike a screen grab it
# never shows other applications that may cover the game.
#   capture_dialog.ps1 -Out <png>     (path relative to D:\Recomps\lbp3-recompiled)
param([string]$Out = "analysis\screenshots\dialog.png")
$root = "D:\Recomps\lbp3-recompiled"
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class DlgCap {
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f, IntPtr l);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint f);
    [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr ctx);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
    public static List<IntPtr> Windows(uint pid) {
        var l = new List<IntPtr>();
        EnumWindows((h, x) => { uint p; GetWindowThreadProcessId(h, out p); if (p == pid && IsWindowVisible(h)) l.Add(h); return true; }, IntPtr.Zero);
        return l;
    }
    public static string Cls(IntPtr h) { var sb = new StringBuilder(64); GetClassName(h, sb, 64); return sb.ToString(); }
}
"@
[void][DlgCap]::SetThreadDpiAwarenessContext([IntPtr](-4))
$p = Get-Process lbp3 -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $p) { Write-Host "game not running"; exit 1 }
$wins = [DlgCap]::Windows([uint32]$p.Id)
$game = $null; $best = 0
foreach ($h in $wins) { if ([DlgCap]::Cls($h) -eq "SDL_app") { $r = New-Object DlgCap+RECT; [DlgCap]::GetClientRect($h, [ref]$r) | Out-Null; $a = $r.Right * $r.Bottom; if ($a -gt $best) { $best = $a; $game = $h } } }
if (-not $game) { Write-Host "no SDL_app window"; exit 1 }
$gr = New-Object DlgCap+RECT; [DlgCap]::GetClientRect($game, [ref]$gr) | Out-Null
$go = New-Object DlgCap+POINT; [DlgCap]::ClientToScreen($game, [ref]$go) | Out-Null
$bmp = New-Object System.Drawing.Bitmap $gr.Right, $gr.Bottom
$g = [System.Drawing.Graphics]::FromImage($bmp)
$hdc = $g.GetHdc(); [DlgCap]::PrintWindow($game, $hdc, 3) | Out-Null; $g.ReleaseHdc($hdc)
$info = @()
foreach ($h in $wins) {
    if ($h -eq $game -or [DlgCap]::Cls($h) -eq "ConsoleWindowClass") { continue }
    $wr = New-Object DlgCap+RECT; [DlgCap]::GetWindowRect($h, [ref]$wr) | Out-Null
    $w = $wr.Right - $wr.Left; $hh = $wr.Bottom - $wr.Top
    if ($w -le 1 -or $hh -le 1) { continue }
    $db = New-Object System.Drawing.Bitmap $w, $hh
    $dg = [System.Drawing.Graphics]::FromImage($db)
    $dh = $dg.GetHdc(); [DlgCap]::PrintWindow($h, $dh, 2) | Out-Null; $dg.ReleaseHdc($dh); $dg.Dispose()
    $x = $wr.Left - $go.X; $y = $wr.Top - $go.Y
    $g.DrawImage($db, $x, $y, $w, $hh); $db.Dispose()
    $cx = $x + $w / 2; $cy = $y + $hh / 2
    $info += ("{0} {1}x{2} at {3},{4} (centre {5},{6}; game centre {7},{8})" -f [DlgCap]::Cls($h), $w, $hh, $x, $y, $cx, $cy, ($gr.Right / 2), ($gr.Bottom / 2))
}
New-Item -ItemType Directory -Force (Split-Path "$root\$Out") | Out-Null
$bmp.Save("$root\$Out", [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
Write-Host "saved $Out; game client $($gr.Right)x$($gr.Bottom)"
$info | ForEach-Object { Write-Host "  window $_" }
# Dialogs drawn over the game with ImGui (the overlay) are part of the game window's frame.
if ($info.Count -eq 0) { Write-Host "  no dialog windows (an overlay dialog is in the game frame)" }
