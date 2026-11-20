#!/bin/sh
# dlss_exp3.sh <tag>: mip bias off against NVIDIA's, standing at the basket in Quality (dlss_exp.sh's harness):
# base = mip bias 0, pp = NVIDIA's log2(render / output) - 1; 12 consecutive frames each.
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
taskkill //f //im lbp3.exe >/dev/null 2>&1; sleep 1; rm -f analysis/input_cmd.txt
exe_m=$(cygpath -m "$EXE"); root_m=$(cygpath -m "$PWD")
disp=$(head -n 1 analysis/test_display.txt 2>/dev/null | tr -d '\r ')
env LBP3_VOLUME=0 LBP3_REMOTE_INPUT=1 LBP3_DISPLAY="$disp" LBP3_FG_DUMP=12 LBP3_RES=1440 LBP3_RES_SHADERS=1 \
  LBP3_SHADER_CACHE_DIR="$cache_w" LBP3_UPSCALER=dlss LBP3_DLSS_TRACE=1 LBP3_UPSCALER_MODE=quality "$@" \
  powershell -NoProfile -Command \
  "Start-Process -FilePath '$exe_m' -ArgumentList '--run','--skip-intro' -WorkingDirectory '$root_m' -RedirectStandardOutput '$root_m/analysis/remote_stdout.txt' -RedirectStandardError '$root_m/analysis/remote_stderr.txt'" >/dev/null
sleep 30
cmdq 'cross 200\nwait 1500\nsquare\nwait 2500\n'; sleep 6
cmdq 'up 100\nwait 700\n'; sleep 1.5
cmdq 'cross\nwait 3000\n'; sleep 4            # Play -> the Adventure page
cmdq 'cross\nwait 6500\n'; sleep 8            # Adventure -> the world map (Prologue)
cmdq 'cross\nwait 4500\n'; sleep 6            # the Prologue's level map
cmdq 'down 100\nwait 1200\nright 100\nwait 1200\nup 100\nwait 1200\n'; sleep 5
cmdq 'cross\nwait 1500\n'; sleep 30          # Needlepoint Peaks
cmdq 'lstick 1 0 3000\nwait 100\ncross 150\nwait 100\nlstick 1 0 3000\nwait 100\ncross 150\nwait 100\nlstick 1 0 3000\nwait 100\ncross 150\nwait 100\nlstick 1 0 3000\nwait 100\ncross 150\nwait 100\nlstick 1 0 3000\n'
sleep 18                                      # at the basket, standing
snap() { cmdq "fgdump dlssx_${tag}_$1\n"; sleep 9; }
set_() { cmdq "dlss $1 $2\n"; sleep 3; }
set_ mipbias 0; snap base
set_ mipbias nvidia; snap pp
shot "dlssx_${tag}_end"
taskkill //f //im lbp3.exe >/dev/null 2>&1; sleep 1
cp analysis/remote_stderr.txt "analysis/dlssx_${tag}_stderr.txt"
grep '^\[dlss\]' "analysis/dlssx_${tag}_stderr.txt" | grep -v 'ngx:' | tail -40
echo "GPU warnings: $(grep -c 'Rejecting invalid S#\|stuck\|device lost\|DEVICE_LOST' "analysis/dlssx_${tag}_stderr.txt")"
ls -d analysis/fg_dump/dlssx_${tag}_* 2>/dev/null | tr '\n' ' '; echo
