"""Milestone 5 (perf/intro_stalls.md): the Introduction route's stalls across runs, one row per run.

    python -I intro_stalls_table.py <tag> [<tag> ...]      (run from analysis/intro_stalls)

For each run (<tag>_stamped.txt + <tag>_split.csv from intro_walk.sh / intro_stock.sh): the boot
frames, the Pod's first frame, the Prologue map's first frame (flip periods and the frame split's
phases of the slow frame), and the sim pauses of the Introduction's load and of the Legend of
Bunkum's load (flips with no game frame, from the LBP3_TRACE_SIM string, and their span).
"""
import csv
import re
import sys


def runs_of_zero(s):
    """(start, flips with 0 game frames, span to the next run of three 1s) for each pause >= 6 flips."""
    out = []
    for m in re.finditer(r"[0?][0?2]*", s):
        seg = m.group()
        zeros = seg.count("0")
        if zeros >= 6:
            out.append((m.start(), zeros, len(seg)))
    return out


def main():
    print(f"{'run':8} {'trace':5} {'Pod frame':>10} {'(prep/rec/GPU)':>16} {'map frame':>10} {'(prep/rec/GPU)':>16} "
          f"{'Intro load':>16} {'Bunkum load':>16}")
    for tag in sys.argv[1:]:
        rows = list(csv.DictReader(open(f"{tag}_split.csv", newline="")))
        rows = [{k: float(v) for k, v in r.items()} for r in rows]
        t0 = rows[0]["submit_ms"]
        log = [l.rstrip("\n").split("\t", 1) for l in open(f"{tag}_stamped.txt", encoding="utf-8", errors="replace")
               if "\t" in l]
        traced = any(re.match(r"^\[fs\] /\S+: read \d+ bytes", l) for _, l in log)
        game = "".join(m.group(1) for _, l in log if (m := re.match(r"^\[sim\] (?:\d\d:\d\d:\d\d\.\d{3} )?steps per flip: \S+ \| game frames: (\S+)", l)))

        def slow_frame(lo, hi):
            cand = [r for r in rows if lo <= (r["submit_ms"] - t0) / 1000 <= hi and r["period_us"] > 100000]
            if not cand:
                return "-", "-"
            r = max(cand, key=lambda r: r["period_us"])
            return (f"{r['period_us'] / 1000:.0f} ms",
                    f"{r['prep_us'] / 1000:.0f}/{r['record_us'] / 1000:.0f}/{r['finish_us'] / 1000:.0f}")

        pod, pod_ph = slow_frame(8.9, 10.5)
        mp, mp_ph = slow_frame(43, 47)
        pauses = runs_of_zero(game)
        def pause_near(t_lo, t_hi):
            for start, zeros, span in pauses:
                if start < len(rows) and t_lo <= (rows[start]["submit_ms"] - t0) / 1000 <= t_hi:
                    end = min(start + span, len(rows) - 1)
                    return f"{zeros} ({(rows[end]['submit_ms'] - rows[start]['submit_ms']) / 1000:.2f} s)"
            return "-"
        intro = pause_near(50, 56)
        bunkum = pause_near(150, 260)
        print(f"{tag:8} {'yes' if traced else 'no':5} {pod:>10} {pod_ph:>16} {mp:>10} {mp_ph:>16} {intro:>16} {bunkum:>16}")


if __name__ == "__main__":
    main()
