#!/bin/sh
# dlss_smoke.sh: boot the dlss build with the upscaler on (muted, 2560x1440, test saves, game lock),
# wait 40 s at the Pod, print the [dlss] and [gpu] lines, stop.
cd /d/Recomps/lbp3-recompiled || exit 1
EXE=${DLSS_EXE:-D:/Recomps/lbp3-wt/dlss/build/lbp3.exe}
. .scratch/lbp3-port/tools/game_lock.sh
game_lock_acquire dlss 60 || exit 1
. .scratch/lbp3-port/tools/saves.sh; saves_keep
trap 'saves_return; game_lock_release dlss' EXIT
SAVES_BASELINE=analysis/savedata_story_20261111 saves_baseline || exit 1
if [ ! -d analysis/dlss_cache/CUSA00473 ]; then
  mkdir -p analysis/dlss_cache && cp -r user/shader_cache_best/CUSA00473* analysis/dlss_cache/ || exit 1
fi
cache_w=$(cygpath -w "$PWD/analysis/dlss_cache")
taskkill //f //im lbp3.exe >/dev/null 2>&1; sleep 1
exe_m=$(cygpath -m "$EXE"); root_m=$(cygpath -m "$PWD")
disp=$(head -n 1 analysis/test_display.txt 2>/dev/null | tr -d '\r ')
env LBP3_VOLUME=0 LBP3_DISPLAY="$disp" LBP3_RES=1440 LBP3_RES_SHADERS=1 LBP3_SHADER_CACHE_DIR="$cache_w" \
  LBP3_UPSCALER=dlss "$@" powershell -NoProfile -Command \
  "Start-Process -FilePath '$exe_m' -ArgumentList '--run','--skip-intro' -WorkingDirectory '$root_m' -RedirectStandardOutput '$root_m/analysis/remote_stdout.txt' -RedirectStandardError '$root_m/analysis/remote_stderr.txt'" >/dev/null
sleep 40
# SHOT=<name>: a screenshot of the game window first (analysis/screenshots/<name>.png).
[ -n "$SHOT" ] && powershell -NoProfile -File tools/capture.ps1 -Out "analysis/screenshots/$SHOT.png" >/dev/null
alive=$(tasklist 2>/dev/null | grep -ci "lbp3.exe")
taskkill //f //im lbp3.exe >/dev/null 2>&1; sleep 1
cp analysis/remote_stderr.txt analysis/dlss_smoke_stderr.txt
echo "game still running after 40 s: $alive"
grep -E '^\[dlss\]|^\[gpu\] Vulkan device|^\[video\] 2 display|^\[res\] 1440p' analysis/dlss_smoke_stderr.txt | head -30
echo "--- last lines"; tail -5 analysis/dlss_smoke_stderr.txt
