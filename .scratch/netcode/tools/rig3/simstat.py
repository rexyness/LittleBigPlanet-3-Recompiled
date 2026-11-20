"""simstat.py <stderr log> <from HH:MM:SS> [to HH:MM:SS]: no-step flips from the [sim] lines (LBP3_TRACE_SIM=1)."""
import re
import sys

path, t0 = sys.argv[1], sys.argv[2]
t1 = sys.argv[3] if len(sys.argv) > 3 else '99'
rows = []
for line in open(path, encoding='utf-8', errors='replace'):
    m = re.match(r'\[sim\] ([0-9:.]+) steps per flip: (\S+)', line)
    if m and t0 <= m.group(1) < t1:
        rows.append((m.group(1), m.group(2).replace('?', '')))
s = ''.join(r[1] for r in rows)
idx = []
for t, part in rows:
    idx += [t] * len(part)
runs = [(idx[m.start()], len(m.group())) for m in re.finditer('0+', s)]
big = [r for r in runs if r[1] >= 5]
print(f'flips {len(s)}, no step {s.count("0")} ({100 * s.count("0") / max(len(s), 1):.2f}%), '
      f'2+ steps {sum(1 for c in s if c in "23456789")}, longest no-step run {max([n for _, n in runs] or [0])}')
print('runs of 5+ flips with no step (line time, flips):', big[:20])
