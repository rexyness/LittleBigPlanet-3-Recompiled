#!/bin/sh
# Soak: load the saved level with the Thruster firing and leave it in Create mode.
#   [KEEPALIVE=s] [KEEPALIVE_CMD=...] [SHOT_EVERY=s] soak.sh <tag> <minutes> [NAME=value ...]
#   prints RESET / DEADLOCK / STALLED / OK at the end
# After 5 minutes without a button press the game leaves the scene for its attract video
# (lbp3_attract.bik); every soak before 2026-11-10 16:00 ran the Thruster scene for only
# those 5 minutes. So every KEEPALIVE seconds (default 60, 0 = off) it sends KEEPALIVE_CMD
# (input lines split by '|'; default R1, the grab, with nothing in reach). The stick doesn't
# reset that timer, and the d-pad pauses (up) or leaves hover mode (down).
# Observed (2026-11-10), not explained: in 2 runs (R2+L2, and R1, each with a screenshot only
# every 10 minutes) the view moved 3.5-4.5 minutes in until the Thruster left the frame (frame
# work 23.8 -> 16 ms). R1 held in 3 other runs: a screenshot every 14 s (6 min), every 60 s
# (60 min), and every 10 minutes again (12 min). So the cause is unknown; the default is the
# setting of the hour that passed, R1 and a screenshot every 60 s (SHOT_EVERY), and the
# SCENE CHANGED verdict catches a repeat.
# The verdict adds IDLE if the attract video opened anyway, and SCENE CHANGED if the frame
# work fell below 75% of its level in the scene's first minute for 10 s or more (that is how
# the Thruster leaving the view showed up).
cd /d/Recomps/lbp3-recompiled || exit 1
tag=$1; mins=$2; shift 2
keep=${KEEPALIVE:-60}; kcmd=${KEEPALIVE_CMD:-r1 150}; every=${SHOT_EVERY:-60}
. /d/Recomps/lbp3-pc/.scratch/lbp3-port/tools/saves.sh; saves_keep  # the human's current saves come back on exit
restore() { saves_baseline; }
# The start inputs are timed, and once (2026-11-10) they left the game on the Pod's Home menu.
# So the start screenshot must show the bright Create sky (mean grey level above 120 in the
# region x 700-1200, y 40-250: the scene measures about 157, the Pod 28, the attract video 61),
# or the launch is tried again, up to 3 times.
try=1
while :; do
  taskkill //f //im lbp3.exe >/dev/null 2>&1; sleep 1; rm -f analysis/input_cmd.txt
  restore || exit 1
  powershell -NoProfile -Command "Set-Location D:\\Recomps\\lbp3-recompiled; & .\\tools\\remote_start.ps1 $*" >/dev/null || exit 1
  sleep 28
  printf 'cross\nwait 2000\nsquare\nwait 2500\n' > analysis/input_cmd.txt; sleep 6
  printf 'square 100\n' > analysis/input_cmd.txt; sleep 25
  powershell -NoProfile -File tools/capture.ps1 -Out "analysis/screenshots/${tag}_start.png" >/dev/null
  sky=$(cd analysis/screenshots && python -c "from PIL import Image, ImageStat; print(int(ImageStat.Stat(Image.open('${tag}_start.png').convert('L').crop((700, 40, 1200, 250))).mean[0]))")
  [ "${sky:-0}" -gt 120 ] && break
  echo "$tag: launch $try did not reach the scene (sky level ${sky:-unknown})"
  if [ $try -ge 3 ]; then echo "$tag: NO SCENE"; exit 1; fi  # the exit puts the saves back
  try=$(( try + 1 ))
done
start=$(date +%s); end=$(( start + mins * 60 )); next=$(( start + keep )); shot=$(( start + every ))
while [ "$(date +%s)" -lt "$end" ]; do
  tasklist //fi "imagename eq lbp3.exe" | grep -q lbp3 || break
  now=$(date +%s)
  if [ "$keep" -gt 0 ] && [ "$now" -ge "$next" ]; then
    printf '%s\n' "$kcmd" | tr '|' '\n' > analysis/input_cmd.tmp && mv analysis/input_cmd.tmp analysis/input_cmd.txt
    next=$(( next + keep ))
  fi
  if [ "$now" -ge "$shot" ]; then  # a screenshot every SHOT_EVERY seconds (default 60), to see the scene is still there
    powershell -NoProfile -File tools/capture.ps1 -Out "analysis/screenshots/${tag}_m$(( (now - start) / 60 )).png" >/dev/null
    shot=$(( shot + every ))
  fi
  sleep 10
done
flips() { grep -o '^\[video\] [0-9]* frames flipped' analysis/remote_stderr.txt | tail -1 | awk '{print $2}'; }
a=$(flips); sleep 8; b=$(flips)
log="analysis/${tag}_stderr.txt"
cp analysis/remote_stderr.txt "$log"
# The strings come from the diag build only; on any build, frames must still be flipping.
if grep -q 'device lost' "$log"; then out=RESET
elif grep -q 'WaitRegMem stuck' "$log"; then out=DEADLOCK
elif [ -z "$b" ] || [ "$a" = "$b" ]; then out=STALLED
else out=OK; fi
n=$(grep -n 'open /app0/gamedata_orbis/videos/lbp3/lbp3_attract.bik' "$log" | head -1 | cut -d: -f1)
if [ -n "$n" ]; then
  at=$(head -n "$n" "$log" | grep -o '^\[video\] [0-9]* frames flipped' | tail -1 | awk '{print $2}')
  out="$out, but IDLE: the attract video replaced the scene at frame $at"
fi
# Baseline: the mean work of the status lines for frames 1500-3300 (the scene starts near 960).
drop=$(grep -o '^\[video\] [0-9]* frames flipped.*work [0-9.]* ms' "$log" | awk '
  { f = $2; for (i = 1; i <= NF; i++) if ($i == "work") w = $(i + 1) }
  f >= 1500 && f <= 3300 { sum += w; n++; next }
  f > 3300 && n && !done {
    b = sum / n
    if (w < 0.75 * b) { if (!run) at = f; run++; if (run >= 10) { printf "%d %.1f %.1f", at, b, w; done = 1 } }
    else run = 0
  }')
if [ -n "$drop" ]; then
  set -- $drop
  out="$out, but SCENE CHANGED: the frame work fell from $2 ms to $3 ms at frame $1"
fi
echo "$tag: $out after $(grep -o '^\[video\] [0-9]* frames flipped' "$log" | tail -1)"
taskkill //f //im lbp3.exe >/dev/null 2>&1
sleep 2; saves_return  # put the human's current saves back
