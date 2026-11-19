#!/bin/sh
# Ticket 10 repro by remote input: start the game with the given NAME=value settings, load the
# saved level, switch the Thruster on, then Change to Play Mode -> Save Changes.
#   t10_repro.sh <tag> [NAME=value ...]
# Screenshots: analysis/screenshots/<tag>_{level,on,dialog,after}.png
cd /d/Recomps/lbp3-recompiled || exit 1
tag=$1; shift
cmdq() { printf "$1" > analysis/input_cmd.txt; }
shot() { powershell -NoProfile -File tools/capture.ps1 -Out "analysis/screenshots/${tag}_$1.png" >/dev/null; }
frames() { grep -o '^\[video\] [0-9]* frames flipped' analysis/remote_stderr.txt | tail -1 | awk '{print $2}'; }
taskkill //f //im lbp3.exe >/dev/null 2>&1; sleep 1; rm -f analysis/input_cmd.txt
# RESTORE=<dir>: start from a saved copy of user/savedata/CUSA00473.
# Run alone, it keeps the human's saves aside first and puts them back on exit (saves.sh).
if [ -n "$RESTORE" ]; then . /d/Recomps/lbp3-recompiled/.scratch/lbp3-port/tools/saves.sh; saves_keep; saves_mark_test || exit 1; rm -rf user/savedata/CUSA00473 && cp -r "$RESTORE" user/savedata/CUSA00473 || exit 1; fi
powershell -NoProfile -Command "Set-Location D:\\Recomps\\lbp3-recompiled; & .\\tools\\remote_start.ps1 $*" || exit 1
sleep 28
cmdq 'cross\nwait 2000\nsquare\nwait 2500\n'; sleep 6
cmdq 'square 100\n'; sleep 25
shot level
if [ "${THRUSTER:-on}" = on ]; then
  cmdq 'square 100\nwait 1500\ncross 100\nwait 800\nlstick 0.8 0.5 700\nwait 500\nlstick 0.8 -0.6 700\nwait 500\nlstick 0.5 0.9 600\nwait 700\nlstick 0.1 0.9 350\nwait 700\nlstick 0.4 0.8 200\nwait 600\nlstick 0.3 0.9 250\nwait 800\nsquare 100\nwait 1500\ndown 100\nwait 500\ndown 100\nwait 800\nright 100\nwait 800\ncircle 100\nwait 1000\n'
  sleep 16
  shot on
fi
sleep 5
cmdq 'options 100\nwait 1500\ncross 100\nwait 1500\n'; sleep 3.5
shot dialog
a=$(frames)
cmdq 'cross 100\n'; sleep 15
b=$(frames); sleep 6; c=$(frames)
shot after
echo "frames before Save Changes: $a; +15 s: $b; +21 s: $c"
grep 'WaitRegMem stuck' analysis/remote_stderr.txt | sed 's/.*ReportStuckWait: //'
