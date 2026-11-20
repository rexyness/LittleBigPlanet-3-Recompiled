import re, sys, collections
A = r"D:/Recomps/lbp3-recompiled/analysis/"
FARC = re.compile(r"\[fs\] open /app0/gamedata/lbp3/(?:official_adventure_levels|dlc|levels)?/?([^ ]+?)\.farc")
V = re.compile(r"^\[video\] (\d+) frames flipped \(avg ([\d.]+) ms, worst ([\d.]+) ms, (\d+) over 40 ms;.*?(\d+) early releases.*?work ([\d.]+) ms")
for name in sys.argv[1:]:
    lines = open(A + name, encoding="utf-8", errors="replace").read().splitlines()
    lvl = "boot/pod"; d = collections.defaultdict(list); er = collections.Counter(); o40 = collections.Counter()
    for l in lines:
        m = FARC.search(l)
        if m and not m.group(1).endswith("_container") and "/" in m.group(1):
            lvl = m.group(1).split("/")[-1]
        v = V.match(l)
        if v:
            d[lvl].append(float(v.group(6))); er[lvl] += int(v.group(5)); o40[lvl] += int(v.group(4))
    print("==", name)
    for k, w in d.items():
        s = sorted(w); n = len(s)
        print("  %-28s %5.1f min  work median %5.1f  p90 %5.1f  max %6.1f  over-budget windows %4d  frames>40 %5d  early rel %d" % (k, n / 60, s[n // 2], s[int(n * .9)], s[-1], sum(1 for x in s if x > 33.3), o40[k], er[k]))
