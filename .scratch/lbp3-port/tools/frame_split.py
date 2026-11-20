"""Summarises analysis\\frame_split.csv (LBP3_FRAME_SPLIT=1, runtime/src/gpu/frame_split.cpp).

    python -I frame_split.py <frame_split.csv> [--from S] [--to S] [--over MS] [--ff MS]

--ff: with the first-frame columns (perf/first-frame build), list the frames whose whole-frame
recording (first hand-over to flip) exceeds this (default 50 ms) with their breakdown: buffer
syncs, staging, allocations, new images, uploads, detiling, expansions, binding, descriptors and
(LBP3_FIRST_FRAME=1) GPU time of copies, uploads and detiling.

--from / --to: seconds from the first row (default: all). --over: a frame is "slow" when its
flip-to-flip period exceeds this (default 34.0 ms: 33.3 ms plus timer jitter; at 165 Hz a late
frame takes 50 ms, at 240 Hz 37.5 ms).

Prints, per phase, n, mean, p50, p95 and max in ms (missing values, -1, are skipped), then for
the slow frames which phase grew: each slow frame is put on the phase whose time exceeds that
phase's median over the on-time frames by the most ("dominant phase"), and the mean excess of
every phase over the slow frames is shown. The slot wait and the hand-over are the result of
lateness, not a cause, so they are not candidates. record_us includes gfx_blocked_us and the
compile times; the attribution uses record minus blocked minus compiles ("recording") so the
three are told apart.
"""
import csv
import statistics
import sys

PHASES = [
    ("main_cpu_us", "main thread (game code)"),
    ("waitidle_us", "wait for previous frame"),
    ("prep_us", "patch + copy"),
    ("record_us", "command processor to flip"),
    ("gfx_blocked_us", "  of which gfx blocked"),
    ("compile_us", "  of which compiles"),
    ("finish_us", "GPU submit + run"),
    ("copy_us", "readback copy"),
    ("slot_us", "slot wait"),
    ("present_us", "hand-over"),
    ("release_after_flip_us", "main released after flip"),
    ("period_us", "flip to flip"),
]
CAUSES = ["main_cpu_us", "waitidle_us", "prep_us", "recording_us", "gfx_blocked_us", "compile_us", "finish_us", "copy_us"]


def arg(name, default):
    if name in sys.argv:
        return float(sys.argv[sys.argv.index(name) + 1])
    return default


def pct(values, q):
    if not values:
        return float("nan")
    s = sorted(values)
    k = min(len(s) - 1, max(0, int(round(q * (len(s) - 1)))))
    return s[k]


# First-frame columns (perf/first-frame build; LBP3_FIRST_FRAME=1 or LBP3_FRAME_SPLIT=1): whole
# frame, first hand-over to the flip. (label, count column, us column, KB column); "" = none.
FF_GROUPS = [
    ("buffer sync (memcpy + copy)", "ff_bufsync_n", "ff_bufsync_us", "ff_bufsync_kb"),
    ("staging requests", "ff_staging_n", "ff_staging_us", ""),
    ("  new staging blocks", "ff_staging_new", "", "ff_staging_new_kb"),
    ("sparse residency allocs", "ff_resident_n", "ff_resident_us", "ff_resident_kb"),
    ("new arenas", "ff_arena_new", "ff_arena_us", ""),
    ("new images", "ff_img_new", "ff_img_new_us", ""),
    ("image allocs (VMA)", "ff_img_alloc_n", "ff_img_alloc_us", "ff_img_alloc_kb"),
    ("buffer allocs (VMA)", "ff_buf_alloc_n", "ff_buf_alloc_us", "ff_buf_alloc_kb"),
    ("VMA device memory", "ff_vma_mem_n", "", "ff_vma_mem_kb"),
    ("image refreshes (uploads)", "ff_img_refresh_n", "ff_img_refresh_us", "ff_img_refresh_kb"),
    ("detile recording", "ff_detile_n", "ff_detile_us", ""),
    ("image expansions", "ff_expand_n", "ff_expand_us", ""),
    ("resource binding (nests above)", "ff_draws", "ff_res_bind_us", ""),
    ("descriptor writes", "", "ff_desc_bind_us", ""),
    ("GPU: buffer copies", "ff_gpu_copy_n", "ff_gpu_copy_us", ""),
    ("GPU: image uploads", "ff_gpu_upload_n", "ff_gpu_upload_us", ""),
    ("GPU: detile dispatches", "ff_gpu_detile_n", "ff_gpu_detile_us", ""),
    # edf7a38 / 6bc4898f and later
    ("image frees (VMA)", "ff_img_free_n", "ff_img_free_us", ""),
    ("VMA device memory freed", "ff_vma_free_n", "", "ff_vma_free_kb"),
    ("GPU: image copies", "ff_gpu_imgcopy_n", "ff_gpu_imgcopy_us", ""),
    # df0e47b / c758aab and later
    ("memcpy guest -> staging", "ff_memcpy_n", "ff_memcpy_us", "ff_memcpy_kb"),
    ("  of which all-zero (FIRST_FRAME)", "", "", "ff_memcpy_zero_kb"),
    ("page faults (process)", "ff_page_faults", "", ""),
]


def ff_span_us(f):
    """The frame's recording: ff_record_us, or the whole window when the flip came early
    (ff_bufs_at_flip < flip_buffers: recording went on after the flip interrupt)."""
    rec = f["ff_record_us"]
    if "ff_window_us" in f and f["ff_bufs_at_flip"] < f["flip_buffers"]:
        return max(rec, f["ff_window_us"])
    return rec


def first_frame_report(frames, ff_ms):
    if "ff_record_us" not in frames[0]:
        return
    rec = [f["ff_record_us"] / 1000.0 for f in frames if f["ff_record_us"] >= 0]
    if rec:
        print(f"  whole-frame recording (first hand-over to flip): mean {statistics.fmean(rec):.2f}, "
              f"p95 {pct(rec, 0.95):.2f}, max {max(rec):.2f} ms; draws mean "
              f"{statistics.fmean(f['ff_draws'] for f in frames):.0f}, dispatches "
              f"{statistics.fmean(f['ff_dispatches'] for f in frames):.0f}; "
              f"GPU timestamp overflow {sum(f['ff_gpu_overflow'] for f in frames)}")
    big = [f for f in frames if ff_span_us(f) / 1000.0 > ff_ms]

    def cell(f, n, us, kb):
        parts = []
        if n:
            parts.append(f"{f[n]}x")
        if us:
            parts.append(f"{f[us] / 1000.0:.1f}ms")
        if kb:
            parts.append(f"{f[kb]}KB")
        return " ".join(parts)

    print(f"  first-frame breakdown: {len(big)} frames with whole-frame recording over {ff_ms:.0f} ms"
          f" (per-frame means over all {len(frames)} frames in brackets)")
    for label, n, us, kb in FF_GROUPS:
        if any(c and c not in frames[0] for c in (n, us, kb)):
            continue
        def mean(col):
            return statistics.fmean(f[col] for f in frames) if col else 0.0
        avg = []
        if n:
            avg.append(f"{mean(n):.1f}x")
        if us:
            avg.append(f"{mean(us) / 1000.0:.2f}ms")
        if kb:
            avg.append(f"{mean(kb):.0f}KB")
        print(f"    {label:32} [{' '.join(avg)}]  " + " | ".join(cell(f, n, us, kb) for f in big[:6]))
    for f in big[:6]:
        early = ""
        if "ff_window_us" in f:
            early = f", window {f['ff_window_us'] / 1000.0:.1f} ms"
            if f["ff_bufs_at_flip"] < f["flip_buffers"]:
                early += f", FLIP EARLY after {f['ff_bufs_at_flip']} of {f['flip_buffers']} buffers"
        print(f"    frame {f['frame']} at {f['t']:.1f} s: recording {f['ff_record_us'] / 1000.0:.1f} ms over "
              f"{f['flip_buffers']} buffers, GPU (finish) {f['finish_us'] / 1000.0:.1f} ms, period "
              f"{f['period_us'] / 1000.0:.1f} ms{early}")


def main():
    path = sys.argv[1]
    t_from, t_to, over_ms = arg("--from", None), arg("--to", None), arg("--over", 34.0)
    rows = list(csv.DictReader(open(path, newline="")))
    if not rows:
        sys.exit(f"{path}: no rows")
    t0 = float(rows[0]["submit_ms"])
    frames = []
    for r in rows:
        if None in r or None in r.values() or "" in r.values():
            continue  # a row cut short when the game was killed
        t = (float(r["submit_ms"]) - t0) / 1000.0
        if t_from is not None and t < t_from or t_to is not None and t > t_to:
            continue
        f = {k: int(v) for k, v in r.items() if k not in ("submit_ms",)}
        f["t"] = t
        f["compile_us"] = f["pipeline_us"] + f["shader_us"]
        rec, blk = f["record_us"], f["gfx_blocked_us"]
        f["recording_us"] = rec - max(blk, 0) - f["compile_us"] if rec >= 0 else -1
        frames.append(f)
    if not frames:
        sys.exit("no frames in that range")
    print(f"{path}: {len(frames)} frames, {frames[0]['t']:.1f}-{frames[-1]['t']:.1f} s")
    print(f"  {'phase':34} {'n':>6} {'mean':>7} {'p50':>7} {'p95':>7} {'max':>8}  (ms)")
    for key, label in PHASES:
        vals = [f[key] / 1000.0 for f in frames if f[key] >= 0 or key == "release_after_flip_us"]
        if key == "release_after_flip_us":
            vals = [f[key] / 1000.0 for f in frames if f["record_us"] >= 0]
        if not vals:
            continue
        print(f"  {label:34} {len(vals):6d} {statistics.fmean(vals):7.2f} {pct(vals, 0.5):7.2f} "
              f"{pct(vals, 0.95):7.2f} {max(vals):8.2f}")
    early = sum(1 for f in frames if f["release_after_flip_us"] < 0 and f["record_us"] >= 0)
    rel = {k: sum(1 for f in frames if f["submit_rel"] == k) for k in (0, 1, 2)}
    comp = sum(f["pipelines"] for f in frames), sum(f["shaders"] for f in frames)
    print(f"  released before the flip: {early} frames; submit wait returned idle {rel[0]}, "
          f"known address {rel[1]}, grace {rel[2]}; compiled {comp[0]} pipelines, {comp[1]} shaders")
    first_frame_report(frames, arg("--ff", 50.0))
    period = [f for f in frames if f["period_us"] >= 0]
    slow = [f for f in period if f["period_us"] / 1000.0 > over_ms]
    ok = [f for f in period if f["period_us"] / 1000.0 <= over_ms]
    print(f"  slow frames (period > {over_ms:.1f} ms): {len(slow)} of {len(period)} "
          f"({100.0 * len(slow) / max(1, len(period)):.1f}%)")
    if not slow or not ok:
        return
    med = {c: statistics.median([f[c] for f in ok if f[c] >= 0] or [0]) for c in CAUSES}
    dominant = {c: 0 for c in CAUSES}
    excess = {c: [] for c in CAUSES}
    for f in slow:
        ex = {c: (f[c] - med[c]) / 1000.0 for c in CAUSES if f[c] >= 0}
        for c, v in ex.items():
            excess[c].append(v)
        if ex:
            dominant[max(ex, key=ex.get)] += 1
    print(f"  slow frames by the phase that grew most over its on-time median (on-time medians in ms):")
    for c in sorted(CAUSES, key=lambda c: -dominant[c]):
        vals = excess[c]
        mean_ex = statistics.fmean(vals) if vals else float("nan")
        print(f"    {c:16} {dominant[c]:5d} frames   mean excess {mean_ex:+7.2f} ms   (on-time median {med[c] / 1000.0:6.2f})")


if __name__ == "__main__":
    main()
