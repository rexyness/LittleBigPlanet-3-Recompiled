"""Per session and per level: first-sight pipeline/shader compiles from the [perf] lines.

Usage: python -I compiles_per_level.py <log> [<log> ...]
A level is the last official_adventure_levels/.../<name>.farc opened (not *_container); the
[perf] window's compiles are charged to it. Prints, per session, each level's compile count
and time, and the worst single window (the freeze one sees on entry).
"""
import re
import sys

LEVEL = re.compile(r"^\[fs\] open /app0/gamedata/lbp3/((?:official_adventure_levels|[a-z_]+_levels)/[^ ]+?)\.farc")
PERF = re.compile(r"^\[perf\] compiled (\d+) pipelines \((\d+) ms\), (\d+) shaders \((\d+) ms\)")
RES = re.compile(r"^\[res\]")
FG = re.compile(r"^\[framegen\] (GPU warp on|warp)")

for path in sys.argv[1:]:
    level = "(boot/menus)"
    order = []
    stats = {}
    res = fg = False
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = LEVEL.match(line)
            if m and not m.group(1).endswith("_container"):
                level = m.group(1).split("official_adventure_levels/")[-1]
                if level not in stats:
                    stats[level] = [0, 0, 0, 0, 0, 0]
                    order.append(level)
                continue
            if RES.match(line):
                res = True
            if FG.match(line):
                fg = True
            m = PERF.match(line)
            if m:
                p, pms, s, sms = map(int, m.groups())
                st = stats.setdefault(level, [0, 0, 0, 0, 0, 0])
                if level not in order:
                    order.append(level)
                st[0] += p; st[1] += pms; st[2] += s; st[3] += sms
                st[4] = max(st[4], pms + sms)
                st[5] += 1
    print(f"== {path}  (1440p patches: {'yes' if res else 'no'}, frame generation: {'yes' if fg else 'no'})")
    for lv in order:
        p, pms, s, sms, worst, n = stats[lv]
        if p or s:
            print(f"   {lv:55s} {p:5d} pipelines {pms:6d} ms | {s:4d} shaders {sms:5d} ms | worst window {worst:5d} ms")
