import csv,sys,statistics as st
rows=list(csv.DictReader(open(sys.argv[1])))
cols=['main_cpu_us','waitidle_us','prep_us','record_us','gfx_blocked_us','finish_us','copy_us','slot_us','period_us','first_to_last_enq_us']
def med(rs,c):
    v=[float(r[c]) for r in rs if r[c] not in ('','-1')]
    return st.median(v) if v else -1
for spec in sys.argv[2:]:
    a,b=map(int,spec.split('-'))
    rs=[r for r in rows if a<=int(r['frame'])<=b]
    print(f"frames {a}-{b} n={len(rs)}: "+"  ".join(f"{c.replace('_us','')} {med(rs,c)/1000:.2f}" for c in cols))
