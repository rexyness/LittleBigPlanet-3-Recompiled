# Called by play.bat before a session: copies the previous session's logs into
# analysis\play_logs\, named by the time the log was last written (the session's end), so they
# outlive play.bat's three-file rotation. Never fails the launch.
try {
    $log = "analysis\play_log.txt"
    if (Test-Path $log) {
        $stamp = (Get-Item $log).LastWriteTime.ToString("yyyyMMdd_HHmm")
        New-Item -ItemType Directory -Force "analysis\play_logs" | Out-Null
        $dest = "analysis\play_logs\play_log_$stamp.txt"
        if (-not (Test-Path $dest)) { Copy-Item $log $dest }
        $out = "analysis\play_stdout.txt"
        if (Test-Path $out) { Copy-Item $out "analysis\play_logs\play_stdout_$stamp.txt" -ErrorAction SilentlyContinue }
    }
} catch { }
