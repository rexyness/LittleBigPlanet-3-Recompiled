# Starts the LBPPC test copy (D:\Recomps\lbp3-wt\lbppc-test, its own data folder) signed in as a test
# account, muted (mutepid; Windows remembers a per-app mute, so lbppc_stop.ps1 unmutes before stopping),
# UPnP off, scripted pad input from pad.txt. The password is read from the account file into this
# process's environment only, never printed. Prints the game's pid.
param([string]$Tag, [string]$AccountFile = "D:\Recomps\lbp3-wt\coop\B\user\config\online_account.txt")
$T = $env:LBPPC_TEST_TMP; $Test = "D:\Recomps\lbp3-wt\lbppc-test"  # logs in $T; fpsample/mutepid exes in $T\fpsample (README.md)
if (-not $T) { "set LBPPC_TEST_TMP first (README.md)"; exit 1 }
$name = $null; $pw = $null
foreach ($line in Get-Content $AccountFile) {
    if ($line -match '^\s*name\s*=\s*(.+?)\s*$') { $name = $Matches[1] }
    if ($line -match '^\s*password\s*=\s*(.+?)\s*$') { $pw = $Matches[1] }
}
if (-not $name -or -not $pw) { "no name or password in the account file"; exit 1 }
Set-Content -Path "$Test\pad.txt" -Value "" -NoNewline
$env:LBPPC_DATA_DIR = $Test
$env:LBPPC_SERVER = "https://play.lbppc.com"
$env:BB_SHADNET_NPID = $name
$env:BB_SHADNET_PASSWORD = $pw
$env:BB_SHADNET_SERVER = "shadnet.lbppc.com:31313"
$env:BB_SHADNET_WEBAPI = "https://api.lbppc.com"
$env:BB_SHADNET_TLS = "1"
$env:LBPPC_VERSION = "0.1.5"
$env:BB_UPNP = "0"
$env:BB_PAD_FILE = "$Test\pad.txt"
$env:LBPPC_POS_LOG = "1"
Start-Process -FilePath "$T\fpsample\mutepid.exe" -ArgumentList @("LBPPC-Game.exe", "3600") -RedirectStandardOutput "$T\mute_$Tag.txt" -WindowStyle Hidden | Out-Null
$l = Start-Process -FilePath "D:\Recomps\lbppc\LBPPC.exe" -ArgumentList @("--script", "D:\Recomps\lbppc\run.py", "D:\Recomps\ROMs\PS4\CUSA00473") `
    -WorkingDirectory "D:\Recomps\lbppc" -RedirectStandardOutput "$T\lbppc_$Tag.txt" -RedirectStandardError "$T\lbppc_${Tag}_err.txt" -PassThru
$g = $null; for ($i = 0; $i -lt 60 -and -not $g; $i++) { Start-Sleep 2; $g = Get-Process -Name "LBPPC-Game" -ErrorAction SilentlyContinue | Select-Object -First 1 }
if ($g) { "$($g.Id) $($l.Id) $name" } else { "no game" }
