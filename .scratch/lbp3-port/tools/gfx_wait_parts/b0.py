import sys, bisect, statistics as st
rows=[l.rstrip('\n').split(',') for l in open(sys.argv[1]) if not l.startswith('#')]
gs=sorted(int(r[1]) for r in rows if r[0]=='GS')
fl=sorted(int(r[1]) for r in rows if r[0]=='F')
res=[];res2=[]
ob=None
wd=sorted(int(r[1]) for r in rows if r[0]=='W' and r[3]=='gfx WRITE_DATA' and int(r[4],0) in (0x1e1303c70,0x1e1303c78,0x1e1303c80))
for r in rows:
    if r[0]=='GB': ob=int(r[1])
    elif r[0]=='GE' and ob is not None:
        j=bisect.bisect_right(wd,ob)-1; t_reset=wd[j]
        f=bisect.bisect_right(fl,t_reset)-1
        k=bisect.bisect_right(gs,fl[f])  # first hand-over after the previous flip
        res.append(t_reset-gs[k]); 
        # last hand-over before the block
        m=bisect.bisect_right(gs,ob)-1; res2.append(ob-gs[m])
        ob=None
print('first buffer handed over -> flag reset (buffer 0 near its end): med',st.median(res),'p90',sorted(res)[9*len(res)//10])
print('last hand-over before the block -> block: med',st.median(res2))
