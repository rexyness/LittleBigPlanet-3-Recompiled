#!/bin/sh
# dlss_live.sh: drive the dlss worktree's build by hand, one step per call (an agent looking at
# something in game). The game lock is held and the test saves are in from "start" until "stop";
# "stop" puts the human's saves back and frees the lock (saves.sh's kept copy also survives an
# interrupted session: the next saves_keep reuses it).
#   dlss_live.sh start [NAME=value ...]   story test save, 2560x1440, DLSS Quality, muted, remote input
#   dlss_live.sh cmd '<input lines with \n>'
#   dlss_live.sh shot <name>              analysis/screenshots/dlssv_<name>.png
#   dlss_live.sh stop
cd /d/Recomps/lbp3-recompiled || exit 1
EXE=${DLSS_EXE:-D:/Recomps/lbp3-wt/dlss/build/lbp3.exe}
. .scratch/lbp3-port/tools/game_lock.sh
. .scratch/lbp3-port/tools/saves.sh
. .scratch/lbp3-port/tools/m2_lib.sh
what=$1; shift
case "$what" in
start)
  game_lock_acquire dlss-live 60 || exit 1
  saves_keep; trap - EXIT  # the saves stay swapped until "stop"
  SAVES_BASELINE=analysis/savedata_story_20261111 saves_baseline || { saves_return; game_lock_release dlss-live; exit 1; }
  cache_w=$(cygpath -w "$PWD/analysis/dlss_cache")
  rm -f analysis/input_cmd.txt
  exe_m=$(cygpath -m "$EXE"); root_m=$(cygpath -m "$PWD")
  disp=$(head -n 1 analysis/test_display.txt 2>/dev/null | tr -d '\r ')
  env LBP3_VOLUME=0 LBP3_REMOTE_INPUT=1 LBP3_DISPLAY="$disp" LBP3_RES=1440 LBP3_RES_SHADERS=1 \
    LBP3_SHADER_CACHE_DIR="$cache_w" LBP3_UPSCALER=dlss LBP3_UPSCALER_MODE=quality "$@" powershell -NoProfile -Command \
    "Start-Process -FilePath '$exe_m' -ArgumentList '--run','--skip-intro' -WorkingDirectory '$root_m' -RedirectStandardOutput '$root_m/analysis/remote_stdout.txt' -RedirectStandardError '$root_m/analysis/remote_stderr.txt'" >/dev/null
  sleep 32  # (call "start" with its output to a file: the game keeps a pipe open)
  tasklist 2>/dev/null | grep -qi lbp3.exe && echo "started" || echo "GAME NOT RUNNING"
  grep -m2 '^\[dlss\] \(Quality on\|DLSS created\|no upscaler\)' analysis/remote_stderr.txt ;;
cmd) cmdq "$1"; echo "sent" ;;
shot) shot "dlssv_$1"; echo "analysis/screenshots/m2_dlssv_$1.png" ;;
stop)
  taskkill //f //im lbp3.exe >/dev/null 2>&1; sleep 1
  cp analysis/remote_stderr.txt analysis/dlssv_stderr.txt
  saves_return
  game_lock_release dlss-live
  [ -f analysis/savedata_live_is_test ] && echo "WARNING: test marker still set" || echo "saves back" ;;
*) echo "usage: dlss_live.sh start|cmd|shot|stop"; exit 1 ;;
esac
