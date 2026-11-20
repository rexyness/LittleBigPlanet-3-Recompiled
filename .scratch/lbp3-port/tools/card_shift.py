"""Where the Home screen's "Recently Edited" card sits in each shot, relative to a reference.
  card_shift.py [--range N] <screenshots dir> <ref shot> <shot> [<shot> ...]
For three crops (the card, the Play/Create menu, the bottom buttons) it finds the integer (dx, dy)
in -N..N (default 40) that best aligns the shot to the reference (least mean |diff| over the crop),
and that residual. A moved card shows a non-zero shift on the card crop only.
Only compare shots of the same kind (same suite and moment): a residual far above the reference's
own noise means different content, and the shift is then meaningless. A shift of exactly +-N means
the search hit its edge."""
import sys

import numpy as np
from PIL import Image

args = sys.argv[1:]
R = 40
if args[0] == "--range":
    R, args = int(args[1]), args[2:]
D, ref, shots = args[0], args[1], args[2:]
# 1280x720 shots: the card with its "Edit Now" tag, the Play/Create/Community menu, the bottom buttons
CROPS = {"card": (700, 170, 1080, 470), "menu": (180, 190, 400, 480), "bottom": (50, 612, 420, 656)}


def load(name):
    return np.asarray(Image.open(f"{D}/{name}.png").convert("L"), dtype=np.float32)


def best_shift(a, b, box):
    x0, y0, x1, y1 = box
    ra = a[y0:y1, x0:x1]
    best = (1e9, 0, 0)
    for dy in range(-R, R + 1):
        for dx in range(-R, R + 1):
            rb = b[y0 + dy:y1 + dy, x0 + dx:x1 + dx]
            if rb.shape != ra.shape:
                continue
            m = float(np.abs(ra - rb).mean())
            if m < best[0]:
                best = (m, dx, dy)
    return best


A = load(ref)
print(f"reference {ref}")
for s in shots:
    B = load(s)
    parts = []
    for name, box in CROPS.items():
        m, dx, dy = best_shift(A, B, box)
        parts.append(f"{name} ({dx:+d},{dy:+d}) res {m:5.2f}")
    print(f"  {s:34s} " + "  ".join(parts))
