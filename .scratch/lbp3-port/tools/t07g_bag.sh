#!/bin/sh
# Gap 7 check: boot, enter Create, open the Popit, select the Tools Bag, open it, screenshot,
# and print where the bag page's left edge rests (fraction of the window width).
# On a PS4 the page spans 72.8%-97.6% of the width; the port showed about 91%.
#   t07g_bag.sh <tag> [NAME=value ...]
S="$(cd "$(dirname "$0")" && pwd -W)"
cd /d/Recomps/lbp3-recompiled || exit 1
tag=$1; shift
taskkill //f //im lbp3.exe >/dev/null 2>&1; sleep 1; rm -f analysis/input_cmd.txt
. /d/Recomps/lbp3-pc/.scratch/lbp3-port/tools/saves.sh; saves_keep; saves_baseline
powershell -NoProfile -Command "Set-Location D:\\Recomps\\lbp3-recompiled; & .\\tools\\remote_start.ps1 $*" >/dev/null || exit 1
sleep 28
printf 'cross\nwait 2000\nsquare\nwait 2500\n' > analysis/input_cmd.txt; sleep 6
printf 'square 100\n' > analysis/input_cmd.txt; sleep 28
printf 'square 100\nwait 1500\n' > analysis/input_cmd.txt; sleep 2
printf 'down 100\nwait 700\n' > analysis/input_cmd.txt; sleep 1.5
powershell -NoProfile -File tools/capture.ps1 -Out "analysis/screenshots/${tag}_popit.png" >/dev/null
printf 'cross 100\nwait 2000\n' > analysis/input_cmd.txt; sleep 2.8
powershell -NoProfile -File tools/capture.ps1 -Out "analysis/screenshots/${tag}_bag.png" >/dev/null
sleep 3
powershell -NoProfile -File tools/capture.ps1 -Out "analysis/screenshots/${tag}_bag_late.png" >/dev/null
cp analysis/remote_stderr.txt "analysis/${tag}_stderr.txt"
taskkill //f //im lbp3.exe >/dev/null 2>&1; sleep 2
saves_return  # put the human's current saves back (saves.sh)
python "$S/panel_edge.py" "analysis/screenshots/${tag}_bag.png" "analysis/screenshots/${tag}_bag_late.png"
