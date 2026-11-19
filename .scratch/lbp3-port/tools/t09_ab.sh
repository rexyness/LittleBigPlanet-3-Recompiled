#!/bin/sh
# Ticket 09 A/B in the real Introduction: precise epoll timeouts (the diag build's default)
# against the old ones (LBP3_EPOLL_PRECISE=off), alternating, then steps != 1 per run.
#   t09_ab.sh <prefix> <runs per arm> [NAME=value ...]
# Logs: analysis/pace_<prefix>_{precise,imprecise}<n>_stderr.txt. Saves: pace_run_intro.ps1
# starts from the baseline and puts the human's current saves back after each run.
S="$(cd "$(dirname "$0")" && pwd -W)"
cd /d/Recomps/lbp3-recompiled || exit 1
prefix=$1; runs=$2; shift 2
i=1
while [ $i -le "$runs" ]; do
  for arm in precise imprecise; do
    extra=""; [ $arm = imprecise ] && extra="LBP3_EPOLL_PRECISE=off"
    powershell -NoProfile -ExecutionPolicy Bypass -File "$S/pace_run_intro.ps1" -Tag "${prefix}_${arm}$i" $extra "$@" >/dev/null 2>&1
    python "$S/sim_steps_level.py" "analysis/pace_${prefix}_${arm}${i}_stderr.txt"
  done
  i=$((i + 1))
done
