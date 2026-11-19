#!/bin/sh
# HITL run for tickets 07/10/09 on the diag build, with GPU checkpoints and a rolling screenshot.
#   hitl_start.sh <tag> isolate|control|all [NAME=value ...]   then arm watch_freeze.sh <tag> as a Monitor
# Presets (the diag build now defaults to precise epoll and the PS4 FPU state, and to vblank pacing):
#   isolate  ticket 07: the human's original setup (present pacing) with only the submit-wait fix
#            switched on, so a freeze or a TDR says something about ticket 10's fix alone.
#   control  ticket 07, only if an isolate run stays clean: isolate with the old submit wait
#            (LBP3_SUBMIT_WAIT=all), to show the reset still reproduces that day.
#   all      ticket 10/09: every fix on, default (vblank) pacing, as milestone 1 would ship it.
S="$(cd "$(dirname "$0")" && pwd -W)"  # this tools folder (D:/ form, for PowerShell)
cd /d/Recomps/lbp3-recompiled || exit 1
tag=$1; preset=$2
case "$preset" in
  isolate) vars="LBP3_PACING=present LBP3_EPOLL_PRECISE=off LBP3_GUEST_FPU=off" ;;
  control) vars="LBP3_PACING=present LBP3_EPOLL_PRECISE=off LBP3_GUEST_FPU=off LBP3_SUBMIT_WAIT=all" ;;
  all) vars="LBP3_CLEAR_SWIZZLE=off" ;;
  *) echo "usage: hitl_start.sh <tag> isolate|control|all [NAME=value ...]"; exit 2 ;;
esac
shift 2
git -C /d/Recomps/lbp3-recompiled rev-parse --abbrev-ref HEAD | grep -q '^diag/gpu-checkpoints$' || { echo "not on diag/gpu-checkpoints"; exit 1; }
# Leftover watchers from earlier sessions would follow the new game and overwrite their own
# logs (stopping a Monitor doesn't end the watcher's shell), so end them first.
for w in $(ps -ef | grep 'watch_freeze\.sh' | grep -v grep | awk '{print $2}'); do kill "$w" 2>/dev/null; done
taskkill //f //im lbp3.exe >/dev/null 2>&1; sleep 1; rm -f analysis/input_cmd.txt
mkdir -p analysis/savedata_backups && cp -r user/savedata/CUSA00473 "analysis/savedata_backups/before_${tag}_$(date +%H%M)"
echo "preset $preset: $vars $*"
powershell -NoProfile -Command "Set-Location D:\\Recomps\\lbp3-recompiled; & .\\tools\\remote_start.ps1 LBP3_GPU_CHECKPOINTS=1 $vars $*" || exit 1
# Rolling screenshots every 4 s until the game exits (runs detached).
powershell -NoProfile -ExecutionPolicy Bypass -File "$S/rolling_capture.ps1" -Tag "$tag" >/dev/null 2>&1 &
# The branch check above can't see a stale binary; the checkpoints line only comes from the diag build.
n=0
until grep -q '^\[gpu\] checkpoints on' analysis/remote_stderr.txt 2>/dev/null || [ $n -ge 30 ]; do sleep 1; n=$((n + 1)); done
grep -q '^\[gpu\] checkpoints on' analysis/remote_stderr.txt || echo "WARNING: no '[gpu] checkpoints on' line after 30 s: this lbp3.exe isn't the diag build (rebuild on diag/gpu-checkpoints)"
grep -h '\[gpu\] checkpoints\|\[video\] \(display refresh\|vblank source\|pacing\)' analysis/remote_stderr.txt 2>/dev/null
# Ticket 07's resets happened with present pacing running fast (13-24 ms frames) on the 240 Hz
# display. On a slower display present pacing falls back to the 33.3 ms timer, so a clean session
# there says nothing about ticket 07.
if [ "$preset" = isolate ] || [ "$preset" = control ]; then
  hz=$(grep -o '^\[video\] display refresh [0-9]*' analysis/remote_stderr.txt | head -1 | awk '{print $4}')
  if [ -z "$hz" ] || [ "$hz" -lt 200 ]; then
    echo "WARNING: display refresh ${hz:-unknown} Hz: present pacing falls back to the timer here. Ticket 07 needs the 240 Hz display."
  fi
  echo "Count a clean session only if its status lines show the fast frames of the session that reset (avg 13-24 ms)."
fi
echo "started; screenshots: analysis/screenshots/${tag}_roll*.png"
