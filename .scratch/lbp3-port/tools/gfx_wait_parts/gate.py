import sys, bisect, statistics as st, collections
rows=[l.rstrip('\n').split(',') for l in open(sys.argv[1]) if not l.startswith('#')]
irq=[(int(r[1]),r[3],r[4]) for r in rows if r[0]=='I']; it=[i[0] for i in irq]
# graphics label writes (EOP/EOS/WRITE_DATA) with place
gw=[(int(r[1]),r[3],r[6],r[7],r[4]) for r in rows if r[0]=='W' and r[3].startswith('gfx')]; gt=[g[0] for g in gw]
gb=sorted(int(r[1]) for r in rows if r[0]=='GB')
c=collections.Counter(); d=[]
for r in rows:
    if r[0]!='CE': continue
    s=int(r[10])
    if s<0 or s==int(r[11]): continue
    j=bisect.bisect_right(gb,s)
    # before the next block only (graphics recording)? classify by whether gfx blocked at s
    k=bisect.bisect_right(gt,s)-1
    if k<0: continue
    dt=s-gw[k][0]; d.append(dt)
    if dt<=60: c[(gw[k][1],gw[k][2],gw[k][3])]+=1
print('ring arms: time since the latest graphics label write: med',st.median(d),'; within 60 us:',sum(x<=60 for x in d),'of',len(d))
for k,n in c.most_common(10): print('  ',n,k)
