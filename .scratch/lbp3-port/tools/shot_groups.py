"""Two builds' screenshots against their own run-to-run noise.

  shot_groups.py <screenshots dir> --a seth1,seth2 --b sb2,sb2w [--cells] <template> [<template> ...]

A template names one shot with {run} where the run tag goes, e.g. "ab_{run}_1_level" or
"{run}_bunkum_end" (".png" is added). Runs whose file is missing are skipped.

Per shot it prints, for every run: mean luma, luma p50 / p90, and the share of pixels with any
channel >= 250 (clipping, ticket 17). Then the mean absolute pixel difference (0-255, RGB average)
and the worst 32x32 block's mean difference for the pairs inside A, inside B, and across A and B.
A change shows up as A-B values above both groups' own ranges; a moved object (animation phase)
shows up in the worst block only. --cells adds mean luma for a 4x3 grid inside the letterbox.

Written for the sethlikepie Batch 1 / Batch 2 comparison (perf/seth_ports.md). shot_compare.py
(lbp3-wt/_tools) needs <prefix>_old<i>_<shot> names, which the regression's shots don't have.
"""
import argparse
import itertools
import os

from PIL import Image, ImageChops, ImageStat


def luma_stats(img):
    L = img.convert("L")
    hist = L.histogram()
    n = sum(hist)

    def pct(p):
        c = 0
        for v, k in enumerate(hist):
            c += k
            if c >= p * n:
                return v
        return 255

    hot = [c.point(lambda v: 255 if v >= 250 else 0) for c in img.split()]
    anyc = ImageChops.lighter(ImageChops.lighter(hot[0], hot[1]), hot[2])
    clip = anyc.histogram()[255] / n * 100
    return ImageStat.Stat(L).mean[0], pct(0.5), pct(0.9), clip


def cells(img):
    w, h = img.size
    L = img.convert("L")
    out = []
    for gy in range(3):
        for gx in range(4):
            box = (gx * w // 4, 10 + gy * (h - 20) // 3, (gx + 1) * w // 4, 10 + (gy + 1) * (h - 20) // 3)
            out.append(ImageStat.Stat(L.crop(box)).mean[0])
    return out


def diff(a, b):
    d = ImageChops.difference(a, b)
    full = sum(ImageStat.Stat(d).mean) / 3
    g = d.convert("L")
    blk = g.resize((g.size[0] // 32, g.size[1] // 32), Image.BOX)
    return full, blk.getextrema()[1]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dir")
    ap.add_argument("--a", required=True)
    ap.add_argument("--b", required=True)
    ap.add_argument("--cells", action="store_true")
    ap.add_argument("templates", nargs="+")
    args = ap.parse_args()
    A, B = args.a.split(","), args.b.split(",")

    for t in args.templates:
        imgs = {}
        bad = []
        for r in A + B:
            path = os.path.join(args.dir, t.format(run=r) + ".png")
            if os.path.exists(path):
                im = Image.open(path).convert("RGB")
                if im.size[0] < 64 or im.size[1] < 64 or (imgs and im.size != next(iter(imgs.values())).size):
                    bad.append(f"{r} {im.size[0]}x{im.size[1]}")
                    continue
                imgs[r] = im
        a = [r for r in A if r in imgs]
        b = [r for r in B if r in imgs]
        print(f"\n=== {t}  (A: {' '.join(a) or '-'} | B: {' '.join(b) or '-'})"
              + (f"  skipped, wrong size: {', '.join(bad)}" if bad else ""))
        for r in a + b:
            m, p50, p90, clip = luma_stats(imgs[r])
            line = f"  {'A' if r in a else 'B'} {r:8s} luma {m:6.2f}  p50 {p50:3d}  p90 {p90:3d}  clip {clip:5.2f}%"
            if args.cells:
                line += "  | " + " ".join(f"{c:5.1f}" for c in cells(imgs[r]))
            print(line)
        for name, pairs in (("A-A", list(itertools.combinations(a, 2))),
                            ("B-B", list(itertools.combinations(b, 2))),
                            ("A-B", [(x, y) for x in a for y in b])):
            if not pairs:
                continue
            res = [diff(imgs[x], imgs[y]) for x, y in pairs]
            fm, bm = [r[0] for r in res], [r[1] for r in res]
            print(f"  {name}: mean|diff| {min(fm):5.2f}-{max(fm):5.2f}  worst block {min(bm):3d}-{max(bm):3d}  ({len(res)} pairs)")


if __name__ == "__main__":
    main()
