#!/bin/sh
# dlss_lane.sh <tag> [NAME=value ...]: the DLSS build through the game's other states, res_lane.sh's
# route (_tools/res_lane.sh): the Pod, the Pod computer's Home, Edit Now (Create), the pause menu's
# fade (during and after), a photo (Popit -> Stickers & Decorations -> Take A Photo). 2560x1440,
# the upscaler on with stage 2's defaults (jitter ++, SMAA left out), LBP3_DLSS_TRACE=1 (every flip
# DLSS doesn't take is logged with what was missing), the capture hook taking 2 frames per spot
# (LBP3_FG_DUMP=2, analysis/fg_dump/dlssl_<tag>_<spot>/), screenshots, muted, frame generation off,
# the story test save, the game lock held.
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
env LBP3_VOLUME=0 LBP3_REMOTE_INPUT=1 LBP3_DISPLAY="$disp" LBP3_FG_DUMP=2 LBP3_RES=1440 LBP3_RES_SHADERS=1 \
  LBP3_SHADER_CACHE_DIR="$cache_w" LBP3_UPSCALER=dlss LBP3_DLSS_TRACE=1 "$@" powershell -NoProfile -Command \
  "Start-Process -FilePath '$exe_m' -ArgumentList '--run','--skip-intro' -WorkingDirectory '$root_m' -RedirectStandardOutput '$root_m/analysis/remote_stdout.txt' -RedirectStandardError '$root_m/analysis/remote_stderr.txt'" >/dev/null
alive() { tasklist 2>/dev/null | grep -qi "lbp3.exe"; }
mark() { echo "[lane] $1 from line $(grep -c '' analysis/remote_stderr.txt)"; }
s() { shot "dlssl_${tag}_$1"; cmdq "fgdump dlssl_${tag}_$1\n"; sleep 4; }
sleep 32
alive || echo "$tag: GAME GONE before the Pod"
mark pod; s pod
cmdq 'cross\nwait 2000\nsquare\nwait 2500\n'; sleep 6
mark home; s home
cmdq 'square 200\n'; sleep 25                       # Edit Now: the Create level
mark level; s level
mark pause; cmdq 'options 150\n'; sleep 0.35; shot "dlssl_${tag}_pause_fade"; sleep 2.5; s pause
cmdq 'circle 150\nwait 1500\n'; sleep 3              # back out of the pause menu
mark photo
cmdq 'square 250\nwait 1500\nright 250\nwait 700\ncross 250\nwait 1500\nright 250\nwait 800\n'; sleep 6; s tool
cmdq 'cross 250\nwait 1500\n'; sleep 3; s frame
cmdq 'cross 250\nwait 1500\n'; sleep 2
sleep 5; s pictures
mark end
alive || echo "$tag: GAME GONE during the route"
taskkill //f //im lbp3.exe >/dev/null 2>&1; sleep 2
cp analysis/remote_stderr.txt "analysis/dlssl_${tag}_stderr.txt"
f=analysis/dlssl_${tag}_stderr.txt
grep '^\[dlss\]' "$f" | grep -v 'ngx:\|not upscaled' | tail -12
echo "flips not upscaled: $(grep -c 'not upscaled' "$f")"
grep 'not upscaled' "$f" | sed 's/flip [0-9]* //' | sort | uniq -c | sort -rn | head -8
grep -m3 -i 'jpeg\|photo\|encoder' "$f"
echo "GPU warnings: $(grep -c 'Rejecting invalid S#\|stuck\|device lost\|DEVICE_LOST' "$f")"
