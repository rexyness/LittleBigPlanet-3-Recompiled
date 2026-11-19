import sys, bisect, statistics as st, collections
rows=[l.rstrip('\n').split(',') for l in open(sys.argv[1]) if not l.startswith('#')]
irq=[(int(r[1]),r[3],r[4]) for r in rows if r[0]=='I']
wd=[(int(r[1]),int(r[4],0)) for r in rows if r[0]=='W' and r[3]=='gfx WRITE_DATA' and int(r[4],0) in (0x1e1303c70,0x1e1303c78,0x1e1303c80)]
wdt=[w[0] for w in wd]
ce=[int(r[10]) for r in rows if r[0]=='CE' and int(r[10])>=0]; ce.sort()
cs=sorted(int(r[1]) for r in rows if r[0]=='CS')
fl=sorted(int(r[1]) for r in rows if r[0]=='F')
res=collections.defaultdict(list); pos=collections.Counter()
ob=None
for r in rows:
    if r[0]=='GB': ob=r
    elif r[0]=='GE' and ob is not None:
        g0=int(ob[1]); gs=int(r[15]) if int(r[15])>=0 else int(r[1])
        j=bisect.bisect_right(wdt,g0)-1   # the frame's reset of the flag
        t_reset=wd[j][0]
        # first EOP irq after the reset
        k=bisect.bisect_left([i[0] for i in irq],t_reset)
        t_irq=irq[k][0]; pos[(irq[k][1],irq[k][2])]+=1
        a=bisect.bisect_left(ce,t_reset); t_arm=ce[a]
        b=bisect.bisect_right(cs,t_reset)-1; t_cs=cs[b]
        f=bisect.bisect_right(fl,g0)-1; t_f=fl[f]
        res['flip -> reset (frame buffer 0 starts)'].append(t_reset-t_f)
        res['compute submit -> reset'].append(t_reset-t_cs)
        res['reset -> first EOP irq'].append(t_irq-t_reset)
        res['first EOP irq -> first ring slot armed'].append(t_arm-t_irq)
        res['first EOP irq -> graphics blocks (buffers 0..13 recorded)'].append(g0-t_irq)
        res['first EOP irq -> flag set (the chain)'].append(gs-t_irq)
        res['graphics blocks -> flag set'].append(gs-g0)
        ob=None
print('first EOP irq after the reset (buf, dwords to end):',pos.most_common(3))
for k,v in res.items(): print(f'{k:60s} med {st.median(v):7.0f}  p10 {sorted(v)[len(v)//10]:7.0f}  p90 {sorted(v)[9*len(v)//10]:7.0f}')
