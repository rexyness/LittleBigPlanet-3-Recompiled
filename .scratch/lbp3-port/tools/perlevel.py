"""Frame statistics per level from a play log (lbp3.exe stderr).

The level is the last level .farc the log shows opening (a name without the base/patch/intro
archives). For each [video] status line (about every 30 frames) the window's average, worst,
frames over 40 ms and work time are attributed to the current level.

    python -I perlevel.py <play_log.txt> ...
"""
import re
import sys
from collections import OrderedDict

SKIP = ("base_", "chunk", "patch_", "intro_", "craftworld_")
OPEN = re.compile(r"\[fs\] open (?:\S*/)?([a-z0-9_]+)\.farc")
STAT = re.compile(r"\[video\] (\d+) frames flipped \(avg ([\d.]+) ms, worst ([\d.]+) ms, (\d+) over 40 ms;.*?work ([\d.]+) ms")

for path in sys.argv[1:]:
    level = "(boot)"
    per = OrderedDict()
    stalls = []
    lines = open(path, encoding="utf-8", errors="replace").read().splitlines()
    for i, line in enumerate(lines):
        m = OPEN.search(line)
        if m and not m.group(1).startswith(SKIP):
            level = m.group(1).replace("_container", "")
            continue
        m = STAT.search(line)
        if not m:
            continue
        avg, worst, over, work = float(m.group(2)), float(m.group(3)), int(m.group(4)), float(m.group(5))
        d = per.setdefault(level, {"n": 0, "avg": 0.0, "work": 0.0, "over": 0, "worst": 0.0, "slow": 0})
        d["n"] += 1
        d["avg"] += avg
        d["work"] += work
        d["over"] += over
        d["worst"] = max(d["worst"], worst)
        if avg > 36.0:
            d["slow"] += 1
        if worst >= 150.0:
            stalls.append((level, worst, i + 1))
    print(f"== {path}")
    print(f"  {'level':32} {'windows':>7} {'avg ms':>7} {'work ms':>7} {'>40ms':>6} {'slow win':>8} {'worst':>7}")
    for lv, d in per.items():
        n = d["n"]
        print(f"  {lv[:32]:32} {n:7d} {d['avg'] / n:7.1f} {d['work'] / n:7.1f} {d['over']:6d} {d['slow']:8d} {d['worst']:7.1f}")
    if stalls:
        print("  windows with a frame >= 150 ms (level, worst, log line):")
        for lv, w, ln in stalls[:25]:
            print(f"    {lv[:32]:32} {w:7.1f}  line {ln}")
        if len(stalls) > 25:
            print(f"    ... {len(stalls) - 25} more")
