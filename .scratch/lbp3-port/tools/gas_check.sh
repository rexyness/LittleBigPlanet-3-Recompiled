#!/bin/sh
# gas_check.sh [tag] [NAME=value ...]: issue #27's check, RED while the fluid effect (Horrible Gas,
# the fire material's flames) draws nothing. Boots the firegas save through firegas_live.sh (FG_EXE
# picks the exe) at 2560x1440 with no upscaler, frame generation, async pipelines (they skip draws
# while a pipeline builds) or LBPPC mode, opens the level (Edit Now), F9-traces a frame and judges
# the ray-march draw's target with gas_verdict.py. About 1.5 minutes. Takes the game lock as firegas.
#   gas_check.sh probe    the same F9 check on a firegas_live.sh session already in the level (seconds)
#   GAS_KEEP=1 gas_check.sh [tag] ...   leave that session running (and the lock held) after the check;
#   gas_check.sh dump <n,n,...>         then F9-dump those draws and print their trace and dump lines;
#   gas_check.sh stop [tag]             and end it
# Exit 0 GREEN, 1 RED, 2 BROKEN. Overwrites analysis/dump_draws.txt; deletes the F9 dump folders it makes.
T=/d/Recomps/lbp3-recompiled/.scratch/lbp3-port/tools
cd /d/Recomps/lbp3-recompiled || exit 2
. $T/m2_lib.sh >/dev/null 2>&1
LOG=analysis/remote_stderr.txt
f9() {  # dump the draws in $1 from the next frame; waits for the whole dump
  printf '%s\n' "$1" > analysis/dump_draws.txt
  n0=$(grep -c '^\[targets\] [0-9]* render targets at flip' $LOG)
  cmdq 'f9\n'; t=0
  while [ "$(grep -c '^\[targets\] [0-9]* render targets at flip' $LOG)" -le "$n0" ]; do
    [ $t -ge 60 ] && { echo "BROKEN: no F9 dump within 60 s"; return 2; }; sleep 1; t=$((t + 1)); done
  # The header comes before the images: wait for all of them, then delete the folder.
  h=$(grep '^\[targets\] [0-9]* render targets at flip' $LOG | tail -1)
  n=$(echo "$h" | awk '{print $2}'); d=$(echo "$h" | grep -o 'dump_[0-9_]*')
  while [ "$(grep -c "^\[targets\] analysis.targets.${d}.t" $LOG)" -lt "$n" ]; do
    [ $t -ge 120 ] && { echo "BROKEN: the F9 dump never finished"; return 2; }; sleep 1; t=$((t + 1)); done
  [ -n "$d" ] && rm -rf "analysis/targets/$d"
  return 0
}
window() { seq -s, $(($1 - 8)) $(($2 + 8)); }  # the draws around those numbers (they drift by a few)
probe() {
  f9 "$(window 494 500)" || return 2
  out=$(python $T/gas_verdict.py $LOG); v=$?
  if [ $v = 3 ]; then  # the draws moved further: dump around the ones the trace found
    set -- $(echo "${out#MOVED }" | tr , ' ')
    f9 "$(window $1 $2)" || return 2
    out=$(python $T/gas_verdict.py $LOG); v=$?
  fi
  echo "$out"; return $v
}
[ "$1" = probe ] && { probe; exit $?; }
if [ "$1" = dump ]; then  # the frame's lines for those draws: trace, dumps, [DEBUG-...] probes
  f9 "$2" || exit 2
  s=$(grep -n '^\[draw\] ---- frame' $LOG | tail -1 | cut -d: -f1)
  pat=$(echo "$2" | sed 's/,/|/g')
  tail -n +"$s" $LOG | awk -v re="^\\\\[(draw|dispatch)\\\\] #($pat) |draw0*($pat)_|\\\\[DEBUG-[a-z]*\\\\] draw #($pat) " \
    '$0 ~ re { print; keep = /draw0/; next } keep && /^\[targets\]     alpha/ { print } { keep = 0 }'
  exit 0
fi
. $T/game_lock.sh
if [ "$1" = stop ]; then sh $T/firegas_live.sh stop "gas_${2:-gas}" | tail -1; game_lock_release firegas; exit 0; fi
tag=${1:-gas}; [ $# -gt 0 ] && shift
game_lock_acquire firegas 600 || exit 2
[ "${GAS_KEEP:-0}" = 1 ] ||
  trap 'sh $T/firegas_live.sh stop "gas_$tag" >/dev/null 2>&1; game_lock_release firegas >/dev/null' EXIT
st=$(sh $T/firegas_live.sh start "gas_$tag" LBP3_RES=2560x1440 LBP3_UPSCALER=off LBP3_FRAMEGEN=0 \
  LBP3_ASYNC_PIPELINES=0 LBP3_LBPPC_COOP=0 "$@" | tail -1)
[ "$st" = started ] || { echo "BROKEN: the game did not start ($st)"; exit 2; }
cmdq 'cross\nwait 2000\nsquare\nwait 2500\n'; sleep 6
cmdq 'square 100\n'
n=0  # the level is in when [faults] passes 15000 a status window
until [ "$(grep '^\[faults\]' $LOG | tail -1 | awk '{print $2}')" -gt 15000 ] 2>/dev/null; do
  [ $n -ge 60 ] && { echo "BROKEN: the level never loaded"; exit 2; }; sleep 2; n=$((n + 2)); done
sleep 6
probe
