# gas_verdict.py <game stderr>: issue #27's verdict from the last F9-traced frame in a game log.
# Finds the fluid effect's ray-march draw (ps f790c0f92f865e6d, #494 in the 1440 trace) and the
# composite (ps 5218aa8dc32bacd4, #500). The ray-march runs twice into a half-size target: the
# first pass (#494) draws the list of particle types 7 and 11, which this level doesn't have (it
# stays empty with the fix too); the second (#496, after #495 fills the target with a dim scene
# copy) draws the fire and the gas, counted as pixels changed from the draw before it.
#   exit 0 GREEN   the second pass drew (the fire and gas show)
#   exit 1 RED     it drew nothing (the bug)
#   exit 2 BROKEN  no trace, draws missing, or the composite's dump is dark (the dump failed)
#   exit 3 MOVED   the draws weren't dumped: prints the list for analysis\dump_draws.txt
import re
import sys

RM, COMP = 'ps=f790c0f92f865e6d', 'ps=5218aa8dc32bacd4'
lines = open(sys.argv[1], errors='replace').read().splitlines()
starts = [i for i, l in enumerate(lines) if l.startswith('[draw] ---- frame')]
if not starts:
    print('BROKEN: no traced frame in the log')
    sys.exit(2)
frame = lines[starts[-1]:]


def first(ps):
    for l in frame:
        m = re.match(r'\[draw\] #(\d+) ', l)
        if m and ps in l:
            return int(m.group(1))


def dumps(idx):  # {(w, h): (bmp path, average)} for render target 0 after draw idx
    out = {}
    for l in frame:
        m = re.search(r'(analysis\\targets\\draws\\draw%04d_rt0_\d+) (\d+)x(\d+) \S+ at (\w+), .*average ([\d.]+)/255'
                      % idx, l)
        if m:
            out[(int(m[2]), int(m[3]))] = ('%s_%sx%s_%s' % (m[1], m[2], m[3], m[4]), float(m[5]))
    return out


def nonzero(path):  # non-zero pixels of a 24-bit BMP as WriteBmp writes it
    data = open(path.replace('\\', '/'), 'rb').read()
    w, h = int.from_bytes(data[18:22], 'little'), int.from_bytes(data[22:26], 'little')
    row = (w * 3 + 3) & ~3
    return sum(1 for y in range(h) for x in range(w)
               if any(data[54 + y * row + x * 3: 54 + y * row + x * 3 + 3])), w * h


def passes(ps):  # the frame's draw numbers using that pixel shader, in order
    return [int(re.match(r'\[draw\] #(\d+) ', l)[1]) for l in frame if l.startswith('[draw] #') and ps in l]


def changed(a, b):  # pixels that differ between two same-size BMPs
    da, db = open(a.replace('\\', '/'), 'rb').read(), open(b.replace('\\', '/'), 'rb').read()
    w, h = int.from_bytes(da[18:22], 'little'), int.from_bytes(da[22:26], 'little')
    row = (w * 3 + 3) & ~3
    return sum(1 for y in range(h) for x in range(w)
               if da[54 + y * row + x * 3: 57 + y * row + x * 3] != db[54 + y * row + x * 3: 57 + y * row + x * 3])


rms, comp = passes(RM), first(COMP)
if len(rms) < 2 or comp is None:
    print('BROKEN: ray-march passes %s, composite #%s: not all in the traced frame' % (rms, comp))
    sys.exit(2)
rm, rm2 = rms[0], rms[1]
rmd, rm2d, befored, compd = dumps(rm), dumps(rm2), dumps(rm2 - 1), dumps(comp)
if not rmd or not rm2d or not befored or not compd:
    print('MOVED %d,%d' % (rm, comp))
    sys.exit(3)
(cw, ch), (cpath, cavg) = max(compd.items())
if cavg < 5:
    print('BROKEN: the composite #%d %dx%d averages %.1f/255 (the dump itself failed?)' % (comp, cw, ch, cavg))
    sys.exit(2)
size = (cw // 2, ch // 2)
if size not in rmd or size not in rm2d or size not in befored:
    print('BROKEN: no %dx%d target dumped around the ray-march #%d/#%d' % (size + (rm, rm2)))
    sys.exit(2)
col, n = nonzero(rmd[size][0] + '.bmp')
alp, _ = nonzero(rmd[size][0] + '_alpha.bmp')
fire = changed(befored[size][0] + '.bmp', rm2d[size][0] + '.bmp')
verdict = 'GREEN' if fire > 0 else 'RED'
print('%s: fire/gas pass #%d changed %d; first pass #%d (types 7/11, none here) non-zero %d/%d of %d; composite #%d average %.1f/255'
      % (verdict, rm2, fire, rm, col, alp, n, comp, cavg))
sys.exit(0 if verdict == 'GREEN' else 1)
