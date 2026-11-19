#!/bin/sh
# Gap 17 (b): story progress survives a quit and a relaunch. Adapted from t08_bunkum.sh, but the
# saves are restored from the baseline only once, before the first launch:
#   1  Pod -> Home -> Play -> Adventure -> Prologue -> the map (cursor on the Legend of Bunkum),
#      screenshots of each menu, play the Legend of Bunkum (auto-skip off) until the game loads
#      Needlepoint Peaks by itself, wait until the save files stop changing, quit (kill).
#   2  Relaunch on the same saves, the same menus to the map, screenshots of each menu, then the
#      map cursor moved one node down (the Legend of Bunkum).
# Compare m2_<tag>_1_{world,map}.png with m2_<tag>_2_{world,map}.png. Save files are listed in
# analysis/m2_<tag>_saves.txt after each step.
#   t17_story.sh <tag> [NAME=value ...]
cd /d/Recomps/lbp3-recompiled || exit 1
tag=$1; shift
. /d/Recomps/lbp3-pc/.scratch/lbp3-port/tools/saves.sh; saves_keep; saves_baseline || exit 1
. /d/Recomps/lbp3-pc/.scratch/lbp3-port/tools/m2_lib.sh
log="analysis/m2_${tag}_saves.txt"; : > "$log"
note() { echo "$*"; echo "$*" >> "$log"; }
listing() { note "-- saves $1 ($(date +%H:%M:%S))"; saves_ls >> "$log"; }
# Is Play the highlighted Home item? (green label; blue when not highlighted)
home_on_play() {
  python -c "
import sys
from PIL import Image, ImageStat
im = Image.open(sys.argv[1]).convert('RGB'); w, h = im.size
r, g, b = ImageStat.Stat(im.crop((int(w*0.23), int(h*0.27), int(w*0.29), int(h*0.34)))).mean
sys.exit(0 if g > b + 20 else 1)" "analysis/screenshots/m2_$1.png"
}
to_map() {  # $1 = session number; Home must be open
  shot "${tag}_$1_home"
  if home_on_play "${tag}_$1_home"; then note "$1: Home opened on Play"
  else note "$1: Home opened on another item, pressing up to Play"; cmdq 'up 100\nwait 700\n'; sleep 1.5; fi
  cmdq 'cross 300\n'; sleep 3.2; shot "${tag}_$1_play"
  cmdq 'cross 300\n'; sleep 6.7; shot "${tag}_$1_world"
  cmdq 'cross 300\n'; sleep 7; shot "${tag}_$1_map"
}
listing "baseline"
# Session 1
launch LBP3_CUTSCENE_AUTOSKIP=off "$@"; sleep 28
cmdq 'cross\nwait 2000\nsquare\nwait 2500\n'; sleep 6
to_map 1
listing "1: on the map, before the play"
t0=$(date +%s)
cmdq 'cross 300\n'
if wait_log '01_needlepoint_peaks/needlepoint_peaks.farc' 330; then
  note "1: Needlepoint Peaks opened $(( $(date +%s) - t0 )) s after the Legend of Bunkum started"
else note "1: Needlepoint Peaks never loaded"; fi
sleep 30; shot "${tag}_1_needle"
cmdq 'r1 100\n'                       # keep-alive (the 5-minute attract timer); R1 grabs nothing here
save_settle 30
note "1: save files quiet for 30 s, $(( $(date +%s) - t0 )) s after the start"
shot "${tag}_1_needle2"
listing "1: after the autosave"
cp analysis/remote_stderr.txt "analysis/m2_${tag}_1_stderr.txt"
taskkill //f //im lbp3.exe >/dev/null 2>&1; sleep 2
listing "1: after the kill"
# Session 2: same saves
launch "$@"; sleep 28
cmdq 'cross\nwait 2000\nsquare\nwait 2500\n'; sleep 6
to_map 2
cmdq 'down 300\nwait 1000\n'; sleep 2.5; shot "${tag}_2_map_down"
cp analysis/remote_stderr.txt "analysis/m2_${tag}_2_stderr.txt"
taskkill //f //im lbp3.exe >/dev/null 2>&1; sleep 2
listing "2: after the kill"
saves_return
note "voice lines played in 1: $(grep 'cs01_' "analysis/m2_${tag}_1_stderr.txt" | grep -c ' open ') of 16"
grep -h '^\[savedata\] mount\|playfart\|littlefart\|bigfart' "analysis/m2_${tag}_1_stderr.txt" | grep -c . | sed 's/^/save-related lines in 1: /'
