r"""Audit of the human play-session logs (log-audit lane, 2026-11-13).

Usage: python -I log_audit.py <out.txt>
Reads the unique session logs in the clone's analysis\ folder and prints one report.
"""
import collections
import os
import re
import sys

A = r"D:\Recomps\lbp3-recompiled\analysis"
SESSIONS = [  # (label, path, human?)
    ("11-11 17:53", r"play_logs_20261111\play_log.3.txt", True),
    ("11-11 18:59", r"play_logs_20261111\play_log.2.txt", True),
    ("11-11 20:09", r"play_logs_20261111\play_log.1.txt", True),
    ("11-11 21:03", r"play_logs\play_log_20261111_2103.txt", True),
    ("11-12 13:11 (agent: play_fg.bat check)", r"play_fg_log.1.txt", False),
    ("11-12 14:58 fg", r"play_logs\play_fg_20261112_1458.txt", True),
    ("11-12 18:31 1440", r"play_1440_log.txt", True),
    ("11-12 18:42 1440a", r"play_1440a_log.txt", True),
    ("11-12 19:55 play.bat", r"play_logs\play_log_20261112_1955.txt", True),
    ("11-12 20:54 dlc", r"play_logs\play_dlc_20261112_2054.txt", True),
    ("11-12 21:43 unlocks", r"play_logs\play_unlocks_20261112_2143.txt", True),
    ("11-12 21:59 play.bat", r"play_logs\play_log_20261112_2159.txt", True),
    ("11-12 23:18 (agent: verify boot)", r"play_logs\play_log_20261112_2318.txt", False),
    ("11-13 00:45 (agent: cache boot)", r"play_log.txt", False),
]

NUM = re.compile(r"0x[0-9a-fA-F]+|[0-9a-fA-F]{8,}|\d+(\.\d+)?")
LEVEL = re.compile(r"\[fs\] open (?:\(missing\) )?/app0/gamedata/lbp3/(?:official_adventure_levels|levels)/([^ ]+?\.farc)")
VIDEO = re.compile(r"^\[video\] (\d+) frames flipped \(avg ([\d.]+) ms, worst ([\d.]+) ms, (\d+) over 40 ms;.*?(\d+) early releases.*?work ([\d.]+) ms")
PERF = re.compile(r"^\[perf\] compiled (\d+) pipelines \((\d+) ms\), (\d+) shaders.*?gfx blocked ([\d.]+) ms/frame in (\d+) waits; early releases: wait-idle (\d+)\+(\d+), compute (\d+)\+(\d+), submit (\d+)\+(\d+).*?dropped (\d+) draws")


def template(line):
    return NUM.sub("N", line.rstrip("\n"))[:170]


def audit(label, path, out):
    full = os.path.join(A, path)
    size = os.path.getsize(full)
    lines = open(full, encoding="utf-8", errors="replace").read().splitlines()
    cat_bytes = collections.Counter()
    stubs = collections.OrderedDict()
    tmpl = {k: collections.Counter() for k in ("shader warning", "gpu", "savedata", "audio", "kernel-other", "fs-missing", "net-other", "misc-warn")}
    first_line = {}
    level = "boot"
    level_seen = []
    slow = []          # (line no, level, worst, over40, work)
    early = []
    perf_bad = []
    flips = 0
    crash = []
    notlist = []
    level_open_at = {}
    for i, l in enumerate(lines, 1):
        m = re.match(r"^\[([A-Za-z0-9_ -]+)\]", l)
        cat = m.group(1) if m else ("(stub table)" if l.startswith("  ") else "(other)")
        cat_bytes[cat] += len(l) + 1
        lm = LEVEL.search(l)
        if lm and not lm.group(1).endswith("_container.farc"):
            level = lm.group(1)
            level_open_at[i] = level
            if not level_seen or level_seen[-1] != level:
                level_seen.append(level)
        if l.startswith("[stub]"):
            k = l[7:].strip()
            if k not in stubs:
                stubs[k] = [0, i, level]
            stubs[k][0] += 1
        elif l.startswith("[shader warning]"):
            t = template(l)
            tmpl["shader warning"][t] += 1
            first_line.setdefault(t, i)
        elif l.startswith("[gpu]") and not l.startswith("[gpu] geometry limits"):
            t = template(l); tmpl["gpu"][t] += 1; first_line.setdefault(t, i)
        elif l.startswith("[savedata]") and re.search(r"fail|error|not found|denied|refus|corrupt|0x80", l, re.I):
            t = template(l); tmpl["savedata"][t] += 1; first_line.setdefault(t, i)
        elif l.startswith("[audio]") and "underrun" in l:
            pm = re.search(r"port (\d+) underrun \((\d+) so far\)", l)
            if pm:
                tmpl["audio"]["port %s underruns (max count)" % pm.group(1)] = max(tmpl["audio"]["port %s underruns (max count)" % pm.group(1)], int(pm.group(2)))
        elif l.startswith("[kernel]") and "timed lock #" not in l:
            t = template(l); tmpl["kernel-other"][t] += 1; first_line.setdefault(t, i)
        elif l.startswith("[fs] open (missing)"):
            t = l[len("[fs] open (missing) "):].split("  ->")[0]
            tmpl["fs-missing"][t] += 1; first_line.setdefault(t, i)
        elif l.startswith("[net]") and not re.match(r"\[net\] (loopback:|loopback recv|loopback send|posix)", l):
            t = template(l); tmpl["net-other"][t] += 1; first_line.setdefault(t, i)
        elif re.search(r"\b(warning|error|failed|fatal|assert|stuck|timeout|timed out|lost)\b", l, re.I) and not l.startswith(("[shader warning]", "[savedata]", "[gpu]", "[fs]", "[stub]")):
            t = template(l); tmpl["misc-warn"][t] += 1; first_line.setdefault(t, i)
        if "NOT on the list" in l:
            notlist.append((i, l[:160]))
        if l.startswith("=== crash"):
            crash.append((i, l, level))
        vm = VIDEO.match(l)
        if vm:
            flips = int(vm.group(1))
            if int(vm.group(4)) > 0:
                slow.append((i, level, float(vm.group(3)), int(vm.group(4)), float(vm.group(6))))
            if int(vm.group(5)) > 0:
                early.append((i, level, int(vm.group(5))))
        pm = PERF.match(l)
        if pm:
            g = list(map(int, pm.groups()[5:11])) + [int(pm.group(12))]
            if any(g) or float(pm.group(4)) > 2.0:
                perf_bad.append((i, level, pm.group(4), pm.group(5), g))
    hours = flips / 30 / 3600 if flips else 0
    out.write("\n" + "=" * 100 + "\n%s  %s  (%s)\n" % (label, path, "human" if True else ""))
    out.write("size %.2f MB, %d lines, last flip count %d = %.1f min" % (size / 1e6, len(lines), flips, flips / 1800))
    if hours:
        out.write(", %.2f MB/h\n" % (size / 1e6 / hours))
    else:
        out.write("\n")
    out.write("bytes by category (top 12):\n")
    for c, b in cat_bytes.most_common(12):
        out.write("  %-18s %8.1f KB  %5.1f%%%s\n" % (c, b / 1024, 100 * b / size, ("  %.2f MB/h" % (b / 1e6 / hours)) if hours else ""))
    out.write("levels opened (in order, deduped consecutive): %s\n" % " > ".join(level_seen))
    out.write("[stub] lines: %d unique, %d total\n" % (len(stubs), sum(v[0] for v in stubs.values())))
    for k, (n, ln, lv) in stubs.items():
        out.write("  %5dx first at line %-6d (%s)  %s\n" % (n, ln, lv, k))
    for k in ("shader warning", "gpu", "savedata", "audio", "kernel-other", "fs-missing", "net-other", "misc-warn"):
        if tmpl[k]:
            out.write("%s:\n" % k)
            for t, n in tmpl[k].most_common(15):
                out.write("  %5dx (first line %s) %s\n" % (n, first_line.get(t, "-"), t))
    out.write("windows with frames over 40 ms: %d windows, %d frames\n" % (len(slow), sum(s[3] for s in slow)))
    for s in slow:
        # load context: a level archive opened within 400 lines before
        near = [lv for ln, lv in level_open_at.items() if s[0] - 400 <= ln <= s[0]]
        out.write("  line %-6d level %-50s worst %7.1f ms, %d over 40, work %.1f ms%s\n" % (s[0], s[1][-50:], s[2], s[3], s[4], "  [load nearby]" if near else ""))
    out.write("early releases in [video] windows: %s\n" % (early[:10] if early else "none"))
    out.write("[perf] windows with blocked waits > 2 ms/frame, early releases or dropped draws: %d\n" % len(perf_bad))
    for p in perf_bad[:12]:
        out.write("  line %-6d level %-40s gfx blocked %s ms/frame in %s waits; wait-idle/compute/submit/dropped %s\n" % (p[0], p[1][-40:], p[2], p[3], p[4]))
    out.write("NOT on the list: %s\n" % (notlist[:5] if notlist else "none"))
    out.write("crash blocks: %s\n" % (crash if crash else "none"))


def main():
    with open(sys.argv[1], "w", encoding="utf-8") as out:
        for label, path, human in SESSIONS:
            audit(label + ("" if human else "  [not a human session]"), path, out)


main()
