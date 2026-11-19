import sys, bisect, collections
rows=[l.rstrip('\n').split(',') for l in open(sys.argv[1]) if not l.startswith('#')]
tn={r[1]:r[2] for r in rows if r[0]=='TN'}
bw=[(int(r[1]),r[3],r[4],float(r[5]),int(r[6]),r[7]) for r in rows if r[0]=='BW']  # t_end,tid,fn,dur,req,caller
bw.sort(); bt=[b[0] for b in bw]
rel=[int(r[1]) for r in rows if r[0]=='W' and r[3]=='compute RELEASE_MEM' and 0x1e1303cd8<=int(r[4],0)<=0x1e1303d10]
def near(sat, win=60):
    i=bisect.bisect_right(bt,sat); out=[]
    j=i-1
    while j>=0 and bw[j][0]>=sat-win: out.append(bw[j]); j-=1
    return out
# slow ring arms
slow=collections.Counter(); slow_n=0; nohit=0
fast=collections.Counter(); fast_n=0
for r in rows:
    if r[0]!='CE': continue
    sat,first=int(r[10]),int(r[11])
    if sat<0 or sat==first: continue
    j=bisect.bisect_right(rel,sat)-1
    if j<0: continue
    react=sat-rel[j]
    hits=near(sat)
    key=tuple(sorted({(tn.get(h[1],h[1]),h[2],h[4],h[5]) for h in hits}))
    if react>=400:
        slow_n+=1; slow[key]+=1
    else:
        fast_n+=1; fast[key]+=1
print('slow ring arms (>=400 us after the release):',slow_n)
for k,n in slow.most_common(8): print(' ',n,k)
print('fast ring arms (<400 us):',fast_n)
for k,n in fast.most_common(5): print(' ',n,k)
# gfx flag sets
g=collections.Counter(); gn=0
for r in rows:
    if r[0]!='GE': continue
    sat=int(r[15])
    if sat<0: continue
    gn+=1
    key=tuple(sorted({(tn.get(h[1],h[1]),h[2],h[4],h[5]) for h in near(sat)}))
    g[key]+=1
print('graphics flag sets:',gn)
for k,n in g.most_common(8): print(' ',n,k)
# BW during gfx waits, by thread/fn/req/caller
gw=[]
ob=None
for r in rows:
    if r[0]=='GB': ob=int(r[1])
    elif r[0]=='GE' and ob is not None: gw.append((ob,int(r[1]))); ob=None
c=collections.Counter(); tot=collections.Counter()
for (a,b) in gw:
    i=bisect.bisect_left(bt,a); 
    while i<len(bw) and bw[i][0]<=b:
        x=bw[i]; k=(tn.get(x[1],x[1]),x[2],x[4],x[5]); c[k]+=1; tot[k]+=x[3]; i+=1
print('guest sleeps/waits ending inside graphics waits (thread, fn, req, caller): n, total ms, mean us')
for k,n in c.most_common(25): print(f'  {n:6d} {tot[k]/1000:8.1f} {tot[k]/n:8.0f}  {k}')
