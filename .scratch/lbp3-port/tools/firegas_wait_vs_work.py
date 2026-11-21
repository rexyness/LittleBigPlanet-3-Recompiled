import re,sys
V=re.compile(r'^\[video\] (\d+) frames flipped.*work ([\d.]+) ms')
G=re.compile(r'^\[framegen\] gpu:.*gpu ([\d.]+) ms avg.*wait ([\d.]+) ms avg')
F=re.compile(r'^\[faults\] (\d+)')
for path in sys.argv[1:]:
    rows=[];cur=None
    for l in open(path,encoding='utf-8',errors='replace'):
        m=V.match(l)
        if m: cur=dict(flip=int(m[1]),work=float(m[2]));rows.append(cur);continue
        if cur is None: continue
        m=G.match(l)
        if m: cur['gpu']=float(m[1]);cur['wait']=float(m[2])
        m=F.match(l)
        if m: cur['wf']=int(m[1])
    for lvl in (False,True):
        sel=[r for r in rows if 'wait' in r and r['flip']>300 and ((r.get('wf',0)>15000)==lvl)]
        if not sel: continue
        bins={}
        for r in sel:
            b=min(int(r['wait']),6); bins.setdefault(b,[]).append(r['work'])
        name=path.split('/')[-1]
        print(f"{name[:28]:28s} {'LEVEL' if lvl else 'other'}: "+"  ".join(f"wait {b}-{b+1}ms: n={len(v)} work {sorted(v)[len(v)//2]:.1f}" for b,v in sorted(bins.items())))
