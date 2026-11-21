#!/bin/sh
# Ticket 10's Thruster Save Changes recipe on firegas_live.sh (the user's settings, FG_EXE), one run.
T=/d/Recomps/lbp3-recompiled/.scratch/lbp3-port/tools; tag=$1; shift
cd /d/Recomps/lbp3-recompiled || exit 1
. .scratch/lbp3-port/tools/m2_lib.sh >/dev/null 2>&1
frames() { grep -o '^\[video\] [0-9]* frames flipped' analysis/remote_stderr.txt | tail -1 | awk '{print $2}'; }
st=$(FG_SAVE=analysis/savedata_thruster_on sh $T/firegas_live.sh start "$tag" "$@" | tail -1); [ "$st" = started ] || { echo "$tag: START FAILED"; sh $T/firegas_live.sh stop "$tag" >/dev/null; exit 1; }
cmdq 'cross\nwait 2000\nsquare\nwait 2500\n'; sleep 6
cmdq 'square 100\n'; sleep 25
cmdq 'square 100\nwait 1500\ncross 100\nwait 800\nlstick 0.8 0.5 700\nwait 500\nlstick 0.8 -0.6 700\nwait 500\nlstick 0.5 0.9 600\nwait 700\nlstick 0.1 0.9 350\nwait 700\nlstick 0.4 0.8 200\nwait 600\nlstick 0.3 0.9 250\nwait 800\nsquare 100\nwait 1500\ndown 100\nwait 500\ndown 100\nwait 800\nright 100\nwait 800\ncircle 100\nwait 1000\n'
sleep 21
cmdq 'options 100\nwait 1500\ncross 100\nwait 1500\n'; sleep 3.5
a=$(frames); cmdq 'cross 100\n'; sleep 15; b=$(frames); sleep 6; c=$(frames)
dshot "fg_${tag}_after" >/dev/null 2>&1
stuck=$(grep -c 'WaitRegMem stuck' analysis/remote_stderr.txt)
sh $T/firegas_live.sh stop "$tag" | tail -1
echo "$tag: frames before Save Changes $a, +15 s $b, +21 s $c; stuck waits $stuck"
