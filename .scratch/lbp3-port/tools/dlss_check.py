"""dlss_check.py <fg_dump dir> <tag> [<tag>...]: direct checks of DLSS's inputs on dlss_exp*.sh's sets.

1. Vectors (walk_* sets, jitter off): warp the game's frame N-1 onto frame N with frame N's decoded
   vectors, prev(p) = p + s * m(p), m = ((B - 127) * 0.941 * W/1920, (G - 127) * 0.529 * H/1080)
   where R = 255, for the sign pairs s. DLSS is given -m (current -> previous), i.e. s = (-1, -1);
   the right pair has the smallest error against frame N.
2. Jitter (standing sets): the sub-pixel shift between consecutive frames (phase correlation on a
   static crop), for the game's picture (moves by the jitter) and DLSS's (should not move)."""
import glob, json, os, sys
import numpy as np


def load(d):
    m = json.load(open(os.path.join(d, 'manifest.json')))
    out = []
    for k, f in enumerate(m['frames']):
        rec = {}
        for s in ('hudless', 'final', 'velocity'):
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


def vectors(name, recs):
    errs = {}
    pairs = [((1, 1), '++'), ((-1, -1), '-- (what DLSS gets)'), ((1, -1), '+-'), ((-1, 1), '-+'), ((0, 0), 'zero')]
    for k in range(1, len(recs)):
        a, b = recs[k - 1].get('hudless'), recs[k].get('hudless')
        v = recs[k].get('velocity')
        if a is None or b is None or v is None or v.shape[:2] != b.shape[:2]:
            continue
        H, W = b.shape[:2]
        prev, cur = luma(a), luma(b)
        cov = v[..., 0] == 255
        mx = (v[..., 2].astype(np.float32) - 127) * 0.941 * W / 1920
        my = (v[..., 1].astype(np.float32) - 127) * 0.529 * H / 1080
        moving = cov & ((np.abs(mx) > 1) | (np.abs(my) > 1))
        border = np.zeros_like(cov)
        border[64:-64, 64:-64] = True
        mask = moving & border
        if mask.sum() < 1000:
            continue
        ys, xs = np.nonzero(mask)
        for (sx, sy), label in pairs:
            w = bilinear(prev, xs + sx * mx[ys, xs], ys + sy * my[ys, xs])
            errs.setdefault(label, []).append(float(np.abs(w - cur[ys, xs]).mean()))
    if not errs:
        print(f'  {name}: no moving covered pixels')
        return
    n = len(next(iter(errs.values())))
    print(f'  {name}: reprojection error over {n} frame pairs (lower = the right signs):')
    for label, e in sorted(errs.items(), key=lambda kv: np.mean(kv[1])):
        print(f'      {label:<22} {np.mean(e):8.3f}')


def shift(a, b):
    """Sub-pixel translation of b against a (phase correlation, parabolic peak)."""
    A, B = np.fft.fft2(a - a.mean()), np.fft.fft2(b - b.mean())
    r = A * np.conj(B)
    r /= np.abs(r) + 1e-9
    c = np.real(np.fft.ifft2(r))
    py, px = np.unravel_index(np.argmax(c), c.shape)
    h, w = c.shape

    def sub(cm, c0, cp):
        d = cm - 2 * c0 + cp
        return 0.0 if abs(d) < 1e-12 else 0.5 * (cm - cp) / d

    dx = px + sub(c[py, (px - 1) % w], c[py, px], c[py, (px + 1) % w])
    dy = py + sub(c[(py - 1) % h, px], c[py, px], c[(py + 1) % h, px])
    if dx > w / 2: dx -= w
    if dy > h / 2: dy -= h
    return -dx, -dy


def jitter(name, recs, crop):
    x0, y0, s = crop
    for slot in ('hudless', 'final'):
        sh = []
        for k in range(1, len(recs)):
            a, b = recs[k - 1].get(slot), recs[k].get(slot)
            if a is None or b is None:
                continue
            la, lb = luma(a)[y0:y0 + s, x0:x0 + s], luma(b)[y0:y0 + s, x0:x0 + s]
            sh.append(shift(la, lb))
        sh = np.array(sh)
        rms = np.sqrt((sh ** 2).mean(axis=0)) if len(sh) else (np.nan, np.nan)
        print(f'  {name:<14} {"game" if slot == "hudless" else "DLSS"}: frame-to-frame shift rms x {rms[0]:.3f} px, '
              f'y {rms[1]:.3f} px')


root = sys.argv[1]
for tag in sys.argv[2:]:
    for d in sorted(glob.glob(os.path.join(root, f'dlssx_{tag}_*'))):
        name = d.split(f'dlssx_{tag}_')[1]
        recs = load(d)
        if name.startswith('walk_'):
            if name != 'walk_full':
                vectors(name, recs)
        else:
            # a static 512x512 crop: the basket's fabric (bottom middle), or the Pod's centre-left
            crop = (1150, 860, 512) if not name.startswith('pod') else (700, 500, 512)
            jitter(name, recs, crop)
