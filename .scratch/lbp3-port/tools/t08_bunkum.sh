#!/bin/sh
# Gap 8 check: play the Legend of Bunkum (map cursor starts on it) with the auto-skip off,
# report which voice lines played and whether Needlepoint Peaks loaded, then restore the saves.
#   t08_bunkum.sh <tag> [NAME=value ...]
cd /d/Recomps/lbp3-recompiled || exit 1
tag=$1; shift
taskkill //f //im lbp3.exe >/dev/null 2>&1; sleep 1; rm -f analysis/input_cmd.txt
. /d/Recomps/lbp3-recompiled/.scratch/lbp3-port/tools/saves.sh; saves_keep; saves_baseline || exit 1
powershell -NoProfile -Command "Set-Location D:\\Recomps\\lbp3-recompiled; & .\\tools\\remote_start.ps1 LBP3_CUTSCENE_AUTOSKIP=off $*" >/dev/null || exit 1
sleep 28
printf 'cross\nwait 2000\nsquare\nwait 2500\n' > analysis/input_cmd.txt; sleep 6
printf 'up 100\nwait 700\n' > analysis/input_cmd.txt; sleep 1.5
printf 'cross\nwait 3000\ncross\nwait 6500\ncross\nwait 4500\n' > analysis/input_cmd.txt; sleep 16
powershell -NoProfile -File tools/capture.ps1 -Out "analysis/screenshots/${tag}_map.png" >/dev/null
printf 'cross 100\n' > analysis/input_cmd.txt
sleep 200
powershell -NoProfile -File tools/capture.ps1 -Out "analysis/screenshots/${tag}_end.png" >/dev/null
cp analysis/remote_stderr.txt "analysis/${tag}_stderr.txt"
taskkill //f //im lbp3.exe >/dev/null 2>&1; sleep 2
saves_return  # put the human's current saves back (saves.sh)
lines=$(grep 'cs01_' "analysis/${tag}_stderr.txt" | grep -c ' open ')
needle=$(grep -c '01_needlepoint_peaks/needlepoint_peaks.farc' "analysis/${tag}_stderr.txt")
echo "$tag: $lines of 16 voice lines played; needlepoint loaded: $([ "$needle" -gt 0 ] && echo yes || echo no)"
