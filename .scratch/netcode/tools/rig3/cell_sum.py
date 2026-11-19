"""cell_sum.py <tag> <marks file> <host log> <guest log>: one cell of the set-conditions matrix, from the
session log lines of both games ([ses], [ses-win], [ses-conn], [p2p-win]). Prints what happened in each
phase (the join, the Pod, the level) and a ROW line for the ticket's table. Phases come from the marks
cell.sh writes. Times in the logs are local wall-clock stamps, hh:mm:ss.mmm."""
import re
import sys
import statistics

tag, marks_path, host_path, guest_path = sys.argv[1:5]


def secs(stamp):
    h, m, s = stamp.split(':')
    return int(h) * 3600 + int(m) * 60 + float(s)


marks, freezes = {}, []
for line in open(marks_path, encoding='utf-8', errors='replace'):
    p = line.split()
    if len(p) == 3:
        marks[p[0]] = secs(p[2])
    elif len(p) == 4 and p[0] == 'freeze':
        freezes.append((int(p[1]), secs(p[3])))

STAMP = r'(\d\d:\d\d:\d\d\.\d\d\d)'


def load(path):
    out = {'win': [], 'conn': [], 'p2p': [], 'ses': [], 'other': []}
    for line in open(path, encoding='utf-8', errors='replace'):
        if line.startswith('[ses-win]'):
            m = re.match(r'\[ses-win\] ' + STAMP + r' [\d.]+ s: (\d+) flips, (\d+) steps, (\d+) flips with no step '
                         r'\(longest run (\d+)\), (\d+) with two or more, (\d+) step counter jumps; simulation (\w+), '
                         r'loading screen (\w+); (\d+) datagrams dropped', line)
            if not m:
                continue
            w = {'t': secs(m[1]), 'flips': int(m[2]), 'steps': int(m[3]), 'nostep': int(m[4]), 'run': int(m[5]),
                 'multi': int(m[6]), 'drops': int(m[10])}
            h = re.search(r'as host: (\d+) frames made, longest (\d+) ms between two, (\d+) waits over 50 ms, (\d+) catch-up',
                          line)
            if h:
                w.update(frames=int(h[1]), framegap=int(h[2]), waits=int(h[3]), catchup=int(h[4]))
            g = re.search(r'\[1\] (\d+), longest gap (\d+) ms', line)
            if g:
                w.update(in1=int(g[1]), in1gap=int(g[2]))
            out['win'].append(w)
        elif line.startswith('[ses-conn]'):
            m = re.match(r'\[ses-conn\] ' + STAMP + r' (\S+): send budget (\d+) \(cap \d+, growth step \d+\), (\d+) cuts.*'
                         r"the game's RTT ([\d.]+) ms( \(WRONG\))?, the link's (\S+)( ms)?; RTT samples (\d+), negative (\d+); "
                         r'unacknowledged (\d+), held out of order (\d+)', line)
            if m and m[7] != 'none':
                out['conn'].append({'t': secs(m[1]), 'budget': int(m[3]), 'cuts': int(m[4]), 'rtt': float(m[5]),
                                    'wrong': bool(m[6]), 'link': float(m[7]), 'neg': int(m[10]), 'unacked': int(m[11]),
                                    'held': int(m[12])})
        elif line.startswith('[p2p-win]'):
            m = re.match(r'\[p2p-win\] ' + STAMP + r' \S+ \(([^)]*)\): to the game (\d+) \(\d+ B\), longest wait (\d+) ms; '
                         r'gaps (\d+) \(filled (\d+), after asking (\d+), given up (\d+), longest (\d+) ms\); '
                         r'rtt (\d+)/(\d+)/(\d+) ms', line)
            if m:
                out['p2p'].append({'t': secs(m[1]), 'path': m[2], 'given': int(m[3]), 'wait': int(m[4]), 'gaps': int(m[5]),
                                   'asked': int(m[7]), 'givenup': int(m[8]), 'gapms': int(m[9]), 'rtt': int(m[11])})
        elif line.startswith('[ses] '):
            m = re.match(r'\[ses\] ' + STAMP + r' (.*)', line)
            if m:
                out['ses'].append((secs(m[1]), m[2].rstrip()))
        elif line.startswith('[p2p] ') and 'so far:' in line:
            m = re.search(r'so far: (\d+) filled from copies, (\d+) sent again of (\d+) asked, (\d+) skipped; test: (\d+) dropped', line)
            if m:
                out['sofar'] = [int(x) for x in m.groups()]
        elif 'diverg' in line.lower() or 'no game datagram for the game in 5 s' in line:
            out['other'].append(line.rstrip()[:160])
    return out


A, B = load(host_path), load(guest_path)


def first(log, pattern, after=0.0):
    for t, text in log['ses']:
        if t >= after and re.search(pattern, text):
            return t, text
    return None, None


def phase(log, t0, t1, host):
    ws = [w for w in log['win'] if t0 + 5 <= w['t'] <= t1]
    if not ws:
        return 'no windows'
    flips = sum(w['flips'] for w in ws)
    nostep = sum(w['nostep'] for w in ws)
    s = (f"{len(ws)} windows, flips per 5 s min {min(w['flips'] for w in ws)}; no step {nostep} of {flips} flips "
         f"({100.0 * nostep / max(flips, 1):.1f}%), longest run {max(w['run'] for w in ws)}, windows with a run of 30 or "
         f"more: {sum(1 for w in ws if w['run'] >= 30)}, of 8 to 29: {sum(1 for w in ws if 8 <= w['run'] < 30)}")
    if host:
        hs = [w for w in ws if 'frames' in w]
        if hs:
            s += (f"; host waits over 50 ms {sum(w['waits'] for w in hs)}, longest between two frames "
                  f"{max(w['framegap'] for w in hs)} ms, catch-up frames {sum(w['catchup'] for w in hs)}")
        gs = [w for w in ws if 'in1gap' in w]
        if gs:
            s += f"; guest's INPUTS longest gap {max(w['in1gap'] for w in gs)} ms"
    return s


def conn(log, t0, t1):
    cs = [c for c in log['conn'] if t0 <= c['t'] <= t1]
    if not cs:
        return 'no lines'
    return (f"budget {cs[0]['budget']} to {cs[-1]['budget']} (min {min(c['budget'] for c in cs)}), cuts "
            f"{sum(c['cuts'] for c in cs)}; game RTT median {statistics.median(c['rtt'] for c in cs):.0f} ms, link "
            f"{statistics.median(c['link'] for c in cs):.0f} ms, wrong in {sum(c['wrong'] for c in cs)} of {len(cs)}, "
            f"negative samples {sum(c['neg'] for c in cs)}; unacknowledged max {max(c['unacked'] for c in cs)}, held max "
            f"{max(c['held'] for c in cs)}")


def link(log, t0, t1):
    ps = [p for p in log['p2p'] if t0 <= p['t'] <= t1]
    if not ps:
        return 'no lines'
    sf = log.get('sofar')
    extra = (f"; in all: {sf[4]} dropped by the test, {sf[0]} filled from copies, {sf[1]} sent again of {sf[2]} asked, {sf[3]} skipped" if sf else '')
    return (f"longest wait for a datagram {max(p['wait'] for p in ps)} ms; gaps {sum(p['gaps'] for p in ps)} (asked again "
            f"{sum(p['asked'] for p in ps)}, given up {sum(p['givenup'] for p in ps)}), longest hold behind a gap "
            f"{max(p['gapms'] for p in ps)} ms; path {ps[-1]['path']}" + extra)


join = marks.get('join_pressed', 0)
pod0, pod1 = marks.get('pod_start', 0), marks.get('pod_end', 0)
lvl_cmd, lvl0, end = marks.get('level_cmd', 0), marks.get('level_play', 0), marks.get('end', 0)
print(f"=== cell {tag}")
t5, _ = first(A, r"host thread's slots:.*\[1\] state 5", join)
t11, _ = first(A, r"host thread's slots:.*\[1\] state 11", join)
tg, _ = first(B, r'\] Kistopawa state 11', join)
blocksA = [(t, x) for t, x in A['ses'] if t >= join and x.startswith('join block: over')]
blocksB = [(t, x) for t, x in B['ses'] if t >= join and x.startswith('join block: over')]


def blk(b):
    return ', '.join(re.search(r'over after ([\d.]+ s)', x)[1] for _, x in b) or 'none'


ok_join = t11 is not None and tg is not None
print(f"join: {'in' if ok_join else 'NOT IN'}; host thread's slot 5 to 11 in "
      f"{(t11 - t5) if (t5 and t11) else float('nan'):.1f} s; join block host {blk(blocksA)}, guest {blk(blocksB)}")
if pod0 and pod1:
    print(f"pod, host:  {phase(A, pod0, pod1, True)}")
    print(f"pod, guest: {phase(B, pod0, pod1, False)}")
level_ok = None
if lvl0:
    exch = [x for t, x in A['ses'] if t >= lvl_cmd and x.startswith('game state exchange') and x.count('[') >= 4]
    runs = [w['run'] for w in A['win'] if lvl_cmd <= w['t'] <= lvl0 + 10]
    level_ok = bool(exch) or (runs and max(runs) >= 15)
    print(f"level start: {'happened' if level_ok else 'NOT SEEN'}; host's longest run with no step around it "
          f"{max(runs) if runs else 0} flips; guest's "
          f"{max([w['run'] for w in B['win'] if lvl_cmd <= w['t'] <= lvl0 + 10] or [0])} flips; guest's INPUTS longest gap "
          f"{max([w.get('in1gap', 0) for w in A['win'] if lvl_cmd <= w['t'] <= lvl0 + 10] or [0])} ms")
    print(f"level, host:  {phase(A, lvl0, end, True)}")
    print(f"level, guest: {phase(B, lvl0, end, False)}")
t_all0, t_all1 = (t11 or pod0), end
print(f"host's connection to the guest:  {conn(A, t_all0, t_all1)}")
print(f"guest's connection to the host:  {conn(B, t_all0, t_all1)}")
print(f"link, host's side:  {link(A, t_all0, t_all1)}")
print(f"link, guest's side: {link(B, t_all0, t_all1)}")
events = []
for name, log in (('host', A), ('guest', B)):
    for t, x in log['ses']:
        if t < join:
            continue
        if x.startswith('catch-up loading screen: shown') or 'ends its connection' in x or x.startswith('test freeze: the'):
            events.append(f"{name} +{t - join:.0f} s: {x[:110]}")
    events += [f"{name}: {x}" for x in log['other'][:4]]
print('events: ' + ('; '.join(events) if events else 'none'))
for ms, t in freezes:
    near = [w for w in A['win'] if t <= w['t'] <= t + 11]
    print(f"freeze {ms} ms at +{t - join:.0f} s: host's longest run with no step {max([w['run'] for w in near] or [0])} flips, "
          f"waits {sum(w.get('waits', 0) for w in near)}, guest's INPUTS longest gap "
          f"{max([w.get('in1gap', 0) for w in near] or [0])} ms, catch-up frames {sum(w.get('catchup', 0) for w in near)}")
kicked = any('ends its connection' in x for _, x in A['ses'] + B['ses'] if _ >= join)
print(f"ROW|{tag}|join {'ok' if ok_join else 'FAILED'}|level {'ok' if level_ok else ('n/a' if level_ok is None else 'NOT SEEN')}|"
      f"kick {'YES' if kicked else 'no'}")
