# Stops the LBPPC test copy: first unmutes its audio sessions (Windows keeps a per-app mute by the
# executable's path, which the user's own LBPPC shares), then stops the muter, the game and its launcher.
param([string]$Tag = "stop")
$T = $env:LBPPC_TEST_TMP  # where lbppc_start.ps1 ran from (README.md)
if (-not $T) { "set LBPPC_TEST_TMP first (README.md)"; exit 1 }
Get-Process -Name "mutepid" -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
& "$T\fpsample\mutepid.exe" "LBPPC-Game.exe" 2 unmute
Get-Process -Name "LBPPC-Game", "LBPPC" -ErrorAction SilentlyContinue | ForEach-Object { Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue; "stopped $($_.Name) $($_.Id)" }
