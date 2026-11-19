# Start one test instance: inst.ps1 -Root D:\Recomps\lbp3-wt\coop\A -Tag r1 -Exe <lbp3.exe> NAME=value ...
param([string]$Root, [string]$Tag, [string]$Exe, [Parameter(ValueFromRemainingArguments = $true)][string[]]$EnvVars)
$env:LBP3_REMOTE_INPUT = "1"
foreach ($kv in $EnvVars) { $n, $v = $kv -split "=", 2; Set-Item -Path "env:$n" -Value $v }
New-Item -ItemType Directory -Force "$Root\analysis" | Out-Null
Remove-Item "$Root\analysis\input_cmd.txt" -ErrorAction SilentlyContinue
$p = Start-Process -FilePath $Exe -ArgumentList @("--run", "--skip-intro") -WorkingDirectory $Root `
    -RedirectStandardOutput "$Root\stdout_$Tag.txt" -RedirectStandardError "$Root\stderr_$Tag.txt" -PassThru
Write-Output $p.Id
