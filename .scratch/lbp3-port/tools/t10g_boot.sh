#!/bin/sh
# Gap 10: what the 7 s boot stall waits on. Every run's pacing log shows flips about 3.02 s, 2.008 s
# and 2.008 s apart near flip 60. The "60 frames flipped" line marks the end of the first gap, so
# F10 fires right then, inside the two 2 s gaps. With SYNC=1 in the environment it fires after
# "90 frames flipped" instead, with LBP3_TRACE_SYNC=1 (diag build), and keeps
# analysis\sync_trace.txt, which then covers the whole stall.
#   [SYNC=1] t10g_boot.sh <tag> [NAME=value ...]
cd /d/Recomps/lbp3-recompiled || exit 1
tag=$1; shift
log=analysis/remote_stderr.txt
at=60; extra=""
[ "$SYNC" = 1 ] && { at=90; extra="LBP3_TRACE_SYNC=1"; }
taskkill //f //im lbp3.exe >/dev/null 2>&1; sleep 1; rm -f analysis/input_cmd.txt
. /d/Recomps/lbp3-pc/.scratch/lbp3-port/tools/saves.sh; saves_keep; saves_baseline
powershell -NoProfile -Command "Set-Location D:\\Recomps\\lbp3-recompiled; & .\\tools\\remote_start.ps1 LBP3_PACE_LOG=1 $extra $*" >/dev/null || exit 1
n=0
until grep -q "^\\[video\\] $at frames flipped" "$log" 2>/dev/null || [ $n -ge 900 ]; do sleep 0.1; n=$((n + 1)); done
[ $n -ge 900 ] && { echo "$tag: never reached $at flips"; taskkill //f //im lbp3.exe >/dev/null 2>&1; exit 1; }
sleep 0.3
before=$(grep -c '^\[perf\] wrote' "$log")
printf 'f10\n' > analysis/input_cmd.txt
m=0
until [ "$(grep -c '^\[perf\] wrote' "$log")" -gt "$before" ] || [ $m -ge 300 ]; do sleep 0.1; m=$((m + 1)); done
f=$(grep '^\[perf\] wrote' "$log" | tail -1 | sed 's/.*wrote analysis\\perf\\//' | tr -d '\r')
[ -n "$f" ] && cp "analysis/perf/$f" "analysis/perf/${tag}_boot.txt" && echo "F10: $f saved as ${tag}_boot.txt"
sleep 8
taskkill //f //im lbp3.exe >/dev/null 2>&1; sleep 1
saves_return  # put the human's current saves back (saves.sh)
cp "$log" "analysis/${tag}_stderr.txt"; cp analysis/pace.csv "analysis/${tag}_pace.csv"
[ "$SYNC" = 1 ] && cp analysis/sync_trace.txt "analysis/${tag}_sync.txt"
echo "$tag: log analysis/${tag}_stderr.txt"
