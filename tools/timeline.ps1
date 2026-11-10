# Starts the game and saves a screenshot every N seconds, then stops it.
#   .\tools\timeline.ps1 -Every 20 -Count 8 -Name timeline [-AutoPress cross]
param([int]$Every = 20, [int]$Count = 8, [string]$Name = "timeline", [string]$AutoPress = "")

$root = Split-Path $PSScriptRoot -Parent
Set-Location $root
if ($AutoPress) { $env:LBP3_AUTOPRESS = $AutoPress }
# Test runs open on the monitor named in analysis\test_display.txt (a number or a rate such as
# "165hz"; LBP3_DISPLAY in runtime/src/video/presenter.cpp), unless LBP3_DISPLAY is set already.
$testDisplay = "$root\analysis\test_display.txt"
$setDisplay = -not $env:LBP3_DISPLAY -and (Test-Path $testDisplay)
if ($setDisplay) { $env:LBP3_DISPLAY = (Get-Content $testDisplay -TotalCount 1).Trim() }
$p = Start-Process -FilePath "$root\build\lbp3.exe" `
    -ArgumentList "--run", "--skip-intro" `
    -WorkingDirectory $root -RedirectStandardOutput "$root\analysis\shot_stdout.txt" `
    -RedirectStandardError "$root\analysis\shot_stderr.txt" -PassThru
$env:LBP3_AUTOPRESS = $null
if ($setDisplay) { $env:LBP3_DISPLAY = $null }
for ($i = 1; $i -le $Count; $i++) {
    & "$PSScriptRoot\capture.ps1" -Out ("analysis\screenshots\{0}_{1:D2}.png" -f $Name, ($i * $Every)) -Delay $Every | Out-Null
}
Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
Write-Host "done"
