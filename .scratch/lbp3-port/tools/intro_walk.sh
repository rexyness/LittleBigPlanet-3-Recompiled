#!/bin/sh
# Milestone 5, the Introduction's transition stalls (perf/intro_stalls.md). pace_run_intro.ps1's
# route stops short of them (it stands at the spawn, then camera_check walks 12 s), so this takes
# the same menu route into the Introduction and then walks right with jumps (t09g_walk.sh's loop)
# past the tutorial video and Newton's entrance until 25 s after his story save. intro_drive.py
# drives it and stamps every log line with the QPC clock frame_split.csv uses. Saves: saves.sh
# (keep, the Thruster baseline, put back on exit by any path).
#   intro_walk.sh <tag> [intro_drive.py options --] [NAME=value ...]
#   SAVES_BASELINE=<dir> (saves.sh) starts from another save copy, e.g. the human's before_t09_nockpt_1852.
#   e.g. intro_walk.sh is_w1 -- LBP3_FRAME_SPLIT=1 LBP3_TRACE_READS=1000000
#        intro_walk.sh is_f1 --f10-on 'pattern' --f10-delay-ms 0 -- LBP3_FRAME_SPLIT=1
# Output in analysis/intro_stalls/: <tag>_stamped.txt, <tag>_stderr.txt, <tag>_split.csv,
# <tag>_pace.csv, and <tag>_perf.txt when F10 fired.
cd /d/Recomps/lbp3-recompiled || exit 1
tag=$1; shift
drive=""
while [ $# -gt 0 ] && [ "$1" != "--" ]; do drive="$drive '$1'"; shift; done
[ "$1" = "--" ] && shift
out=analysis/intro_stalls; mkdir -p "$out"
tools=/d/Recomps/lbp3-pc/.scratch/lbp3-port/tools
taskkill //f //im lbp3.exe >/dev/null 2>&1; sleep 1; rm -f analysis/input_cmd.txt
. $tools/saves.sh; saves_keep; saves_baseline || exit 1
powershell -NoProfile -Command "Set-Location D:\\Recomps\\lbp3-recompiled; & .\\tools\\remote_start.ps1 LBP3_TRACE_SIM=1 LBP3_PACE_LOG=1 $*" >/dev/null || exit 1
eval "python -I $tools/intro_drive.py walk \"$out/${tag}_stamped.txt\" $drive"
echo "$tag: driver exit $?"
# Pending F10 reports (and LBP3_SAMPLE) finish before the kill (they take a few seconds).
n=0
while [ "$(grep -c '^\[perf\] sampling' analysis/remote_stderr.txt)" -gt "$(grep -c '^\[perf\] wrote' analysis/remote_stderr.txt)" ] && [ $n -lt 30 ]; do sleep 1; n=$((n + 1)); done
taskkill //f //im lbp3.exe >/dev/null 2>&1; sleep 1
saves_return  # put the human's current saves back (saves.sh)
cp analysis/remote_stderr.txt "$out/${tag}_stderr.txt"
cp analysis/pace.csv "$out/${tag}_pace.csv" 2>/dev/null
[ -f analysis/frame_split.csv ] && cp analysis/frame_split.csv "$out/${tag}_split.csv"
k=0
for f in $(grep '^\[perf\] wrote' analysis/remote_stderr.txt | sed 's/.*wrote analysis\\perf\\//' | tr -d '\r'); do
  k=$((k + 1)); cp "analysis/perf/$f" "$out/${tag}_perf$k.txt" && echo "$tag: F10 report $f -> $out/${tag}_perf$k.txt"
done
grep -c 'np_tu01b_01_newton.mp3  ->' "$out/${tag}_stderr.txt" | sed "s/^/$tag: newton opened: /"
grep -o 'open /savedata0/00000001/playfart_[0-9_]*' "$out/${tag}_stderr.txt" | sed "s/^/$tag: /"
echo "$tag: done"
