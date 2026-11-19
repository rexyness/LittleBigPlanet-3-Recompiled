#!/bin/sh
# Milestone 5's heavy route: Needlepoint Peaks from the test copy of the human's story save
# (analysis/savedata_story_20261111; never the live saves), on any lbp3.exe.
#   boot -> Pod (close the network message) -> computer -> Home (up = Play) -> Adventure -> the
#   world map opens on the Prologue -> its map opens on "A Night On The Tiles": down
#   (Introduction), right (The Legend of Bunkum), up (Needlepoint Peaks) -> Play. 30 s in the
#   level at the start (light: game work ~22 ms), then five 3 s walks right with a jump between,
#   to the picnic basket with the layer switch, then HOLD seconds standing there.
# The measure is the walk (game work 28-32 ms on port 2026-11-11); standing at the basket is
# light (~24 ms), so it only shows the floor.
#   m5_np_route.sh <tag> [NAME=value ...]        environment for the game
#   M5_EXE=<lbp3.exe>  a build other than the clone's (run in the clone's folder; its own
#                      SDL3.dll beside it), as t_m2v_osk.sh's OSK_EXE
#   HOLD=<seconds>     standing time at the heavy spot (default 40)
# Writes analysis/m5_<tag>_stderr.txt and screenshots analysis/screenshots/m2_m5_<tag>_*.png;
# prints the [video] lines of the walk and of the standing part.
cd /d/Recomps/lbp3-recompiled || exit 1
tag=$1; shift
HOLD=${HOLD:-40}
EXE=${M5_EXE:-build/lbp3.exe}
. /d/Recomps/lbp3-pc/.scratch/lbp3-port/tools/saves.sh; saves_keep
SAVES_BASELINE=analysis/savedata_story_20261111 saves_baseline || exit 1
. /d/Recomps/lbp3-pc/.scratch/lbp3-port/tools/m2_lib.sh
launch_exe() {  # as tools/remote_start.ps1, with any lbp3.exe (paths in mixed form)
  taskkill //f //im lbp3.exe >/dev/null 2>&1; sleep 1; rm -f analysis/input_cmd.txt
  exe_m=$(cygpath -m "$(realpath "$EXE")"); root_m=$(cygpath -m "$PWD")
  disp=$(head -n 1 analysis/test_display.txt 2>/dev/null | tr -d '\r ')
  env LBP3_REMOTE_INPUT=1 LBP3_DISPLAY="${LBP3_DISPLAY:-$disp}" "$@" powershell -NoProfile -Command \
    "Start-Process -FilePath '$exe_m' -ArgumentList '--run','--skip-intro' -WorkingDirectory '$root_m' -RedirectStandardOutput '$root_m/analysis/remote_stdout.txt' -RedirectStandardError '$root_m/analysis/remote_stderr.txt'" >/dev/null
}
launch_exe "$@"; sleep 30
echo "build: $EXE"
grep -m1 -o 'LBP3_DISPLAY=[^:]*: window on "[^"]*" ([0-9.]* Hz), one of [0-9] displays' analysis/remote_stderr.txt
cmdq 'cross 200\nwait 1500\nsquare\nwait 2500\n'; sleep 6
cmdq 'up 100\nwait 700\n'; sleep 1.5
cmdq 'cross\nwait 3000\n'; sleep 4            # Play -> the Adventure page
cmdq 'cross\nwait 6500\n'; sleep 8            # Adventure -> the world map (Prologue)
cmdq 'cross\nwait 4500\n'; sleep 6            # the Prologue's level map
cmdq 'down 100\nwait 1200\nright 100\nwait 1200\nup 100\nwait 1200\n'; sleep 5
shot "m5_${tag}_map"
cmdq 'cross\nwait 1500\n'; sleep 30          # Needlepoint Peaks
shot "m5_${tag}_start"
w0=$(grep -c '' analysis/remote_stderr.txt)
cmdq 'lstick 1 0 3000\nwait 100\ncross 150\nwait 100\nlstick 1 0 3000\nwait 100\ncross 150\nwait 100\nlstick 1 0 3000\nwait 100\ncross 150\nwait 100\nlstick 1 0 3000\nwait 100\ncross 150\nwait 100\nlstick 1 0 3000\n'
sleep 17
h0=$(grep -c '' analysis/remote_stderr.txt)
shot "m5_${tag}_heavy"
sleep "$HOLD"
h1=$(grep -c '' analysis/remote_stderr.txt)
shot "m5_${tag}_heavy_end"
taskkill //f //im lbp3.exe >/dev/null 2>&1; sleep 1
cp analysis/remote_stderr.txt "analysis/m5_${tag}_stderr.txt"
# One line per [video] window: avg, worst, frames over 40 ms, work.
stat() { grep 'frames flipped' | sed -n 's/.*(avg \([0-9.]*\) ms, worst \([0-9.]*\) ms, \([0-9]*\) over 40 ms.*work \([0-9.]*\) ms.*/\1 \2 \3 \4/p'; }
mean() { awk -v what="$1" '{a += $1; w += $4; o += $3; if ($2 > m) m = $2; n++}
  END {if (n) printf "%s: avg %.2f ms, work %.2f ms, %d frames over 40 ms, worst %.1f ms, %d windows\n", what, a / n, w / n, o, m, n}'; }
sed -n "$((w0 + 1)),${h0}p" "analysis/m5_${tag}_stderr.txt" | stat | mean "walk (the measure)"
sed -n "$((h0 + 1)),${h1}p" "analysis/m5_${tag}_stderr.txt" | stat | mean "standing at the basket (${HOLD} s)"
echo "GPU warnings: $(grep -c 'Rejecting invalid S#\|stuck\|device lost' "analysis/m5_${tag}_stderr.txt")"
