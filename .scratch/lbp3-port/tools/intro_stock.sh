#!/bin/sh
# Milestone 5 (perf/intro_stalls.md): the stock Introduction route (pace_run_intro.ps1, which keeps
# and returns the saves itself) with intro_drive.py stamping the log on the frame split's clock.
#   intro_stock.sh <tag> [NAME=value ...]      e.g. intro_stock.sh is_s1 LBP3_FRAME_SPLIT=1
# Output in analysis/intro_stalls/: <tag>_stamped.txt, <tag>_split.csv (and pace_run_intro.ps1's
# analysis/pace_<tag>_stderr.txt, pace_<tag>.csv).
cd /d/Recomps/lbp3-recompiled || exit 1
tag=$1; shift
out=analysis/intro_stalls; mkdir -p "$out"
tools=/d/Recomps/lbp3-pc/.scratch/lbp3-port/tools
rm -f "$out/stop_$tag"
python -I $tools/intro_drive.py stamp "$out/${tag}_stamped.txt" --stop-file "$out/stop_$tag" --max-s 400 &
stamper=$!
sleep 1
powershell -NoProfile -ExecutionPolicy Bypass -File $tools/pace_run_intro.ps1 -Tag "$tag" "$@"
: > "$out/stop_$tag"
wait $stamper
rm -f "$out/stop_$tag"
[ -f analysis/frame_split.csv ] && cp analysis/frame_split.csv "$out/${tag}_split.csv"
echo "$tag: done"
