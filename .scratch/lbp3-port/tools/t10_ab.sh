#!/bin/sh
# Run the ticket 10 recipe several times per variant and classify each outcome.
#   t10_ab.sh <name>:<runs>:<NAME=value,...> ...      (use "-" for no settings)
# Each run restores analysis/savedata_thruster_on and keeps the Thruster as loaded (firing).
S="$(cd "$(dirname "$0")" && pwd -W)"  # this tools folder (D:/ form, for PowerShell)
cd /d/Recomps/lbp3-recompiled || exit 1
. /d/Recomps/lbp3-pc/.scratch/lbp3-port/tools/saves.sh; saves_keep  # each run starts from the baseline; the human's saves come back on exit
for spec in "$@"; do
  name=${spec%%:*}; rest=${spec#*:}; runs=${rest%%:*}; vars=${rest#*:}
  [ "$vars" = "-" ] && vars=""
  vars=$(echo "$vars" | tr ',' ' ')
  i=1
  while [ $i -le $runs ]; do
    tag="ab_${name}_$i"
    RESTORE=analysis/savedata_thruster_on THRUSTER=keep sh "$S/t10_repro.sh" "$tag" $vars LBP3_GPU_CHECKPOINTS=1 >/dev/null 2>&1
    sleep 2
    cp analysis/remote_stderr.txt "analysis/${tag}_stderr.txt"
    if grep -q '^\[gpu\] Vulkan device lost' analysis/remote_stderr.txt; then out=RESET
    elif grep -q 'WaitRegMem stuck' analysis/remote_stderr.txt; then out=DEADLOCK
    else
      a=$(grep -o '^\[video\] [0-9]* frames flipped' analysis/remote_stderr.txt | tail -1 | awk '{print $2}'); sleep 6
      b=$(grep -o '^\[video\] [0-9]* frames flipped' analysis/remote_stderr.txt | tail -1 | awk '{print $2}')
      if [ "$a" != "$b" ]; then out=OK; else out=STALL_OTHER; fi
    fi
    mounts=$(grep -c 'savedata\] mount' analysis/remote_stderr.txt)
    if [ "$out" = DEADLOCK ]; then
      pid=$(tasklist //fi "imagename eq lbp3.exe" //fo csv //nh | cut -d, -f2 | tr -d '"')
      { powershell -NoProfile -ExecutionPolicy Bypass -File "$S/peek.ps1" $pid 1e1303c18 3; powershell -NoProfile -ExecutionPolicy Bypass -File "$S/peek.ps1" $pid 1741410 1; } > "analysis/${tag}_peek.txt" 2>&1
    fi
    echo "$name run $i: $out (savedata mounts $mounts)"
    taskkill //f //im lbp3.exe >/dev/null 2>&1; sleep 2
    i=$((i + 1))
  done
done
saves_return  # put the human's current saves back (saves.sh)
