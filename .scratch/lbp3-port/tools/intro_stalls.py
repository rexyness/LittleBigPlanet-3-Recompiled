"""Milestone 5: the Introduction's stalls, per stall, from one run (perf/intro_stalls.md).

    python -I intro_stalls.py <tag_stamped.txt> <tag_split.csv> [--min-zero N] [--long-ms MS] [--quiet]

Inputs: the log stamped by intro_drive.py (each line "<QPC ms>\\t<line>") and frame_split.csv
(LBP3_FRAME_SPLIT=1), both on the QueryPerformanceCounter clock.

A "sim stall" is a run of at least --min-zero (default 6) flips whose LBP3_TRACE_SIM "game frames"
count is 0: the game keeps submitting and flipping but its world doesn't advance. A "long frame"
is a flip-to-flip period over --long-ms (default 100 ms). The [sim] string's flips are matched to
frame_split rows by the offset that best fits "ms between submits" to submit_ms deltas.

Per stall: its time (s from the first split row), length, the frame split's phases inside it
against the 5 s before it, the archive and bank reads (LBP3_TRACE_READS lines, by thread and
file) from 2 s before it to its end, "[fs] slow read" lines, and the savedata, file-open and
F10 events in the window.
"""
import csv
import re
import statistics
import sys
from collections import defaultdict


def opt(name, default, cast=float):
    return cast(sys.argv[sys.argv.index(name) + 1]) if name in sys.argv else default


SIM_RX = re.compile(r"^\[sim\] (?:\d\d:\d\d:\d\d\.\d{3} )?steps per flip: (\S+) \| game frames: (\S+)")
GAP_RX = re.compile(r"^\[sim\] ms between submits: (.*)")
READ_RX = re.compile(r"^\[fs\] (/\S+): read (\d+) bytes at (-?\d+) -> (\d+) \[(.*)\]")
WRITE_RX = re.compile(r"^\[fs\] (/\S+): write (\d+) bytes at (-?\d+) -> (\S+) (\d+) \[(.*)\]")
SLOW_RX = re.compile(r"^\[fs\] slow read ([\d.]+) ms \(disk ([\d.]+), copy ([\d.]+)\): (\S+), (\d+) bytes.*\[(.*)\]")
EVENT_RX = re.compile(r"^(\[savedata\]|\[fs\] open |\[perf\]|\[drive\]|\[fs\] close |\[video\] \d+ frames|\[sample\] '|-- )")

PHASES = ["main_cpu_us", "waitidle_us", "prep_us", "record_us", "gfx_blocked_us", "finish_us", "copy_us",
          "slot_us", "period_us"]


def load_stamped(path):
    out = []
    for raw in open(path, encoding="utf-8", errors="replace"):
        raw = raw.rstrip("\n")
        if "\t" not in raw:
            continue
        t, line = raw.split("\t", 1)
        try:
            out.append((float(t), line))
        except ValueError:
            pass
    return out


def load_split(path):
    rows = []
    for r in csv.DictReader(open(path, newline="")):
        f = {k: (float(v) if k == "submit_ms" else int(v)) for k, v in r.items()}
        rows.append(f)
    rows.sort(key=lambda f: f["frame"])
    return rows


def sim_flips(lines):
    """Per flip: (game frames char, ms between submits), in order, and each line's stamp."""
    game, gaps, stamps = [], [], []
    pending = None
    for t, line in lines:
        m = SIM_RX.match(line)
        if m:
            pending = (t, m.group(2))
            continue
        g = GAP_RX.match(line)
        if g and pending:
            vals = [int(x) for x in g.group(1).split()]
            chars = pending[1]
            n = min(len(vals), len(chars))
            game += list(chars[:n])
            gaps += vals[:n]
            stamps += [pending[0]] * n
            pending = None
    return game, gaps, stamps


def align(gaps, rows):
    """Offset o such that sim flip i is frame row i + o (best fit of the submit gaps)."""
    deltas = [None] + [rows[i]["submit_ms"] - rows[i - 1]["submit_ms"] for i in range(1, len(rows))]
    best = None
    for o in range(-15, 16):
        err, n = 0.0, 0
        for i, g in enumerate(gaps):
            j = i + o
            if 1 <= j < len(rows):
                err += abs(deltas[j] - g)
                n += 1
        if n > 100 and (best is None or err / n < best[0]):
            best = (err / n, o)
    return best


def mean(vals):
    vals = [v for v in vals if v >= 0]
    return statistics.fmean(vals) / 1000.0 if vals else float("nan")


def phase_line(frames):
    return "  ".join(f"{p.replace('_us', '')} {mean([f[p] for f in frames]):6.2f}" for p in PHASES)


def main():
    stamped, split = sys.argv[1], sys.argv[2]
    min_zero, long_ms = int(opt("--min-zero", 6)), opt("--long-ms", 100.0)
    lines = load_stamped(stamped)
    rows = load_split(split)
    t0 = rows[0]["submit_ms"]
    rel = lambda ms: (ms - t0) / 1000.0
    game, gaps, _ = sim_flips(lines)
    fit = align(gaps, rows)
    print(f"{stamped}: {len(lines)} log lines, {len(rows)} split rows, {len(game)} traced flips; "
          f"sim->split offset {fit[1] if fit else '?'} (mean gap error {fit[0] if fit else float('nan'):.2f} ms)")
    off = fit[1] if fit else 0
    # Stamp check: [video] N frames flipped is printed at flip N.
    vid = [(t, int(m.group(1))) for t, l in lines if (m := re.match(r"^\[video\] (\d+) frames flipped", l))]
    d = [t - rows[n - 1]["submit_ms"] for t, n in vid if 0 < n <= len(rows)]
    if d:
        print(f"  stamp check: '[video] N frames flipped' seen {statistics.median(d):.0f} ms after frame N's submit "
              f"(median; min {min(d):.0f}, max {max(d):.0f})")
    events = []  # (time ms, kind, payload)
    reads = []  # (t, path, bytes, thread)
    for t, l in lines:
        if m := READ_RX.match(l):
            reads.append((t, m.group(1), int(m.group(4)), m.group(5)))
        elif m := SLOW_RX.match(l):
            events.append((t, "slow", l))
        elif EVENT_RX.match(l):
            events.append((t, "event", l))
        elif m := WRITE_RX.match(l):
            events.append((t, "write", l))
    level = next((t for t, l in lines if "00_craftworld/craftworld.farc  ->" in l), None)
    print(f"  Introduction's farc opened at {rel(level):.1f} s" if level else "  level farc not seen")
    # Sim stalls.
    stalls = []
    i = 0
    while i < len(game):
        if game[i] == "0":
            j = i
            while j < len(game) and game[j] in "0":
                j += 1
            if j - i >= min_zero:
                stalls.append(("sim", i + off, j + off))
            i = j
        else:
            i += 1
    # Long frames.
    for k, f in enumerate(rows):
        if f["period_us"] > long_ms * 1000:
            stalls.append(("long", k, k + 1))
    stalls.sort(key=lambda s: rows[min(max(s[1], 0), len(rows) - 1)]["submit_ms"])
    print(f"  {sum(1 for s in stalls if s[0] == 'sim')} sim stalls (>= {min_zero} flips with no game frame), "
          f"{sum(1 for s in stalls if s[0] == 'long')} frames over {long_ms:.0f} ms")
    all_frames = [f for f in rows if f["record_us"] >= 0]
    print(f"  whole run: {phase_line(all_frames)}")
    for kind, a, b in stalls:
        if a < 0 or a >= len(rows):
            continue
        b = min(b, len(rows))
        ta = rows[a]["submit_ms"]
        tb = rows[b]["submit_ms"] if b < len(rows) else rows[-1]["submit_ms"]
        inside = rows[a:b]
        before = [f for f in rows if ta - 5000 <= f["submit_ms"] < ta - 200]
        tag = "in level" if level and ta >= level else "before level"
        if kind == "sim":
            print(f"\n== sim stall at {rel(ta):.2f} s ({tag}): {b - a} flips with no game frame, {(tb - ta) / 1000:.2f} s "
                  f"(frames {rows[a]['frame']}-{rows[b - 1]['frame']}); worst flip period "
                  f"{max(f['period_us'] for f in inside) / 1000:.1f} ms")
        else:
            f = rows[a]
            print(f"\n== long frame at {rel(ta):.2f} s ({tag}): frame {f['frame']} period {f['period_us'] / 1000:.1f} ms; "
                  f"compiles {f['pipelines']} pipelines {f['pipeline_us'] / 1000:.1f} ms, {f['shaders']} shaders "
                  f"{f['shader_us'] / 1000:.1f} ms")
            # The frame whose period is long: the time went into this frame or the one before.
            inside = rows[max(a - 1, 0):a + 1]
            for g in inside:
                print("   frame %d: %s" % (g["frame"], phase_line([g])))
        print(f"   inside: {phase_line(inside)}")
        print(f"   before: {phase_line(before)}")
        w0, w1 = ta - 2000, tb + 300
        rd = [r for r in reads if w0 <= r[0] <= w1]
        if rd:
            by = defaultdict(lambda: [0, 0, None, None])
            for t, p, n, th in rd:
                e = by[(th, p.split("/")[-1])]
                e[0] += 1
                e[1] += n
                e[2] = t if e[2] is None else e[2]
                e[3] = t
            print(f"   reads {rel(w0):.2f}-{rel(w1):.2f} s: {len(rd)} reads, {sum(r[2] for r in rd) / 1e6:.1f} MB")
            for (th, p), (n, nb, fa, fb) in sorted(by.items(), key=lambda kv: -kv[1][1])[:10]:
                print(f"     {th:22} {p:34} {n:5d} reads {nb / 1e6:7.2f} MB  {rel(fa):7.2f}-{rel(fb):7.2f} s")
        shown = 0
        for t, k, l in events:
            if not (w0 <= t <= w1 + 1200):
                continue
            if k == "event" and (l.startswith("[fs] close") or l.startswith("[fs] open /temp0")
                                 or ".fev  ->" in l or "fonts/" in l or "[savedata] mounted" in l
                                 or "[savedata] set param" in l or "[savedata] save icon" in l
                                 or l.startswith("[perf] compiled 0 pipelines (0 ms), 0 shaders (0 ms)")):
                continue
            if k == "write" and ("temp_orbis" in l):
                continue
            if t > w1 and not l.startswith("[video]") and not l.startswith("[perf]"):
                continue
            if l.startswith("[video]"):
                l = re.sub(r" \(avg.*?; ", " (", l)[:150]
            if shown < 40:
                print(f"     {rel(t):7.2f} s  {l[:190]}")
            shown += 1
        if shown > 40:
            print(f"     ... {shown - 40} more events")


if __name__ == "__main__":
    main()
