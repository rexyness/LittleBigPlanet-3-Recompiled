"""Print the left edge of the Popit bag page (the tall blue panel) as a fraction of the width.

For each row in the middle band, find where the first run of at least 12 panel-blue pixels starts
in the right half of the frame; the page's left edge is the median over the rows that have one.
"""
import sys

import numpy as np
from PIL import Image

MIN_RUN = 12


def is_panel(rgb):
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    return (b > 170) & (b - r > 90) & (g > 90) & (g < b)


for path in sys.argv[1:]:
    a = np.asarray(Image.open(path).convert("RGB"), dtype=np.int32)
    h, w, _ = a.shape
    starts = []
    for y in range(int(h * 0.2), int(h * 0.8)):
        row = is_panel(a[y, w // 2:])
        run = 0
        for x, v in enumerate(row):
            run = run + 1 if v else 0
            if run == MIN_RUN:
                starts.append(w // 2 + x - MIN_RUN + 1)
                break
    if len(starts) < (h * 0.6) * 0.3:
        print(f"{path}: no panel found ({len(starts)} rows)")
        continue
    left = int(np.median(starts))
    print(f"{path}: panel left edge x {left} of {w} = {100 * left / w:.1f}% ({len(starts)} rows)")
