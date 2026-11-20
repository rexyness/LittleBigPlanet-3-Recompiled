"""dlss_compare.py <fg_dump dir> <out dir> <set> [<set>...]: for each capture set of a DLSS run,
the same frame's HUD-less image (the game's own SMAA'd picture: DLSS's input) against its flipped
image (DLSS's picture with the screen HUD put back). Writes full PNGs and a sheet of 3x crops
(left: game, right: DLSS) from the most detailed regions, and prints difference statistics."""
import json, os, sys
import numpy as np
from PIL import Image

root, out = sys.argv[1], sys.argv[2]
os.makedirs(out, exist_ok=True)
CROP, ZOOM = 160, 3


def load(d, k, s, m):
    c = m['frames'][k][s]
    if not c['ok']:
        return None
    raw = np.fromfile(os.path.join(d, f'f{k:03d}_{s}.raw'), dtype=np.uint8)
    return raw.reshape(c['h'], c['w'], 4)[..., :3]


for name in sys.argv[3:]:
    d = os.path.join(root, name)
    m = json.load(open(os.path.join(d, 'manifest.json')))
    k = len(m['frames']) - 1
    hud, fin = load(d, k, 'hudless', m), load(d, k, 'final', m)
    if hud is None or fin is None or hud.shape != fin.shape:
        print(name, 'missing or mismatched images')
        continue
    Image.fromarray(hud).save(os.path.join(out, f'{name}_game.png'))
    Image.fromarray(fin).save(os.path.join(out, f'{name}_dlss.png'))
    diff = np.abs(hud.astype(int) - fin.astype(int)).max(axis=2)
    h, w = diff.shape
    print(f'{name}: {w}x{h}, mean |diff| {diff.mean():.2f}, pixels differing >8: {100 * (diff > 8).mean():.1f}%, '
          f'>32: {100 * (diff > 32).mean():.1f}%')
    # The most detailed regions of the game's picture (gradient energy), away from each other.
    g = hud.astype(float).mean(axis=2)
    e = np.abs(np.diff(g, axis=0))[:, :-1] + np.abs(np.diff(g, axis=1))[:-1, :]
    picks = []
    bs = CROP
    scores = []
    for y in range(0, h - bs, bs // 2):
        for x in range(0, w - bs, bs // 2):
            scores.append((e[y:y + bs, x:x + bs].mean(), x, y))
    scores.sort(reverse=True)
    for s, x, y in scores:
        if all(abs(x - px) >= bs or abs(y - py) >= bs for _, px, py in picks):
            picks.append((s, x, y))
        if len(picks) == 3:
            break
    picks.append((0, w // 2 - bs // 2, h // 2 - bs // 2))  # and the centre
    tiles = []
    for _, x, y in picks:
        a = Image.fromarray(hud[y:y + bs, x:x + bs]).resize((bs * ZOOM, bs * ZOOM), Image.NEAREST)
        b = Image.fromarray(fin[y:y + bs, x:x + bs]).resize((bs * ZOOM, bs * ZOOM), Image.NEAREST)
        t = Image.new('RGB', (bs * ZOOM * 2 + 8, bs * ZOOM), (255, 0, 255))
        t.paste(a, (0, 0))
        t.paste(b, (bs * ZOOM + 8, 0))
        tiles.append(t)
    sheet = Image.new('RGB', (tiles[0].width, sum(t.height + 8 for t in tiles)), (255, 0, 255))
    yy = 0
    for t in tiles:
        sheet.paste(t, (0, yy))
        yy += t.height + 8
    sheet.save(os.path.join(out, f'{name}_crops.png'))
    print('  crops at', [(x, y) for _, x, y in picks])
