#!/bin/sh
# Try to reproduce the white screen seen once (run e3): Save Changes taken while the Popit menu is
# open. Each attempt starts from the baseline: Edit Now, move the Thruster (as t17_edit.sh), circle
# back to the Popit menu and leave it open, then options -> Change to Play Mode -> Save Changes.
# Screenshots every 5 s for 40 s after the save (m2_<tag>_<n>_wNN.png); the verdict is WHITE if the
# last one is still mostly white. Logs "Rejecting invalid S#", stuck waits and device losses.
#   t_m2v_white.sh <tag> [attempts] [NAME=value ...]
cd /d/Recomps/lbp3-recompiled || exit 1
tag=$1; attempts=${2:-2}; shift 2
. /d/Recomps/lbp3-pc/.scratch/lbp3-port/tools/saves.sh; saves_keep
. /d/Recomps/lbp3-pc/.scratch/lbp3-port/tools/m2_lib.sh
k=1
while [ $k -le "$attempts" ]; do
  saves_baseline || exit 1
  launch "$@"; sleep 28; cmdq 'cross\nwait 2000\nsquare\nwait 2500\n'; sleep 6
  cmdq 'square 200\n'; sleep 25
  cmdq 'square 100\nwait 1500\ncross 100\nwait 800\nlstick 0.8 0.5 700\nwait 500\nlstick 0.8 -0.6 700\nwait 500\nlstick 0.5 0.9 600\nwait 700\nlstick 0.1 0.9 350\nwait 700\nlstick 0.4 0.8 200\nwait 600\nlstick 0.3 0.9 250\nwait 800\n'
  sleep 11
  cmdq 'cross 100\nwait 1200\nlstick -1 0 1500\nwait 800\ncross 100\nwait 1000\n'; sleep 6
  cmdq 'circle 250\nwait 1200\n'; sleep 2.5; shot "${tag}_${k}_popit"
  before=$(find user/savedata/CUSA00473/LBPxSAVE -type f -printf '%P %s %T@\n' | sort)
  cmdq 'options 250\nwait 1500\ncross 250\nwait 1500\n'; sleep 3.5; shot "${tag}_${k}_dialog"
  n0=$(wc -l < analysis/remote_stderr.txt)
  cmdq 'cross 300\n'
  i=0; while [ $i -le 8 ]; do sleep 5; shot "${tag}_${k}_w$(printf %02d $i)"; i=$((i + 1)); done
  after=$(find user/savedata/CUSA00473/LBPxSAVE -type f -printf '%P %s %T@\n' | sort)
  saved=$([ "$before" != "$after" ] && echo yes || echo no)
  white=$(python -c "
import sys
from PIL import Image, ImageStat
m = ImageStat.Stat(Image.open(sys.argv[1]).convert('L')).mean[0]
print(f'{m:.0f}')" "analysis/screenshots/m2_${tag}_${k}_w08.png")
  s=$(tail -n +"$((n0 + 1))" analysis/remote_stderr.txt | grep -c 'Rejecting invalid S#')
  st=$(tail -n +"$((n0 + 1))" analysis/remote_stderr.txt | grep -c 'stuck\|device lost')
  fr=$(tail -n +"$((n0 + 1))" analysis/remote_stderr.txt | grep -o '^\[video\] [0-9]* frames flipped (avg [0-9.]* ms' | tail -1)
  verdict=$([ "$white" -gt 200 ] && echo WHITE || echo normal)
  echo "$tag attempt $k: saved $saved; screen 40 s after the save: mean grey $white ($verdict); S# lines $s; stuck/device lost $st; last: $fr"
  cp analysis/remote_stderr.txt "analysis/m2_${tag}_${k}_stderr.txt"
  taskkill //f //im lbp3.exe >/dev/null 2>&1; sleep 2
  k=$((k + 1))
done
saves_return
