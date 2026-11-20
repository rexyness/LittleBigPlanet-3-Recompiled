#!/bin/sh
# dlss_run.sh <tag> [NAME=value ...]: one run of the dlss worktree's build on res_run2.sh's route
# (the story test save: the Pod, the Prologue map, Needlepoint's start, walk, basket, popit), at
# 2560x1440 (LBP3_RES=1440 + shaders), muted, frame generation off, the capture hook on
# (LBP3_FG_DUMP=2: the flipped image and the HUD-less image at full size per spot).
# Holds the game lock and swaps in the test saves (saves.sh) for the whole run; the human's
# saves and the lock come back on any exit. Uses its own shader cache copy (analysis/dlss_cache).
cd /d/Recomps/lbp3-recompiled || exit 1
tag=$1; shift
EXE=D:/Recomps/lbp3-wt/dlss/build/lbp3.exe
. .scratch/lbp3-port/tools/game_lock.sh
game_lock_acquire dlss 60 || exit 1
. .scratch/lbp3-port/tools/saves.sh; saves_keep
trap 'saves_return; game_lock_release dlss' EXIT
SAVES_BASELINE=analysis/savedata_story_20261111 saves_baseline || exit 1
. .scratch/lbp3-port/tools/m2_lib.sh
if [ ! -d analysis/dlss_cache/CUSA00473 ]; then
  mkdir -p analysis/dlss_cache && cp -r user/shader_cache_best/CUSA00473* analysis/dlss_cache/ || exit 1
fi
cache_w=$(cygpath -w "$PWD/analysis/dlss_cache")
# NODUMP=1: no capture hook (frame generation wants the same GPU hooks); stills only as screenshots.
dumpenv="LBP3_FG_DUMP=2"
[ -n "$NODUMP" ] && dumpenv="LBP3_NO_FG_DUMP=1"
launch_exe() {
  taskkill //f //im lbp3.exe >/dev/null 2>&1; sleep 1; rm -f analysis/input_cmd.txt analysis/frame_split.csv
  exe_m=$(cygpath -m "$EXE"); root_m=$(cygpath -m "$PWD")
  disp=$(head -n 1 analysis/test_display.txt 2>/dev/null | tr -d '\r ')
  env LBP3_VOLUME=0 LBP3_REMOTE_INPUT=1 LBP3_DISPLAY="${LBP3_DISPLAY:-$disp}" $dumpenv LBP3_FRAME_SPLIT=1 \
    LBP3_RES=1440 LBP3_RES_SHADERS=1 LBP3_SHADER_CACHE_DIR="$cache_w" "$@" powershell -NoProfile -Command \
    "Start-Process -FilePath '$exe_m' -ArgumentList '--run','--skip-intro' -WorkingDirectory '$root_m' -RedirectStandardOutput '$root_m/analysis/remote_stdout.txt' -RedirectStandardError '$root_m/analysis/remote_stderr.txt'" >/dev/null
}
fgd() { cmdq "fgdump dlss_${tag}_$1\n"; sleep 4; }
launch_exe "$@"; sleep 30
echo "run $tag: env $*"
grep -m3 '^\[dlss\]' analysis/remote_stderr.txt
grep -m1 '\[video\] 2 display buffers' analysis/remote_stderr.txt
cmdq 'cross 200\nwait 1500\nsquare\nwait 2500\n'; sleep 6
shot "dlss_${tag}_pod"; fgd pod
cmdq 'up 100\nwait 700\n'; sleep 1.5
cmdq 'cross\nwait 3000\n'; sleep 4            # Play -> the Adventure page
cmdq 'cross\nwait 6500\n'; sleep 8            # Adventure -> the world map (Prologue)
cmdq 'cross\nwait 4500\n'; sleep 6            # the Prologue's level map
cmdq 'down 100\nwait 1200\nright 100\nwait 1200\nup 100\nwait 1200\n'; sleep 5
shot "dlss_${tag}_map"; fgd map
cmdq 'cross\nwait 1500\n'; sleep 30          # Needlepoint Peaks
shot "dlss_${tag}_start"; fgd start
w0=$(grep -c '' analysis/remote_stderr.txt)
cmdq 'lstick 1 0 3000\nwait 100\ncross 150\nwait 100\nlstick 1 0 3000\nwait 100\ncross 150\nwait 100\nlstick 1 0 3000\nwait 100\ncross 150\nwait 100\nlstick 1 0 3000\nwait 100\ncross 150\nwait 100\nlstick 1 0 3000\n'
sleep 7; fgd walk; sleep 6
h0=$(grep -c '' analysis/remote_stderr.txt)
shot "dlss_${tag}_heavy"; fgd basket
sleep 20
h1=$(grep -c '' analysis/remote_stderr.txt)
cmdq 'square 100\n'; sleep 2.5; fgd popit; shot "dlss_${tag}_popit"
taskkill //f //im lbp3.exe >/dev/null 2>&1; sleep 1
cp analysis/remote_stderr.txt "analysis/dlss_${tag}_stderr.txt"
[ -f analysis/frame_split.csv ] && cp analysis/frame_split.csv "analysis/dlss_${tag}_split.csv"
stat() { grep 'frames flipped' | sed -n 's/.*(avg \([0-9.]*\) ms, worst \([0-9.]*\) ms, \([0-9]*\) over 40 ms.*work \([0-9.]*\) ms.*/\1 \2 \3 \4/p'; }
mean() { awk -v what="$1" '{a += $1; w += $4; o += $3; if ($2 > m) m = $2; n++}
  END {if (n) printf "%s: avg %.2f ms, work %.2f ms, %d frames over 40 ms, worst %.1f ms, %d windows\n", what, a / n, w / n, o, m, n}'; }
sed -n "$((w0 + 1)),${h0}p" "analysis/dlss_${tag}_stderr.txt" | stat | mean "walk"
sed -n "$((h0 + 1)),${h1}p" "analysis/dlss_${tag}_stderr.txt" | stat | mean "standing at the basket (20 s)"
echo "GPU warnings: $(grep -c 'Rejecting invalid S#\|stuck\|device lost\|DEVICE_LOST' "analysis/dlss_${tag}_stderr.txt")"
echo "dlss lines:"; grep '^\[dlss\]' "analysis/dlss_${tag}_stderr.txt" | head -20
ls -d analysis/fg_dump/dlss_${tag}_* 2>/dev/null | tr '\n' ' '; echo
