#!/bin/sh
# Gap 10 starting point: what the Introduction's level load waits on. Takes the route into the
# Introduction (as pace_run_intro.ps1), fires F10 as soon as craftworld.farc opens and again when
# that report is written, then lets the level run. F10 dumps every thread's stack into the log and
# samples the main and GPU threads for 3 s (analysis\perf\perfNN.txt).
#   t10g_load.sh <tag> [NAME=value ...]
cd /d/Recomps/lbp3-recompiled || exit 1
tag=$1; shift
log=analysis/remote_stderr.txt
taskkill //f //im lbp3.exe >/dev/null 2>&1; sleep 1; rm -f analysis/input_cmd.txt
. /d/Recomps/lbp3-pc/.scratch/lbp3-port/tools/saves.sh; saves_keep; saves_baseline
powershell -NoProfile -Command "Set-Location D:\\Recomps\\lbp3-recompiled; & .\\tools\\remote_start.ps1 LBP3_TRACE_SIM=1 LBP3_PACE_LOG=1 $*" >/dev/null || exit 1
sleep 28
printf 'cross\nwait 2000\nsquare\nwait 2500\n' > analysis/input_cmd.txt; sleep 6
printf 'up 100\nwait 700\n' > analysis/input_cmd.txt; sleep 1.5
printf 'cross\nwait 3000\ncross\nwait 6500\ncross\nwait 4500\nleft 100\nwait 1500\ncross\nwait 1000\n' > analysis/input_cmd.txt
n=0
until grep -q '00_craftworld/craftworld.farc' "$log" || [ $n -ge 600 ]; do sleep 0.1; n=$((n + 1)); done
[ $n -ge 600 ] && { echo "$tag: craftworld.farc never opened"; taskkill //f //im lbp3.exe >/dev/null 2>&1; exit 1; }
for k in 1 2; do
  before=$(grep -c '^\[perf\] wrote' "$log")
  printf 'f10\n' > analysis/input_cmd.txt
  m=0
  until [ "$(grep -c '^\[perf\] wrote' "$log")" -gt "$before" ] || [ $m -ge 300 ]; do sleep 0.1; m=$((m + 1)); done
  f=$(grep '^\[perf\] wrote' "$log" | tail -1 | sed 's/.*wrote analysis\\perf\\//' | tr -d '\r')
  [ -n "$f" ] && cp "analysis/perf/$f" "analysis/perf/${tag}_load${k}.txt" && echo "F10 $k: $f saved as ${tag}_load${k}.txt"
done
sleep 25
taskkill //f //im lbp3.exe >/dev/null 2>&1; sleep 1
saves_return  # put the human's current saves back (saves.sh)
cp "$log" "analysis/${tag}_stderr.txt"; cp analysis/pace.csv "analysis/${tag}_pace.csv"
echo "$tag: log analysis/${tag}_stderr.txt, pacing analysis/${tag}_pace.csv"
