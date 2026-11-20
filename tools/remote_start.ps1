# Starts the game for a remote-controlled test session: input comes from
# analysis\input_cmd.txt (see runtime/src/input/gamepad_sdl.cpp, LBP3_REMOTE_INPUT).
# Extra environment variables can be passed as "NAME=value" arguments, and arguments
# starting with "--" go to lbp3.exe:
#   .\tools\remote_start.ps1 LBP3_READBACKS=relaxed --break=eboot.bin+0x1234
param([Parameter(ValueFromRemainingArguments = $true)][string[]]$EnvVars)

$root = Split-Path $PSScriptRoot -Parent
Set-Location $root
$env:LBP3_REMOTE_INPUT = "1"
# Scripted runs get the plain on-screen keyboard (no word suggestions, nothing learned, its place not
# kept), so what a run types and where its presses land don't depend on this PC's learned words. Pass
# LBP3_OSK_PLAIN=0 to test the suggestions.
$setPlain = -not $env:LBP3_OSK_PLAIN
if ($setPlain) { $env:LBP3_OSK_PLAIN = "1" }
$exeArgs = @("--run", "--skip-intro")
$exeArgs += @($EnvVars | Where-Object { $_ -like "--*" })
$EnvVars = @($EnvVars | Where-Object { $_ -and $_ -notlike "--*" })
foreach ($kv in $EnvVars) {
    $name, $value = $kv -split "=", 2
    Set-Item -Path "env:$name" -Value $value
}
# Scripted runs don't see this PC's physical controllers: SDL's ignore hint, read from the environment,
# with an "except" list that names no real device. With the user's DualSense connected, the Edit Now
# route opened the Popit and every keyboard run failed (2026-11-20); and a press on a real pad would
# land in the run. LBP3_REAL_PADS=1 keeps them (hitl_start.sh: the human plays), as does a caller that
# sets the hint itself.
$noPadHint = "SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT"
$setNoPads = ($env:LBP3_REAL_PADS -ne "1") -and -not (Test-Path "env:$noPadHint")
if ($setNoPads) { Set-Item -Path "env:$noPadHint" -Value "0x0000/0x0000" }
# Test runs open on the monitor named in analysis\test_display.txt (a number or a rate such as
# "165hz"; LBP3_DISPLAY in runtime/src/video/presenter.cpp), unless LBP3_DISPLAY is set already.
$testDisplay = "$root\analysis\test_display.txt"
$setDisplay = -not $env:LBP3_DISPLAY -and (Test-Path $testDisplay)
if ($setDisplay) { $env:LBP3_DISPLAY = (Get-Content $testDisplay -TotalCount 1).Trim() }
$p = Start-Process -FilePath "$root\build\lbp3.exe" `
    -ArgumentList $exeArgs `
    -WorkingDirectory $root -RedirectStandardOutput "$root\analysis\remote_stdout.txt" `
    -RedirectStandardError "$root\analysis\remote_stderr.txt" -PassThru
$env:LBP3_REMOTE_INPUT = $null
foreach ($kv in $EnvVars) { Remove-Item -Path ("env:" + ($kv -split "=", 2)[0]) -ErrorAction SilentlyContinue }
if ($setDisplay) { $env:LBP3_DISPLAY = $null }
if ($setPlain) { $env:LBP3_OSK_PLAIN = $null }
if ($setNoPads) { Remove-Item -Path "env:$noPadHint" -ErrorAction SilentlyContinue }
Write-Host "started pid $($p.Id)"
