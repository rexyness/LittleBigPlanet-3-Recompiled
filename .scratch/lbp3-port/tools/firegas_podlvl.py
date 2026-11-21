import re,sys,csv,statistics as st
F=re.compile(r'^\[faults\] (\d+)'); V=re.compile(r'^\[video\] (\d+) frames flipped.*work ([\d.]+) ms')
for tag in sys.argv[1:]:
    log=f'analysis/firegas_{tag}_stderr.txt'; flip=None; wins=[]
    for l in open(log,encoding='utf-8',errors='replace'):
        m=V.match(l)
        if m: flip=int(m[1]); wk=float(m[2]); continue
        m=F.match(l)
        if m and flip: wins.append((flip,int(m[1]),wk))
    lvl=next(f for f,w,_ in wins if w>15000)
    pod=[f for f,w,_ in wins if 300<f<lvl-200]
    a,b=pod[0],pod[-1]
    rows=list(csv.DictReader(open(f'analysis/firegas_{tag}_split.csv')))
    def med(lo,hi,c):
        v=[float(r[c]) for r in rows if lo<int(r['frame'])<=hi and r[c] not in('','-1')]; return st.median(v)/1000
    pw=st.median([w for f,_,w in wins if a<=f<=b])
    lw=st.median([w for f,_,w in wins if lvl+150<=f<=lvl+750])
    cols=['main_cpu_us','prep_us','record_us','gfx_blocked_us','finish_us','copy_us']
    print(f"{tag:9s} POD  {a}-{b} work {pw:5.1f} | "+" ".join(f"{c[:-3]} {med(a,b,c):5.2f}" for c in cols))
    print(f"{tag:9s} LVL  {lvl+150}-{lvl+750} work {lw:5.1f} | "+" ".join(f"{c[:-3]} {med(lvl+150,lvl+750,c):5.2f}" for c in cols))
