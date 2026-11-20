import sys, bisect, statistics as st
rows=[l.rstrip('\n').split(',') for l in open(sys.argv[1]) if not l.startswith('#')]
irq=[(int(r[1]),int(r[5]),float(r[6])) for r in rows if r[0]=='I']; it=[i[0] for i in irq]
wd=sorted(int(r[1]) for r in rows if r[0]=='W' and r[3]=='gfx WRITE_DATA' and int(r[4],0) in (0x1e1303c70,0x1e1303c78,0x1e1303c80))
bn=[];bt=[];an=[];at=[];dur_b=[];dur_a=[]
ob=None
for r in rows:
    if r[0]=='GB': ob=r
    elif r[0]=='GE' and ob is not None:
        g0=int(ob[1]); gn,gt=int(ob[21]),float(ob[22])
        t_reset=wd[bisect.bisect_right(wd,g0)-1]; k=bisect.bisect_left(it,t_reset); t_irq,i_n,i_t=irq[k]
        bn.append(gn-i_n); bt.append(gt-i_t); dur_b.append(g0-t_irq)
        an.append(int(r[21])); at.append(float(r[22])); dur_a.append(float(r[5]))
        ob=None
print(f'before the block (first EOP irq -> block, med {st.median(dur_b):.0f} us): faults med {st.median(bn)}, handler med {st.median(bt):.0f} us, per fault {sum(bt)/max(1,sum(bn)):.1f} us')
print(f'during the block (med {st.median(dur_a):.0f} us): faults med {st.median(an)}, handler med {st.median(at):.0f} us, per fault {sum(at)/max(1,sum(an)):.1f} us')
