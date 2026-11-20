# Pacing test run (analysis\ is git-ignored): start, go Pod -> Home -> Play -> Adventure ->
# Prologue -> Introduction, run camera_check, collect stats, stop. Logs go to analysis\pace_<Tag>_*.
# Copy this file to D:\Recomps\lbp3-recompiled\analysis\ first (it resolves the repo root from there).
#   .\analysis\pace_run.ps1 -Tag vblank_gl LBP3_PACING=vblank SDL_RENDER_DRIVER=opengl
param([string]$Tag, [Parameter(ValueFromRemainingArguments = $true)][string[]]$EnvVars)
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root
Get-Process lbp3 -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 1
Remove-Item analysis\input_cmd.txt -ErrorAction SilentlyContinue
& .\tools\remote_start.ps1 @EnvVars LBP3_TRACE_SIM=1 LBP3_PACE_LOG=1
Start-Sleep -Seconds 28
Set-Content analysis\input_cmd.txt "cross`nwait 2000`nsquare`nwait 2500"
Start-Sleep -Seconds 6
& .\tools\capture.ps1 -Out "analysis\screenshots\pace_${Tag}_home.png" | Out-Null
Set-Content analysis\input_cmd.txt "cross`nwait 3000`ncross`nwait 6500`ncross`nwait 4500`ncross`nwait 1000"
Start-Sleep -Seconds 38
& .\tools\capture.ps1 -Out "analysis\screenshots\pace_${Tag}_level.png" | Out-Null
python tools/camera_check.py 240 2>&1 | Select-Object -First 3
Start-Sleep -Seconds 20
Get-Process lbp3 -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 1
Copy-Item analysis\remote_stderr.txt "analysis\pace_${Tag}_stderr.txt" -Force
Copy-Item analysis\pace.csv "analysis\pace_${Tag}.csv" -Force
Select-String -Path "analysis\pace_${Tag}_stderr.txt" -Pattern '\[video\] (window|display|pacing|vblank)' | ForEach-Object Line
Select-String -Path "analysis\pace_${Tag}_stderr.txt" -Pattern '^\[pace\]' | Select-Object -Last 2 | ForEach-Object Line
Select-String -Path "analysis\pace_${Tag}_stderr.txt" -Pattern 'frames flipped' | Select-Object -Last 2 | ForEach-Object { $_.Line -replace '.*frames flipped', '' }
