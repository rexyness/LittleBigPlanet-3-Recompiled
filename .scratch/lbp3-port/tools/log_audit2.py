r"""Cross-session aggregation for the log audit (log-audit lane, 2026-11-13).

Prints compact tables: stubs across sessions, warnings, slow-frame classes,
early releases and dropped draws, and the gfx-blocked distribution.
"""
import collections
import os
import re
import sys

A = r"D:\Recomps\lbp3-recompiled\analysis"
OLD = [("11-11 17:53", r"play_logs_20261111\play_log.3.txt"),
       ("11-11 18:59", r"play_logs_20261111\play_log.2.txt"),
       ("11-11 20:09", r"play_logs_20261111\play_log.1.txt"),
       ("11-11 21:03", r"play_logs\play_log_20261111_2103.txt")]
NEW = [("11-12 14:58 fg", r"play_logs\play_fg_20261112_1458.txt"),
       ("11-12 18:31 1440", r"play_1440_log.txt"),
       ("11-12 18:42 1440a", r"play_1440a_log.txt"),
       ("11-12 19:55 play.bat", r"play_logs\play_log_20261112_1955.txt"),
       ("11-12 20:54 dlc", r"play_logs\play_dlc_20261112_2054.txt"),
       ("11-12 21:43 unlocks", r"play_logs\play_unlocks_20261112_2143.txt"),
       ("11-12 21:59 play.bat", r"play_logs\play_log_20261112_2159.txt")]
NUM = re.compile(r"0x[0-9a-fA-F]+|[0-9a-fA-F]{8,}|\d+(\.\d+)?")
FARC = re.compile(r"\[fs\] open (/app0/gamedata/[^ ]+?\.farc)")
VIDEO = re.compile(r"^\[video\] (\d+) frames flipped \(avg ([\d.]+) ms, worst ([\d.]+) ms, (\d+) over 40 ms;(.*?)(\d+) early releases.*?work ([\d.]+) ms")
PERF = re.compile(r"^\[perf\] compiled (\d+) pipelines \((\d+) ms\), (\d+) shaders.*?gfx blocked ([\d.]+) ms/frame in (\d+) waits; early releases: wait-idle (\d+)\+(\d+), compute (\d+)\+(\d+), submit (\d+)\+(\d+).*?dropped (\d+) draws")


def tmpl(l):
    return NUM.sub("N", l)[:160]


def scan(label, path, out, agg):
    lines = open(os.path.join(A, path), encoding="utf-8", errors="replace").read().splitlines()
    last_farc_line = -10**9
    last_farc = "(none yet)"
    farcs = []
    compiled_recent = collections.deque(maxlen=3)
    classes = collections.Counter()
    worst = []
    early = []
    drops = []
    blocked = []
    for i, l in enumerate(lines, 1):
        fm = FARC.search(l)
        if fm:
            last_farc_line = i
            last_farc = fm.group(1).split("/gamedata/")[1]
            if "level" in last_farc and not last_farc.endswith("_container.farc") and (not farcs or farcs[-1] != last_farc):
                farcs.append(last_farc)
        if l.startswith("[stub]"):
            agg["stub"][l[7:].strip()][label] += 1
        elif l.startswith("[shader warning]"):
            agg["shaderwarn"][tmpl(l)][label] += 1
        elif l.startswith("[audio]") and "underrun" in l:
            pm = re.search(r"port (\d+) underrun \((\d+) so far\)", l)
            if pm:
                agg["underrun"]["port " + pm.group(1)][label] = max(agg["underrun"]["port " + pm.group(1)][label], int(pm.group(2)))
        elif l.startswith("[savedata]") and re.search(r"fail|error|denied|refus|corrupt|0x80|not found", l, re.I):
            agg["savedata"][tmpl(l)][label] += 1
        elif l.startswith("[fs] open (missing)"):
            p = l[len("[fs] open (missing) "):].split("  ->")[0]
            if not p.startswith("/temp0/hdd0/CUSA00473_00/gamedata/"):
                agg["missing"][re.sub(r"\d+", "N", p)][label] += 1
        elif re.match(r"^\[(gpu|gnm|kernel|net|np|input|modal|overlay|dialog|osk|ime|jpeg|addcont|unlocks|res|thread|start|game|natives|entry)\]", l):
            if re.search(r"warn|error|fail|refus|lost|stuck|timeout|timed out|invalid|unknown|unsupported|not (found|supported)|drop", l, re.I) and "timed lock #" not in l:
                agg["warnlines"][tmpl(l)][label] += 1
        elif not l.startswith("[") and l.strip() and not l.startswith(("  ", "loaded ", "patched ", "imports:", "missing (", "=== crash", "read of", "rax=", "rsi=", "r8 =", "r10", "stack (", "last unimpl")):
            agg["untagged"][tmpl(l)][label] += 1
        pm = PERF.match(l)
        if pm:
            n = int(pm.group(1))
            compiled_recent.append(n)
            g = list(map(int, pm.groups()[5:11]))
            if any(g):
                early.append((i, last_farc, g))
            if int(pm.group(12)):
                drops.append((i, last_farc, int(pm.group(12))))
            blocked.append(float(pm.group(4)))
        vm = VIDEO.match(l)
        if vm and int(vm.group(4)) > 0:
            over = int(vm.group(4)); work = float(vm.group(7)); w = float(vm.group(3))
            near_load = i - last_farc_line < 120
            compiling = sum(compiled_recent) > 0
            if near_load:
                c = "level load (archive opened within 120 lines)"
            elif compiling:
                c = "pipeline compiles in the window"
            elif work > 33.3:
                c = "work over budget (no load, no compile)"
            else:
                c = "spike (no load, no compile, work in budget)"
            classes[c] += over
            worst.append((w, i, last_farc, over, work, c))
        if vm and int(vm.group(6)) > 0:
            early.append((i, last_farc, ["video early releases", int(vm.group(6))]))
    out.write("\n== %s (%s)\nlevel archives: %s\n" % (label, path, " > ".join(f.split("/")[-1] for f in farcs) or "(none)"))
    out.write("frames over 40 ms by class: %s\n" % dict(classes))
    for w in sorted(worst, reverse=True)[:6]:
        out.write("  worst %.0f ms line %d (%s) over40 %d work %.1f [%s]\n" % (w[0], w[1], w[2][-45:], w[3], w[4], w[5]))
    spikes = [w for w in worst if w[5].startswith("spike") or w[5].startswith("work over")]
    out.write("  no-load/no-compile windows: %d, frames %d; first few: %s\n" % (len(spikes), sum(s[3] for s in spikes), [(s[1], s[2][-30:], s[3], s[4]) for s in spikes[:5]]))
    out.write("early releases (perf/video): %d windows; first: %s\n" % (len(early), early[:4]))
    out.write("dropped draws: %s\n" % (drops[:5] or "none"))
    if blocked:
        b = sorted(blocked)
        out.write("gfx blocked ms/frame: median %.2f p95 %.2f max %.2f\n" % (b[len(b) // 2], b[int(len(b) * .95)], b[-1]))


def table(out, title, d, labels):
    out.write("\n### %s\n" % title)
    for k, per in sorted(d.items(), key=lambda kv: -sum(kv[1].values())):
        out.write("  %6d  %s  | %s\n" % (sum(per.values()), k, ", ".join("%s:%d" % (lb.split()[1] if len(lb.split()) > 1 else lb, per[lb]) for lb in labels if per.get(lb))))


def main():
    with open(sys.argv[1], "w", encoding="utf-8") as out:
        for group, sessions in (("OLD build (11-11, before the 11-12 fixes)", OLD), ("CURRENT build era (11-12)", NEW)):
            out.write("\n\n######## %s\n" % group)
            agg = collections.defaultdict(lambda: collections.defaultdict(collections.Counter))
            for label, path in sessions:
                scan(label, path, out, agg)
            labels = [s[0] for s in sessions]
            for key, title in (("stub", "[stub] calls"), ("underrun", "audio underruns (max running count)"),
                               ("savedata", "savedata errors"), ("missing", "missing files (excluding the patch-overlay probes)"),
                               ("shaderwarn", "shader/texture-cache warnings"), ("warnlines", "other warning-like lines"),
                               ("untagged", "untagged lines")):
                table(out, title, agg[key], labels)


main()
