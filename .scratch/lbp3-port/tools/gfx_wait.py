"""Reads analysis/gfx_wait_trace.csv (LBP3_GFX_WAIT_TRACE=1, shadPS4 lbp3_gfx_wait_trace.h on the
perf/gfx-wait branch) and answers perf/gfx_wait.md's questions:

  - which addresses the graphics queue blocks on, with which packet, where in the frame;
  - who satisfies them (a command stream, or a guest CPU thread: "cpu") and when (the watcher);
  - what the command-processor thread did meanwhile (graphics / compute resumes, commands);
  - the compute queue's state when graphics blocked, and its waits and submits during the block;
  - learn / forget events.

  python gfx_wait.py <trace.csv> [--split frame_split.csv] [--timeline N] [--min-us X]
      --timeline N   print the events of the N-th graphics wait's frame (0-based, among waits
                     >= --min-us), from the previous flip to the wait's end
"""
import argparse
import bisect
import collections
import csv
import statistics


def q(xs, p):
    if not xs:
        return float('nan')
    xs = sorted(xs)
    return xs[min(len(xs) - 1, int(p * (len(xs) - 1) + 0.5))]


def med(xs):
    return statistics.median(xs) if xs else float('nan')


def h(x):
    return int(x, 0)


def load(path):
    rows = []
    with open(path, newline='') as f:
        for line in f:
            if not line.strip() or line.startswith('#'):
                continue
            rows.append(next(csv.reader([line.rstrip('\n')])))
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('trace')
    ap.add_argument('--split')
    ap.add_argument('--timeline', type=int, default=-1)
    ap.add_argument('--min-us', type=float, default=0.0)
    a = ap.parse_args()
    rows = load(a.trace)

    kinds = collections.Counter(r[0] for r in rows)
    print('rows:', dict(kinds))

    flips = [(int(r[1]), int(r[2])) for r in rows if r[0] == 'F']
    flip_t = [t for t, _ in flips]

    # Pair GB/GE (graphics) and CB/CE (compute) in order.
    gw, cw = [], []
    open_g = open_c = None
    for i, r in enumerate(rows):
        k = r[0]
        if k == 'GB':
            open_g = (i, r)
        elif k == 'GE' and open_g:
            b = open_g[1]
            gw.append(dict(
                i0=open_g[0], i1=i, t0=int(b[1]), t1=int(r[1]), flip=int(b[2]), kind=b[3],
                addr=h(b[4]), v0=h(b[5]), ref=h(b[6]), mask=h(b[7]), func=int(b[8]), buf=int(b[9]),
                off_end=int(b[10]), size=int(b[11]), top=int(b[12]), c_state=int(b[13]),
                c_addr=h(b[14]), c_val=h(b[15]), c_ref=h(b[16]), c_func=int(b[17]), c_sat=int(b[18]),
                c_pending=int(b[19]), known=int(b[20]), v1=h(r[4]), dur=float(r[5]), polls=int(r[6]),
                max_gap=float(r[7]), gres=float(r[8]), cres=float(r[9]), cres_n=int(r[10]),
                cadv=float(r[11]), cpkts=int(r[12]), cdisp=int(r[13]), pc=float(r[14]), sat=int(r[15]),
                first=int(r[16]), writer=r[17], writer_t=int(r[18]), c_state1=int(r[19]),
                c_addr1=h(r[20]),
                # write tracking during the wait (traces from 2026-11-12 08:30 on)
                fault_n=int(r[21]) if len(r) > 25 else -1, fault_us=float(r[22]) if len(r) > 25 else -1,
                fault_cp_n=int(r[23]) if len(r) > 25 else -1, prot_cp_n=int(r[24]) if len(r) > 25 else -1,
                prot_cp_us=float(r[25]) if len(r) > 25 else -1))
            open_g = None
        elif k == 'CB':
            open_c = (i, r)
        elif k == 'CE' and open_c:
            b = open_c[1]
            cw.append(dict(
                i0=open_c[0], i1=i, t0=int(b[1]), t1=int(r[1]), flip=int(b[2]), kind=b[3],
                addr=h(b[4]), v0=h(b[5]), ref=h(b[6]), mask=h(b[7]), func=int(b[8]), known=int(b[9]),
                g_blocked=int(b[10]), v1=h(r[4]), dur=float(r[5]), polls=int(r[6]),
                max_gap=float(r[7]), gres=float(r[8]), pc=float(r[9]), sat=int(r[10]),
                first=int(r[11]), writer=r[12], writer_t=int(r[13])))
            open_c = None

    print(f'graphics waits: {len(gw)}; compute waits: {len(cw)}; flips: {len(flips)}')
    gws = [w for w in gw if w['dur'] >= a.min_us]

    # 1. Graphics waits by address.
    print('\n== graphics waits by address (dur in us) ==')
    by = collections.defaultdict(list)
    for w in gws:
        by[(w['kind'], w['addr'])].append(w)
    for (kind, addr), ws in sorted(by.items(), key=lambda kv: -sum(w['dur'] for w in kv[1])):
        d = [w['dur'] for w in ws]
        refs = collections.Counter((w['ref'], w['mask'], w['func']) for w in ws).most_common(3)
        place = collections.Counter((w['buf'], w['off_end'], w['size'], w['top']) for w in ws).most_common(3)
        writers = collections.Counter(w['writer'] for w in ws).most_common(4)
        vals = collections.Counter((w['v0'], w['v1']) for w in ws).most_common(3)
        print(f'{kind} {addr:#x}: n={len(ws)} total={sum(d) / 1000:.1f} ms med={med(d):.0f} '
              f'p95={q(d, .95):.0f} max={max(d):.0f}')
        print(f'   ref/mask/func {[(hex(r), hex(m), f) for (r, m, f), _ in refs]}  '
              f'value begin->end {[(hex(x), hex(y), n) for (x, y), n in vals]}')
        print(f'   place (buf, dwords to end, size, top) {place}')
        print(f'   writer {writers}  known at begin: {sum(w["known"] for w in ws)}')

    # 2. Timing split: until the guest satisfied it (watcher) and the detection lag.
    print('\n== when it was satisfied (watcher) ==')
    sat = [w for w in gws if w['sat'] >= 0]
    early = [w for w in sat if w['sat'] == w['first']]
    good = [w for w in sat if w['sat'] != w['first']]
    print(f'satisfied seen: {len(sat)} of {len(gws)}; already true at the watcher\'s first look: '
          f'{len(early)} (first look lag med {med([w["first"] - w["t0"] for w in sat]):.0f} us)')
    if good:
        until = [w['sat'] - w['t0'] for w in good]
        lag = [w['t1'] - w['sat'] for w in good]
        print(f'begin -> guest satisfies: med {med(until):.0f} p95 {q(until, .95):.0f} max {max(until):.0f} us')
        print(f'satisfied -> processor notices: med {med(lag):.0f} p95 {q(lag, .95):.0f} '
              f'max {max(lag):.0f} us')

    # 3. What the processor thread did during the graphics waits.
    print('\n== the processor thread during graphics waits (sums over waits, ms) ==')
    tot = sum(w['dur'] for w in gws)
    for key, label in (('gres', 'graphics resumes (polls)'), ('cres', 'compute resumes, all'),
                       ('cadv', '  of which ran packets'), ('pc', 'ProcessCommands')):
        s = sum(w[key] for w in gws)
        print(f'{label:28s} {s / 1000:8.1f}  ({100 * s / tot:.1f}% of blocked {tot / 1000:.1f} ms)')
    rest = tot - sum(w['gres'] + w['cres'] + w['pc'] for w in gws)
    print(f'{"rest (loop, locks, trace)":28s} {rest / 1000:8.1f}  ({100 * rest / tot:.1f}%)')
    print(f'per wait: polls med {med([w["polls"] for w in gws]):.0f}, max gap med '
          f'{med([w["max_gap"] for w in gws]):.0f} p95 {q([w["max_gap"] for w in gws], .95):.0f} us; '
          f'compute packets med {med([w["cpkts"] for w in gws]):.0f}, dispatches med '
          f'{med([w["cdisp"] for w in gws]):.0f}')

    if gws and gws[0]['fault_n'] >= 0:
        fn = [w['fault_n'] for w in gws]; fu = [w['fault_us'] for w in gws]
        print(f'write-tracking faults on guest threads per wait: med {med(fn):.0f} p95 {q(fn, .95):.0f}; '
              f'time in the handler per wait: med {med(fu):.0f} us p95 {q(fu, .95):.0f} us '
              f'(per fault {sum(fu) / max(1, sum(fn)):.1f} us); faults on this thread med '
              f'{med([w["fault_cp_n"] for w in gws]):.0f}; Protect calls on this thread med '
              f'{med([w["prot_cp_n"] for w in gws]):.0f}, {med([w["prot_cp_us"] for w in gws]):.0f} us')

    # 4. Compute queue at the graphics block.
    print('\n== the compute queue when graphics blocked ==')
    st = collections.Counter((w['c_state'], w['c_sat'], min(w['c_pending'], 3)) for w in gws)
    print('(parked, its condition already true, pending submits capped at 3):', st.most_common())
    ca = collections.Counter(w['c_addr'] for w in gws if w['c_state']).most_common(10)
    print('parked on:', [(hex(x), n) for x, n in ca])

    # 5. Compute waits, submits and writes inside each graphics wait.
    cw_t0 = [c['t0'] for c in cw]
    cs_t = sorted(int(r[1]) for r in rows if r[0] == 'CS')
    ci_t = sorted(int(r[1]) for r in rows if r[0] == 'CI')
    inside_cw, inside_cs, inside_ci, last_ci_to_sat, last_ce_to_sat = [], [], [], [], []
    for w in gws:
        lo, hi = bisect.bisect_left(cw_t0, w['t0']), bisect.bisect_right(cw_t0, w['t1'])
        inside_cw.append(hi - lo)
        inside_cs.append(bisect.bisect_right(cs_t, w['t1']) - bisect.bisect_left(cs_t, w['t0']))
        inside_ci.append(bisect.bisect_right(ci_t, w['t1']) - bisect.bisect_left(ci_t, w['t0']))
        if w['sat'] >= 0 and w['sat'] != w['first']:
            j = bisect.bisect_right(ci_t, w['sat']) - 1
            if j >= 0 and ci_t[j] >= w['t0'] - 50000:
                last_ci_to_sat.append(w['sat'] - ci_t[j])
            ces = [c['t1'] for c in cw if c['t1'] <= w['sat']]
            if ces and ces[-1] >= w['t0'] - 50000:
                last_ce_to_sat.append(w['sat'] - ces[-1])
    print('\n== inside each graphics wait ==')
    print(f'compute waits begun: med {med(inside_cw)} max {max(inside_cw) if inside_cw else 0}; '
          f'compute submits arrived: med {med(inside_cs)} max {max(inside_cs) if inside_cs else 0}; '
          f'compute interrupts: med {med(inside_ci)}')
    if last_ci_to_sat:
        print(f'last compute interrupt -> guest satisfies graphics wait: med {med(last_ci_to_sat):.0f} '
              f'p95 {q(last_ci_to_sat, .95):.0f} us (n={len(last_ci_to_sat)})')
    if last_ce_to_sat:
        print(f'last compute wait end -> guest satisfies: med {med(last_ce_to_sat):.0f} us')

    # 6. Compute waits overall.
    print('\n== compute waits by address ==')
    cby = collections.defaultdict(list)
    for c in cw:
        cby[c['addr']].append(c)
    for addr, cs in sorted(cby.items(), key=lambda kv: -sum(c['dur'] for c in kv[1]))[:16]:
        d = [c['dur'] for c in cs]
        good_c = [c for c in cs if c['sat'] >= 0 and c['sat'] != c['first']]
        lag = [c['t1'] - c['sat'] for c in good_c]
        print(f'{addr:#x}: n={len(cs)} total={sum(d) / 1000:.1f} ms med={med(d):.0f} max={max(d):.0f} '
              f'writer {collections.Counter(c["writer"] for c in cs).most_common(2)} '
              f'sat->noticed med {med(lag):.0f} us; during gfx block {sum(c["g_blocked"] > 0 for c in cs)}')

    # 7. Command-stream writes to the graphics wait addresses (resets).
    gaddrs = {w['addr'] for w in gws}
    wr = collections.Counter()
    for r in rows:
        if r[0] == 'W':
            ad = h(r[4])
            for g in gaddrs:
                if ad + 7 >= g and ad <= g + 7:
                    wr[(hex(g), r[3], hex(h(r[5])), r[6], r[7])] += 1
    print('\n== command-stream writes to graphics wait addresses (addr, source, data, buf, off_end) ==')
    for k, n in wr.most_common(12):
        print(n, k)

    # 8. Learn / forget.
    lx = [(r[0], int(r[2]), r[3], r[4]) for r in rows if r[0] in ('L', 'X')]
    print(f'\n== learn / forget events: {len(lx)} ==')
    for e in lx[:20]:
        print(e)

    # 9. EOP interrupts per frame: buffer index distribution.
    irq = collections.Counter((r[3], r[4]) for r in rows if r[0] == 'I')
    print('\n== graphics EOP interrupts (buf, dwords to end): count ==')
    print(irq.most_common(12))

    # 10. Waits by flip, in stretches of 100 flips.
    print('\n== graphics waits per 100 flips (n, ms) ==')
    s = collections.defaultdict(lambda: [0, 0.0])
    for w in gws:
        s[w['flip'] // 100][0] += 1
        s[w['flip'] // 100][1] += w['dur'] / 1000
    print(' '.join(f'{k * 100}:{v[0]}/{v[1]:.0f}' for k, v in sorted(s.items())))

    # 11. The frame split, joined by time (the wait belongs to the latest submit before it).
    if a.split:
        fr = []
        with open(a.split, newline='') as f:
            for r in csv.DictReader(f):
                fr.append(r)
        sub = [float(r['submit_ms']) * 1000 for r in fr]
        per = collections.defaultdict(float)
        for w in gw:
            j = bisect.bisect_right(sub, w['t0']) - 1
            if j >= 0:
                per[j] += w['dur']
        agree = [(float(fr[j]['gfx_blocked_us']), v) for j, v in per.items()]
        diff = [abs(x - y) for x, y in agree]
        print(f'\n== join with frame_split: {len(per)} frames with waits; |csv - trace| med '
              f'{med(diff):.0f} us, p95 {q(diff, .95):.0f} us ==')

    # 12. A frame's timeline.
    if 0 <= a.timeline < len(gws):
        w = gws[a.timeline]
        j = bisect.bisect_right(flip_t, w['t0']) - 1
        tf = flip_t[j] if j >= 0 else w['t0'] - 30000
        keep = gaddrs | {w['c_addr']} | {c['addr'] for c in cw if tf <= c['t0'] <= w['t1']}
        print(f'\n== timeline: graphics wait #{a.timeline} at flip {w["flip"]} (t relative to the '
              f'previous flip, us) ==')
        for r in rows:
            k = r[0]
            if k in ('GB', 'GE', 'CB', 'CE', 'CS', 'GS', 'F', 'I', 'CI', 'L', 'X') or (
                    k == 'W' and any(h(r[4]) + 7 >= g and h(r[4]) <= g + 7 for g in keep)):
                t = int(r[1])
                if tf <= t <= w['t1'] + 200:
                    print(f'{t - tf:8d} ' + ','.join(r))


if __name__ == '__main__':
    main()
