#!/bin/sh
# Gap 9 check: play the Legend of Bunkum into Needlepoint Peaks (map cursor starts on it, auto-skip
# off, so this needs the gap 8 fix), then walk right and jump for about 70 s with screenshots,
# looking for Sackboy being launched out of the level. Restores the saves before and after.
#   t09g_walk.sh <tag> [NAME=value ...]
cd /d/Recomps/lbp3-recompiled || exit 1
tag=$1; shift
taskkill //f //im lbp3.exe >/dev/null 2>&1; sleep 1; rm -f analysis/input_cmd.txt
. /d/Recomps/lbp3-pc/.scratch/lbp3-port/tools/saves.sh; saves_keep; saves_baseline || exit 1
powershell -NoProfile -Command "Set-Location D:\\Recomps\\lbp3-recompiled; & .\\tools\\remote_start.ps1 LBP3_CUTSCENE_AUTOSKIP=off $*" >/dev/null || exit 1
sleep 28
printf 'cross\nwait 2000\nsquare\nwait 2500\n' > analysis/input_cmd.txt; sleep 6
printf 'up 100\nwait 700\n' > analysis/input_cmd.txt; sleep 1.5
printf 'cross\nwait 3000\ncross\nwait 6500\ncross\nwait 4500\n' > analysis/input_cmd.txt; sleep 16
printf 'cross 100\n' > analysis/input_cmd.txt
# Wait up to 5 minutes for Needlepoint Peaks to load, then for the spawn.
n=0
until grep -q '01_needlepoint_peaks/needlepoint_peaks.farc' analysis/remote_stderr.txt || [ $n -ge 300 ]; do sleep 2; n=$((n + 2)); done
[ $n -ge 300 ] && { echo "$tag: Needlepoint Peaks never loaded"; taskkill //f //im lbp3.exe >/dev/null 2>&1; exit 1; }
sleep 30
powershell -NoProfile -File tools/capture.ps1 -Out "analysis/screenshots/${tag}_w00.png" >/dev/null
i=1
while [ $i -le 20 ]; do
  printf 'lstick 1 0 2600\nwait 100\ncross 400\nwait 400\n' > analysis/input_cmd.txt
  sleep 3.6
  [ $((i % 4)) -eq 0 ] && powershell -NoProfile -File tools/capture.ps1 -Out "analysis/screenshots/${tag}_w$(printf %02d $i).png" >/dev/null
  i=$((i + 1))
done
sleep 3
powershell -NoProfile -File tools/capture.ps1 -Out "analysis/screenshots/${tag}_wend.png" >/dev/null
cp analysis/remote_stderr.txt "analysis/${tag}_stderr.txt"
taskkill //f //im lbp3.exe >/dev/null 2>&1; sleep 2
saves_return  # put the human's current saves back (saves.sh)
echo "$tag: done; screenshots analysis/screenshots/${tag}_w*.png"
