# Print the game window's client size as a DPI-unaware and as a DPI-aware caller sees it, plus its DPI.
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class WinDpi {
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hwnd, out RECT r);
    [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr ctx);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
"@
$p = Get-Process lbp3 -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $p) { "game not running"; exit 1 }
$h = $p.MainWindowHandle
$r = New-Object WinDpi+RECT
[WinDpi]::GetClientRect($h, [ref]$r) | Out-Null
"unaware caller: client $($r.Right)x$($r.Bottom)"
[WinDpi]::SetThreadDpiAwarenessContext([IntPtr](-4)) | Out-Null  # per-monitor aware v2
[WinDpi]::GetClientRect($h, [ref]$r) | Out-Null
"aware caller:   client $($r.Right)x$($r.Bottom); window DPI $([WinDpi]::GetDpiForWindow($h)) (96 = 100%)"
