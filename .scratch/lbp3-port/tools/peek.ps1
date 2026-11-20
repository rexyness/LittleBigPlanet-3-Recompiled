# Read 8-byte words from another process: peek.ps1 <pid> <hexaddr> <count>
param([int]$ProcId, [string]$Addr, [int]$Count = 16)
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class Peek {
    [DllImport("kernel32.dll")] public static extern IntPtr OpenProcess(int access, bool inherit, int pid);
    [DllImport("kernel32.dll")] public static extern bool ReadProcessMemory(IntPtr h, IntPtr addr, byte[] buf, IntPtr size, out IntPtr read);
    [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);
}
"@
$h = [Peek]::OpenProcess(0x0010 -bor 0x0400, $false, $ProcId)  # VM_READ | QUERY_INFORMATION
if ($h -eq [IntPtr]::Zero) { throw "OpenProcess failed" }
$base = [Convert]::ToInt64($Addr, 16)
$buf = New-Object byte[] ($Count * 8)
$read = [IntPtr]::Zero
if (-not [Peek]::ReadProcessMemory($h, [IntPtr]$base, $buf, [IntPtr]$buf.Length, [ref]$read)) { throw "read failed" }
for ($i = 0; $i -lt $Count; $i++) {
    '{0:x}: {1:x16}' -f ($base + 8 * $i), [BitConverter]::ToUInt64($buf, 8 * $i)
}
[void][Peek]::CloseHandle($h)
