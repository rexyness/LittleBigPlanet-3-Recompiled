"""dlss_temporal.py <fg_dump dir> <out dir> <tag>: stage 2's comparison of dlss_exp.sh's sets.
Per set: frame-to-frame change of DLSS's picture (the flipped image) and of the game's own (the
HUD-less image), and the sharpness of each (mean luma gradient). Standing still, a steadier and
sharper DLSS picture is better; walking, the right vectors keep DLSS's picture as sharp as the
game's without trails. Also writes sheets of 3x crops: the standing sets side by side (the same
regions), and each walking set's last frame (game left, DLSS right)."""
import glob, json, os, sys
import numpy as np
from PIL import Image

root, out, tag = sys.argv[1], sys.argv[2], sys.argv[3]
os.makedirs(out, exist_ok=True)
CROP, ZOOM = 128, 3


def frames(d, slot):
    m = json.load(open(os.path.join(d, 'manifest.json')))
    imgs = []
    for k, f in enumerate(m['frames']):
        c = f[slot]
        if not c['ok']:
            continue
        raw = np.fromfile(os.path.join(d, f'f{k:03d}_{slot}.raw'), dtype=np.uint8)
        imgs.append(raw.reshape(c['h'], c['w'], 4)[..., :3])
    return imgs


def luma(a):
    return a[..., 0] * 0.299 + a[..., 1] * 0.587 + a[..., 2] * 0.114


def sharp(a):
    y = luma(a.astype(np.float32))
    return float(np.abs(np.diff(y, axis=0)).mean() + np.abs(np.diff(y, axis=1)).mean())


def change(seq):
    if len(seq) < 2:
        return float('nan')
    return float(np.mean([np.abs(seq[i].astype(np.int16) - seq[i - 1].astype(np.int16)).mean()
                          for i in range(1, len(seq))]))


sets = sorted(glob.glob(os.path.join(root, f'dlssx_{tag}_*')))
order = ['base', 'nosmaa', 'pp', 'mm', 'pm', 'mp', 'pod_base', 'pod_pp', 'pod_mm', 'pod_pm', 'pod_mp',
         'basket_full', 'walk_normal', 'walk_zero', 'walk_flipx', 'walk_flipy', 'walk_flipxy', 'walk_full']
sets.sort(key=lambda p: order.index(p.split(f'dlssx_{tag}_')[1]) if p.split(f'dlssx_{tag}_')[1] in order else 99)
print(f'{"set":<14}{"frames":>7}{"DLSS change":>13}{"game change":>13}{"DLSS sharp":>12}{"game sharp":>12}')
data = {}
for d in sets:
    name = d.split(f'dlssx_{tag}_')[1]
    fin, hud = frames(d, 'final'), frames(d, 'hudless')
    data[name] = (fin, hud)
    print(f'{name:<14}{len(fin):>7}{change(fin):>13.3f}{change(hud):>13.3f}'
          f'{np.mean([sharp(a) for a in fin]):>12.3f}{np.mean([sharp(a) for a in hud]):>12.3f}')

# Standing: the same regions across sets (most detailed in the first set's last game frame, spread
# over the frame), one sheet per spot.
def standing_sheet(standing, sheet_name):
    if not standing:
        return
    ref = data[standing[0]][1][-1]
    h, w, _ = ref.shape
    e = np.abs(np.diff(luma(ref.astype(np.float32)), axis=1))[:-1, :]
    cand = sorted(((e[y:y + CROP, x:x + CROP].mean(), x, y) for y in range(0, h - CROP, CROP // 2)
                   for x in range(0, w - CROP, CROP // 2)), reverse=True)
    picks = []
    for s, x, y in cand:
        if all(abs(x - px) >= 3 * CROP or abs(y - py) >= 3 * CROP for px, py in picks):
            picks.append((x, y))
        if len(picks) == 3:
            break
    cols = [('game', data[standing[0]][1][-1])] + [(n, data[n][0][-1]) for n in standing]
    tw = CROP * ZOOM
    sheet = Image.new('RGB', (len(cols) * (tw + 6), len(picks) * (tw + 6)), (255, 0, 255))
    for r, (x, y) in enumerate(picks):
        for c, (n, img) in enumerate(cols):
            tile = Image.fromarray(img[y:y + CROP, x:x + CROP]).resize((tw, tw), Image.NEAREST)
            sheet.paste(tile, (c * (tw + 6), r * (tw + 6)))
    sheet.save(os.path.join(out, f'dlssx_{tag}_{sheet_name}.png'))
    print(f'{sheet_name} sheet columns:', ', '.join(n for n, _ in cols), '; regions', picks)


standing_sheet([n for n in order[:6] if n in data and data[n][0]], 'standing')
standing_sheet([n for n in order[6:11] if n in data and data[n][0]], 'pod')

for n in order[12:]:
    if n not in data or not data[n][0]:
        continue
    fin, hud = data[n][0][-1], data[n][1][-1]
    h, w, _ = fin.shape
    x, y = w // 2 - CROP, h // 2 - CROP // 2
    a = Image.fromarray(hud[y:y + CROP, x:x + 2 * CROP]).resize((2 * CROP * ZOOM, CROP * ZOOM), Image.NEAREST)
    b = Image.fromarray(fin[y:y + CROP, x:x + 2 * CROP]).resize((2 * CROP * ZOOM, CROP * ZOOM), Image.NEAREST)
    s = Image.new('RGB', (a.width, a.height * 2 + 6), (255, 0, 255))
    s.paste(a, (0, 0))
    s.paste(b, (0, a.height + 6))
    s.save(os.path.join(out, f'dlssx_{tag}_{n}.png'))
