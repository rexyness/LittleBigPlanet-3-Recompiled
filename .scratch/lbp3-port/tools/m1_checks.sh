#!/bin/sh
# Milestone 1's scripted checks (1-4) on the current build, one after another.
#   m1_checks.sh <prefix>
# 1. The real Introduction, twice: steps != 1 over the level (under 1%), frame averages.
# 2. The Thruster save recipe, 3 runs (t10_ab.sh; OK means the save finished and frames go on).
# 3. The Legend of Bunkum plays its 16 voice lines and Needlepoint Peaks loads (t08_bunkum.sh).
# 4. The Create sky in the recipe's level screenshot is cyan (blue well above red), not yellow.
# Every script starts from the baseline save and puts the human's current saves back (saves.sh).
S="$(cd "$(dirname "$0")" && pwd -W)"; T="$(cd "$(dirname "$0")" && pwd)"
cd /d/Recomps/lbp3-recompiled || exit 1
p=$1
echo "== check 1: the Introduction, 2 runs"
for i in 1 2; do
  powershell -NoProfile -ExecutionPolicy Bypass -File "$S/pace_run_intro.ps1" -Tag "${p}_intro$i" >/dev/null 2>&1
  f="analysis/pace_${p}_intro${i}_stderr.txt"
  python "$T/sim_steps_level.py" "$f"
  n=$(grep -n '00_craftworld/craftworld.farc' "$f" | tail -1 | cut -d: -f1)
  [ -n "$n" ] && tail -n +"$n" "$f" | grep -o '^\[video\] [0-9]* frames flipped (avg [0-9.]* ms, worst [0-9.]* ms, [0-9]* over 40 ms' |
    awk '{split($0, a, "avg "); v = a[2] + 0; if (v < lo || !lo) lo = v; if (v > hi) hi = v; split($0, z, "ms, "); o += z[3] + 0}
         END {printf "  frame averages %.1f-%.1f ms; frames over 40 ms after the level opened: %d\n", lo, hi, o}'
done
echo "== check 2: the Thruster save recipe"
sh "$T/t10_ab.sh" "$p":3:-
echo "== check 3: the Legend of Bunkum"
sh "$T/t08_bunkum.sh" "${p}_bunkum"
echo "  cutscene watchdog lines (0 = the auto-skip stayed off): $(grep -c '^\[cutscene\]' "analysis/${p}_bunkum_stderr.txt")"
echo "== check 4: the Create sky"
(cd analysis/screenshots && python -c "
from PIL import Image, ImageStat
r, g, b = ImageStat.Stat(Image.open('ab_${p}_1_level.png').convert('RGB').crop((600, 40, 1000, 220))).mean
print(f'  sky mean RGB ({r:.0f}, {g:.0f}, {b:.0f}):', 'cyan' if b > r + 30 else ('yellow' if r > b + 30 else 'unclear'))")
