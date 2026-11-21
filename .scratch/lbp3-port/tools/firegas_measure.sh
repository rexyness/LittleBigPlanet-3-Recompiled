#!/bin/sh
# firegas_measure.sh <tag> [NAME=value ...]: one measured run of the "Fire & Horrible Gas" level.
# Boot with the user's settings + the firegas save, Edit Now, wait for the level, record 20 status
# windows (600 frames) in Create mode's start view, capture it, stop, print medians.
T=/d/Recomps/lbp3-recompiled/.scratch/lbp3-port/tools
tag=$1; shift
cd /d/Recomps/lbp3-recompiled || exit 1
. .scratch/lbp3-port/tools/m2_lib.sh >/dev/null 2>&1
st=$(sh $T/firegas_live.sh start "$tag" "$@" | tail -1); echo "$st"; [ "$st" = started ] || { sh $T/firegas_live.sh stop "$tag" >/dev/null 2>&1; echo "RESULT $tag: START FAILED"; exit 1; }
sh $T/firegas_live.sh cmd 'cross\nwait 2000\nsquare\nwait 2500\n' >/dev/null; sleep 6
sh $T/firegas_live.sh cmd 'square 100\n' >/dev/null
# the level is in when [faults] goes above 15000 a window (or 60 s pass)
n=0; while [ $n -lt 60 ]; do
  last=$(grep '^\[faults\]' analysis/remote_stderr.txt | tail -1 | awk '{print $2}')
  [ "${last:-0}" -gt 15000 ] && break; sleep 2; n=$((n+2)); done
sleep 6
if [ "${PLAY:-0}" = 1 ]; then  # Change to Play Mode -> Discard Changes, then wait for the level again
  sh $T/firegas_live.sh cmd 'options 250
wait 1500
cross 250
wait 1500
' >/dev/null; sleep 4
  sh $T/firegas_live.sh cmd 'right 300
wait 800
cross 300
' >/dev/null; sleep 30
fi
start=$(grep -c '^\[video\] [0-9]* frames' analysis/remote_stderr.txt)
f0=$(grep -o '^\[video\] [0-9]* frames' analysis/remote_stderr.txt | tail -1 | awk '{print $2}')
while [ $(( $(grep -c '^\[video\] [0-9]* frames' analysis/remote_stderr.txt) - start )) -lt 20 ]; do sleep 2; done
f1=$(grep -o '^\[video\] [0-9]* frames' analysis/remote_stderr.txt | tail -1 | awk '{print $2}')
dshot "fg_${tag}" >/dev/null 2>&1
sh $T/firegas_live.sh stop "$tag" | tail -1
python $T/firegas_summ.py "analysis/firegas_${tag}_stderr.txt" "analysis/firegas_${tag}_split.csv" "$f0" "$f1" "$tag"
