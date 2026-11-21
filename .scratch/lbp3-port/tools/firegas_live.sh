#!/bin/sh
# firegas_live.sh: a session on the "Fire & Horrible Gas" level (perf/firegas.md), one step per call.
# The caller holds the game lock (owner firegas): . game_lock.sh; game_lock_acquire firegas.
# FG_EXE=<exe> (default the dlss worktree's), FG_SAVE=<saves dir> (default analysis/savedata_firegas_20261121,
# the user's saves after building the level; not in git). "stop" kills the game it started by PID, but
# m2_lib's human_restore then kills any lbp3.exe by name: don't let a human start the game mid-session.
#   firegas_live.sh start <tag> [NAME=value ...]   user's settings, the firegas save baseline, muted, remote input
#   firegas_live.sh cmd '<input lines>'
#   firegas_live.sh shot <name>
#   firegas_live.sh stop <tag>                      kill, keep stderr/split as analysis/firegas_<tag>_*, human saves back
cd /d/Recomps/lbp3-recompiled || exit 1
EXE=${FG_EXE:-D:/Recomps/lbp3-wt/dlss/build/lbp3.exe}
. .scratch/lbp3-port/tools/m2_lib.sh
what=$1; shift
case "$what" in
start)
  tag=$1; shift
  [ "$(cut -d' ' -f1 analysis/game.lock.d/owner 2>/dev/null)" = firegas ] || { echo "lock not held"; exit 1; }
  tasklist 2>/dev/null | grep -qi lbp3.exe && { echo "a game is running: not ours"; exit 1; }
  human_keep || exit 1
  use_baseline "${FG_SAVE:-analysis/savedata_firegas_20261121}" || exit 1
  rm -f analysis/input_cmd.txt analysis/frame_split.csv
  brk=; [ -n "$FG_BREAK" ] && brk=",'--break=$FG_BREAK'"  # FG_BREAK=module+0xoffset[:extra]: a runtime breakpoint
  exe_m=$(cygpath -m "$EXE"); root_m=$(cygpath -m "$PWD"); cache_w=$(cygpath -w "$PWD/analysis/firegas_cache")
  disp=$(head -n 1 analysis/test_display.txt 2>/dev/null | tr -d '\r ')
  env LBP3_VOLUME=0 LBP3_REMOTE_INPUT=1 LBP3_OSK_PLAIN=1 SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT=0x0000/0x0000 \
    LBP3_DISPLAY="$disp" LBP3_RES=3840x2160 LBP3_RES_SHADERS=1 LBP3_UPSCALER=dlss LBP3_UPSCALER_MODE=quality \
    LBP3_FRAMEGEN=1 LBP3_FRAMEGEN_MODE=warp LBP3_ASYNC_PIPELINES=1 LBP3_WINDOW=borderless LBP3_STATUS_FLIPS=30 \
    LBP3_LBPPC_COOP=1 LBP3_FRAME_SPLIT=1 LBP3_SHADER_CACHE_DIR="$cache_w" "$@" powershell -NoProfile -Command \
    "(Start-Process -PassThru -NoNewWindow -FilePath '$exe_m' -ArgumentList '--run','--skip-intro'$brk -WorkingDirectory '$root_m' -RedirectStandardOutput '$root_m/analysis/remote_stdout.txt' -RedirectStandardError '$root_m/analysis/remote_stderr.txt').Id" > analysis/firegas_pid.txt
  echo "$tag $*" > analysis/firegas_current.txt
  sleep 30
  tasklist 2>/dev/null | grep -qi lbp3.exe && echo "started" || echo "GAME NOT RUNNING" ;;
cmd) cmdq "$1"; echo "sent" ;;
shot) shot "fg_$1"; echo "analysis/screenshots/m2_fg_$1.png" ;;
stop)
  tag=$1
  pid=$(tr -d '\r ' < analysis/firegas_pid.txt 2>/dev/null)
  [ -n "$pid" ] && taskkill //f //pid "$pid" >/dev/null 2>&1; sleep 2
  cp analysis/remote_stderr.txt "analysis/firegas_${tag}_stderr.txt"
  [ -f analysis/frame_split.csv ] && cp analysis/frame_split.csv "analysis/firegas_${tag}_split.csv"
  human_restore
  [ -f analysis/savedata_live_is_test ] && echo "WARNING: test marker still set" || echo "saves back" ;;
esac
