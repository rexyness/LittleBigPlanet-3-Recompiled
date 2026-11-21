"""How much does the vectors' 8-bit rounding cost? On dlss_exp2's walking captures (jitter off), the
reprojection error of frame N-1 warped onto N with the decoded vectors (as DLSS gets them), against the
same with a second, independent rounding error added (uniform +-0.5 code per axis). The 8-bit codes already
carry a +-0.5-code rounding error; if adding another one barely moves the error, removing the first (16-bit
vectors) won't move it much either."""
import glob, json, os, sys
import numpy as np

root = sys.argv[1]
rng = np.random.default_rng(1)


def load(d):
    m = json.load(open(os.path.join(d, 'manifest.json')))
    out = []
    for k, f in enumerate(m['frames']):
        rec = {}
        for s in ('hudless', 'velocity'):
            c = f[s]
            if c['ok']:
                raw = np.fromfile(os.path.join(d, f'f{k:03d}_{s}.raw'), dtype=np.uint8)
                rec[s] = raw.reshape(c['h'], c['w'], 4)
        out.append(rec)
    return out


def luma(a):
    a = a[..., :3].astype(np.float32)
    return a[..., 0] * 0.299 + a[..., 1] * 0.587 + a[..., 2] * 0.114


def bilinear(img, x, y):
    h, w = img.shape
    x = np.clip(x, 0, w - 1.001)
    y = np.clip(y, 0, h - 1.001)
    x0, y0 = np.floor(x).astype(int), np.floor(y).astype(int)
    fx, fy = x - x0, y - y0
    return (img[y0, x0] * (1 - fx) * (1 - fy) + img[y0, x0 + 1] * fx * (1 - fy) +
            img[y0 + 1, x0] * (1 - fx) * fy + img[y0 + 1, x0 + 1] * fx * fy)


for d in sorted(glob.glob(os.path.join(root, 'dlssx_e2_walk_*'))):
    recs = load(d)
    base, noisy, zero = [], [], []
    for k in range(1, len(recs)):
        a, b, v = recs[k - 1].get('hudless'), recs[k].get('hudless'), recs[k].get('velocity')
        if a is None or b is None or v is None:
            continue
        H, W = b.shape[:2]
        prev, cur = luma(a), luma(b)
        cov = v[..., 0] == 255
        cx = v[..., 2].astype(np.float32) - 127
        cy = v[..., 1].astype(np.float32) - 127
        sx, sy = 0.941 * W / 1920, 0.529 * H / 1080
        moving = cov & ((np.abs(cx) > 1) | (np.abs(cy) > 1))
        mask = np.zeros_like(cov)
        mask[64:-64, 64:-64] = True
        mask &= moving
        ys, xs = np.nonzero(mask)
        if len(xs) < 1000:
            continue
        mx, my = cx[ys, xs] * sx, cy[ys, xs] * sy
        base.append(float(np.abs(bilinear(prev, xs - mx, ys - my) - cur[ys, xs]).mean()))
        nx = (cx[ys, xs] + rng.uniform(-0.5, 0.5, len(xs))) * sx
        ny = (cy[ys, xs] + rng.uniform(-0.5, 0.5, len(xs))) * sy
        noisy.append(float(np.abs(bilinear(prev, xs - nx, ys - ny) - cur[ys, xs]).mean()))
        zero.append(float(np.abs(prev[ys, xs] - cur[ys, xs]).mean()))
    if base:
        print(f'{os.path.basename(d)}: {len(base)} pairs; error as decoded {np.mean(base):.3f}, '
              f'with a second +-0.5-code rounding {np.mean(noisy):.3f} ({100 * (np.mean(noisy) / np.mean(base) - 1):+.1f}%), '
              f'zero vectors {np.mean(zero):.3f}')
