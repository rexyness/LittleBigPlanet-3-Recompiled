import sys, csv, statistics as st
for p in sys.argv[1:]:
    fr=list(csv.DictReader(open(p)))
    b=[float(r['gfx_blocked_us']) for r in fr if float(r['gfx_blocked_us'])>0]
    walk=[r for r in fr if 2480<=int(r['frame'])<=3000]
    wb=[r for r in walk if float(r['gfx_blocked_us'])>0]
    rec=[float(r['record_us']) for r in wb]
    print(f'{p.split("/")[-1]:22s} frames with a gfx wait: {len(b)}; blocked med {st.median(b):.0f} p95 {sorted(b)[95*len(b)//100]:.0f} max {max(b):.0f} us; '
          f'walk frames 2480-3000 with a wait {len(wb)}/{len(walk)}; record med (those) {st.median(rec):.0f}; frames over 40: {sum(float(r["period_us"])>40000 for r in fr)}')
