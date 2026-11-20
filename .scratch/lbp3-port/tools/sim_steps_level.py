"""Share of flips whose sim step count isn't 1, over one level only (ticket 09's measure).

Reads a stderr log written with LBP3_TRACE_SIM=1. Counting starts at the level's .farc open,
then after the story save that entering the level makes, then at the first flip after which
one second (30 flips) has no zero-step flip, so neither the load's tail nor the save's pause
is counted. Everything to the end of the log counts.

The story save (savedata mounts of the progress and profile saves) stalls the sim for 14-22
flips. It usually comes at once, before the level has run a clean second; when it came about
50 flips later, the old count included it and read 1.4-2.7% instead of under 1% (ticket 12,
2026-11-11). Its position is known to the nearest [sim] line (every 60 flips), which is
early, never late. It can still fall about 30 flips after the mount (2026-11-11 coop_m1:
21 zero-step flips at level flips 30-53), so counting also never starts before flip
SETTLE (150, the level's first 5 s: the load and the story save).

    python sim_steps_level.py <stderr.txt> [level farc substring, default 00_craftworld/craftworld.farc]
"""
import re
import sys
from collections import Counter

SAVE_WITHIN = 600  # flips (20 s) after the level opens in which a savedata mount is the story save
SETTLE = 150  # flips (5 s) after the level opens that are never counted

path = sys.argv[1]
marker = sys.argv[2] if len(sys.argv) > 2 else "00_craftworld/craftworld.farc"
lines = open(path, encoding="utf-8", errors="replace").read().splitlines()
load = next((i for i, l in enumerate(lines) if marker in l), None)
if load is None:
    sys.exit(f"{path}: {marker} never opened (wrong level?)")
steps = ""
save = 0  # flip index of the last story-save mount
for l in lines[load:]:
    m = re.match(r"\[sim\] (?:\d\d:\d\d:\d\d\.\d{3} )?steps per flip: (\S+)", l)
    if m:
        steps += m.group(1)
    elif l.startswith("[savedata] mount ") and len(steps) <= SAVE_WITHIN:
        save = len(steps)
start = next((i for i in range(max(save, SETTLE), len(steps)) if "0" not in steps[i:i + 30]), None)
if start is None:
    sys.exit(f"{path}: the level never ran for a second without a zero-step flip")
level = steps[start:]
bad = [c for c in level if c != "1"]
print(f"{path}: {len(level)} flips in the level, steps != 1: {len(bad)} = {100 * len(bad) / len(level):.2f}% "
      f"{dict(Counter(bad))} (counted from flip {start}; story save by flip {save})")
