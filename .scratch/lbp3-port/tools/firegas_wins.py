import re,sys
V=re.compile(r'^\[video\] (\d+) frames flipped \(avg ([\d.]+) ms.*work ([\d.]+) ms')
P=re.compile(r'^\[perf\] .*gfx blocked ([\d.]+) ms/frame in (\d+) waits')
F=re.compile(r'^\[faults\] (\d+) write faults, (\d+) pages')
R=re.compile(r'^\[protect\] (\d+) fast, (\d+) walked')
G=re.compile(r'^\[framegen\] gpu:.*gpu ([\d.]+) ms avg.*wait ([\d.]+) ms avg')
rows=[];cur=None
for l in open(sys.argv[1],encoding='utf-8',errors='replace'):
    m=V.match(l)
    if m:
        cur=dict(flip=int(m[1]),avg=float(m[2]),work=float(m[3]));rows.append(cur);continue
    if cur is None: continue
    m=P.match(l)
    if m: cur['blk']=float(m[1]);cur['nw']=int(m[2])
    m=F.match(l)
    if m: cur['wf']=int(m[1]);cur['ua']=int(m[2])
    m=R.match(l)
    if m: cur['prot']=int(m[1])
    m=G.match(l)
    if m: cur['fgw']=float(m[2])
lo=int(sys.argv[2]);hi=int(sys.argv[3]);st=int(sys.argv[4]) if len(sys.argv)>4 else 1
sel=[r for r in rows if lo<=r['flip']<=hi]
for r in sel[::st]:
    print("%6d avg %5.1f work %5.1f blk %4.1f/%2d wf %6d prot %6d ua %6d fgwait %4.1f"%(r['flip'],r['avg'],r['work'],r.get('blk',-1),r.get('nw',-1),r.get('wf',-1),r.get('prot',-1),r.get('ua',-1),r.get('fgw',-1)))
