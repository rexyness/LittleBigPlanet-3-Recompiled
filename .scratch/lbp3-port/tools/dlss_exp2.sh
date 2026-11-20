#!/bin/sh
# dlss_exp2.sh <tag>: stage 2's second comparison (dlss_exp.sh's harness). Standing at the Pod:
# pod_base (jitter off, SMAA left out), pod_pp, pod_mm, pod_pm, pod_mp. Then Needlepoint's basket,
# walking LEFT (there is room; the basket blocks the right) with jitter off and SMAA kept:
# walk_normal, walk_zero, walk_flipx, walk_flipy, walk_flipxy; then the full stage-2 settings
# walking (walk_full: vectors normal, jitter ++, SMAA left out) and standing (basket_full).
# Then: python .scratch/lbp3-port/tools/dlss_temporal.py analysis/fg_dump analysis/dlss_stills <tag>
cd /d/Recomps/lbp3-recompiled || exit 1
tag=$1; shift
EXE=D:/Recomps/lbp3-wt/dlss/build/lbp3.exe
. .scratch/lbp3-port/tools/game_lock.sh
game_lock_acquire dlss 60 || exit 1
. .scratch/lbp3-port/tools/saves.sh; saves_keep
trap 'saves_return; game_lock_release dlss' EXIT
SAVES_BASELINE=analysis/savedata_story_20261111 saves_baseline || exit 1
. .scratch/lbp3-port/tools/m2_lib.sh
cache_w=$(cygpath -w "$PWD/analysis/dlss_cache")
taskkill //f //im lbp3.exe >/dev/null 2>&1; sleep 1; rm -f analysis/input_cmd.txt
exe_m=$(cygpath -m "$EXE"); root_m=$(cygpath -m "$PWD")
disp=$(head -n 1 analysis/test_display.txt 2>/dev/null | tr -d '\r ')
env LBP3_VOLUME=0 LBP3_REMOTE_INPUT=1 LBP3_DISPLAY="$disp" LBP3_FG_DUMP=12 LBP3_RES=1440 LBP3_RES_SHADERS=1 \
  LBP3_SHADER_CACHE_DIR="$cache_w" LBP3_UPSCALER=dlss LBP3_DLSS_TRACE=1 LBP3_DLSS_JITTER=off LBP3_DLSS_SMAA=skip "$@" \
  powershell -NoProfile -Command \
  "Start-Process -FilePath '$exe_m' -ArgumentList '--run','--skip-intro' -WorkingDirectory '$root_m' -RedirectStandardOutput '$root_m/analysis/remote_stdout.txt' -RedirectStandardError '$root_m/analysis/remote_stderr.txt'" >/dev/null
sleep 30
snap() { cmdq "fgdump dlssx_${tag}_$1\n"; sleep 9; }
set_() { cmdq "dlss $1 $2\n"; sleep 3; }
cmdq 'cross 200\nwait 1500\nsquare\nwait 2500\n'; sleep 8   # the Pod
snap pod_base
set_ jitter ++; snap pod_pp
set_ jitter --; snap pod_mm
set_ jitter +-; snap pod_pm
set_ jitter -+; snap pod_mp
set_ jitter off; set_ smaa keep
cmdq 'up 100\nwait 700\n'; sleep 1.5
cmdq 'cross\nwait 3000\n'; sleep 4            # Play -> the Adventure page
cmdq 'cross\nwait 6500\n'; sleep 8            # Adventure -> the world map (Prologue)
cmdq 'cross\nwait 4500\n'; sleep 6            # the Prologue's level map
cmdq 'down 100\nwait 1200\nright 100\nwait 1200\nup 100\nwait 1200\n'; sleep 5
cmdq 'cross\nwait 1500\n'; sleep 30          # Needlepoint Peaks
cmdq 'lstick 1 0 3000\nwait 100\ncross 150\nwait 100\nlstick 1 0 3000\nwait 100\ncross 150\nwait 100\nlstick 1 0 3000\nwait 100\ncross 150\nwait 100\nlstick 1 0 3000\nwait 100\ncross 150\nwait 100\nlstick 1 0 3000\n'
sleep 18                                      # at the basket
walk() {  # walk left 2.6 s, capturing from 0.9 s, then back right
  cmdq 'lstick -1 0 2600\n'; sleep 0.9; cmdq "fgdump dlssx_${tag}_$1\n"; sleep 4
  cmdq 'lstick 1 0 2800\n'; sleep 4
}
for m in normal zero flipx flipy flipxy; do set_ mv "$m"; walk "walk_$m"; done
set_ mv normal; set_ smaa skip; set_ jitter ++
walk walk_full
sleep 3; snap basket_full
shot "dlssx_${tag}_end"
taskkill //f //im lbp3.exe >/dev/null 2>&1; sleep 1
cp analysis/remote_stderr.txt "analysis/dlssx_${tag}_stderr.txt"
grep '^\[dlss\]' "analysis/dlssx_${tag}_stderr.txt" | grep -v 'ngx:\|not upscaled' | tail -30
echo "not upscaled after boot: $(grep -c 'not upscaled' "analysis/dlssx_${tag}_stderr.txt")"
echo "GPU warnings: $(grep -c 'Rejecting invalid S#\|stuck\|device lost\|DEVICE_LOST' "analysis/dlssx_${tag}_stderr.txt")"
ls -d analysis/fg_dump/dlssx_${tag}_* 2>/dev/null | tr '\n' ' '; echo
