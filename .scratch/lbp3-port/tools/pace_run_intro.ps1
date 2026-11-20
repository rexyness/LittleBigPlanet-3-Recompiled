# analysis\pace_run.ps1 with "up" on Home (it opens on Create) and "left" on the Prologue map
# (the cursor now starts on the Legend of Bunkum; left selects the Introduction).
param([string]$Tag, [Parameter(ValueFromRemainingArguments = $true)][string[]]$EnvVars)
$root = "D:\Recomps\lbp3-recompiled"  # the clone
Set-Location $root
Get-Process lbp3 -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 1
Remove-Item analysis\input_cmd.txt -ErrorAction SilentlyContinue
# Start from the baseline and put the human's current saves back at the end, as saves.sh does
# (entering the Introduction advances the story save).
#   The marker analysis\savedata_live_is_test (see saves.sh) means the live saves are test
#   saves: then they are never kept as the human's.
#   The rest of user\savedata (guests' profiles, anything else) is kept whole in $kept.others and
#   put back exactly, as saves.sh's others_* do.
$kept = "analysis\savedata_human_kept"
$others = "$kept.others"
$testMark = "analysis\savedata_live_is_test"
function Other-Entries { Get-ChildItem -Force user\savedata | Where-Object { $_.Name -ne 'CUSA00473' } }
if (Test-Path $kept) { "saves: reusing the kept copy from an interrupted run ($kept)" }
elseif (Test-Path $testMark) {
    "saves: the live saves are test saves ($testMark) and there is no kept copy; restore the human's saves from analysis\savedata_backups, delete the marker, and run again."
    exit 1
}
else {
    Copy-Item -Recurse user\savedata\CUSA00473 $kept
    if (Test-Path "$kept.guests") { Move-Item "$kept.guests" $others }
    elseif (-not (Test-Path $others)) {
        New-Item -ItemType Directory $others | Out-Null
        Other-Entries | ForEach-Object { Copy-Item -Recurse $_.FullName $others }
    }
}
function Restore-Saves {
    New-Item -ItemType File -Force $testMark | Out-Null
    # Cleared only once kept (a reused kept copy from an interrupted run may lack them).
    if (Test-Path "$kept.guests") { Move-Item "$kept.guests" $others }
    if (-not (Test-Path $others)) {
        New-Item -ItemType Directory $others | Out-Null
        Other-Entries | ForEach-Object { Copy-Item -Recurse $_.FullName $others }
    }
    Other-Entries | Remove-Item -Recurse -Force
    Remove-Item -Recurse -Force user\savedata\CUSA00473
    Copy-Item -Recurse analysis\savedata_thruster_on user\savedata\CUSA00473
}
function Return-Saves {
    Get-Process lbp3 -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Seconds 1
    if (Test-Path "$kept.guests") { Move-Item "$kept.guests" $others }
    if (Test-Path $others) {
        Other-Entries | Remove-Item -Recurse -Force
        Get-ChildItem -Force $others | ForEach-Object { Copy-Item -Recurse $_.FullName user\savedata }
        Remove-Item -Recurse -Force $others
    }
    Remove-Item -Recurse -Force user\savedata\CUSA00473
    Copy-Item -Recurse $kept user\savedata\CUSA00473
    Remove-Item -Recurse -Force $kept
    Remove-Item -Force $testMark -ErrorAction SilentlyContinue
}
Restore-Saves
& .\tools\remote_start.ps1 @EnvVars LBP3_TRACE_SIM=1 LBP3_PACE_LOG=1
Start-Sleep -Seconds 28
Set-Content analysis\input_cmd.txt "cross`nwait 2000`nsquare`nwait 2500"
Start-Sleep -Seconds 6
Set-Content analysis\input_cmd.txt "up 100`nwait 700"
Start-Sleep -Seconds 1.5
& .\tools\capture.ps1 -Out "analysis\screenshots\pace_${Tag}_home.png" | Out-Null
Set-Content analysis\input_cmd.txt "cross`nwait 3000`ncross`nwait 6500`ncross`nwait 4500`nleft 100`nwait 1500`ncross`nwait 1000"
Start-Sleep -Seconds 40
& .\tools\capture.ps1 -Out "analysis\screenshots\pace_${Tag}_level.png" | Out-Null
python tools/camera_check.py 240 2>&1 | Select-Object -First 3
Start-Sleep -Seconds 20
Get-Process lbp3 -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 1
Copy-Item analysis\remote_stderr.txt "analysis\pace_${Tag}_stderr.txt" -Force
Copy-Item analysis\pace.csv "analysis\pace_${Tag}.csv" -Force
Return-Saves
Select-String -Path "analysis\pace_${Tag}_stderr.txt" -Pattern '\[video\] (display|vblank source)' | ForEach-Object Line
Select-String -Path "analysis\pace_${Tag}_stderr.txt" -Pattern 'frames flipped' | Select-Object -Last 4 | ForEach-Object { $_.Line -replace '.*frames flipped', '' }
