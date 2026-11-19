"""photo_check.py <savedata dir> [out prefix]: the Popit photo check for t_m2v_photo.sh.

Pulls every JFIF out of the save's LBPxSAVE/*/bigfart* files, decodes it (Pillow) and prints its
size and mean/max RGB. PASS when one of them is a photo of the scene: at least 10000 bytes (the
black frame of the readback bug compressed to 4291) and a mean brightness over 40. Exit 1 on FAIL.
With an out prefix each JPEG is also written to <prefix>_<n>.jpg.
"""
import glob
import io
import os
import sys

from PIL import Image, ImageStat

save_dir = sys.argv[1]
prefix = sys.argv[2] if len(sys.argv) > 2 else None
found = []
for path in sorted(glob.glob(os.path.join(save_dir, "LBPxSAVE", "*", "bigfart*"))):
    data = open(path, "rb").read()
    start = 0
    while (start := data.find(b"\xff\xd8\xff\xe0", start)) >= 0:
        end = data.find(b"\xff\xd9", start)
        if end < 0:
            break
        jpeg = data[start:end + 2]
        try:
            im = Image.open(io.BytesIO(jpeg)).convert("RGB")
        except Exception as e:  # a marker pair inside other data
            print(f"{os.path.relpath(path, save_dir)} @{start}: not a JPEG ({e})")
            start = end
            continue
        mean = tuple(round(v, 1) for v in ImageStat.Stat(im).mean)
        peak = tuple(hi for _, hi in im.getextrema())
        line = f"{os.path.relpath(path, save_dir)} @{start}: {len(jpeg)} bytes {im.size[0]}x{im.size[1]} mean {mean} max {peak}"
        if prefix:
            out = f"{prefix}_{len(found)}.jpg"
            open(out, "wb").write(jpeg)
            line += f" -> {out}"
        print(line)
        found.append((len(jpeg), sum(mean) / 3))
        start = end
ok = any(size >= 10000 and bright > 40 for size, bright in found)
print("photo check:", "PASS" if ok else "FAIL", f"({len(found)} JPEG(s) in the save)")
sys.exit(0 if ok else 1)
