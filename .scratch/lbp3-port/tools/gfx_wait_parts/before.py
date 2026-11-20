import sys, bisect, statistics as st
rows=[l.rstrip('\n').split(',') for l in open(sys.argv[1]) if not l.startswith('#')]
irq=sorted(int(r[1]) for r in rows if r[0]=='I')
wd=sorted(int(r[1]) for r in rows if r[0]=='W' and r[3]=='gfx WRITE_DATA' and int(r[4],0) in (0x1e1303c70,0x1e1303c78,0x1e1303c80))
ce=sorted(int(r[1]) for r in rows if r[0]=='CE')
# compute waits: parked time split before/after the block
cw=[];ob=None
for r in rows:
    if r[0]=='CB': ob=int(r[1])
    elif r[0]=='CE' and ob is not None:
        s=int(r[10]); cw.append((ob, s if s>=0 else int(r[1]), int(r[1]))); ob=None
nb=[];na=[];pb=[];pa=[];lagb=[]
ob=None
for r in rows:
    if r[0]=='GB': ob=int(r[1])
    elif r[0]=='GE' and ob is not None:
        gs=int(r[15]) if int(r[15])>=0 else int(r[1])
        t_reset=wd[bisect.bisect_right(wd,ob)-1]; t_irq=irq[bisect.bisect_left(irq,t_reset)]
        b=bisect.bisect_left(ce,t_irq); g=bisect.bisect_left(ce,ob); f=bisect.bisect_right(ce,gs)
        nb.append(g-b); na.append(f-g)
        # guest-parked time (CB..sat) and armed-but-not-resumed (sat..CE) before the block
        park=sum(max(0,min(s,ob)-max(c0,t_irq)) for c0,s,e in cw if e>t_irq and c0<ob)
        lag=sum(max(0,min(e,ob)-max(s,t_irq)) for c0,s,e in cw if e>t_irq and s<ob)
        pb.append(park); lagb.append(lag)
        ob=None
print(f'round trips (compute waits ended) per fluid frame: before the block med {st.median(nb)}, after med {st.median(na)}')
print(f'before the block ({"irq->block"}): compute parked on a guest job med {st.median(pb):.0f} us; armed but not yet resumed (processor in graphics) med {st.median(lagb):.0f} us')
