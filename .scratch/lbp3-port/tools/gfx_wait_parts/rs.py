import sys, collections, re
rows=[l.rstrip('\n').split(',') for l in open(sys.argv[1]) if not l.startswith('#')]
tn={}
# thread names: from TN rows (only threads that had BW rows); RS tid may lack names
for r in rows:
    if r[0]=='TN': tn[r[1]]=r[2]
def grp(name):
    for p in ('Spu ','JobManagerWorker'):
        if name.startswith(p): return p.strip()
    return name
c=collections.defaultdict(collections.Counter); tot=collections.Counter()
for r in rows:
    if r[0]!='RS': continue
    g=grp(tn.get(r[3],'tid'+r[3])); d=r[4]
    m=re.match(r'(.*)\+0x([0-9a-f]+) \(runtime\)',d)
    if m: d=m.group(1)+' (runtime)'
    c[g][d]+=1; tot[g]+=1
for g in sorted(tot,key=lambda g:-tot[g]):
    print(f'== {g}: {tot[g]} samples')
    for d,n in c[g].most_common(int(sys.argv[2]) if len(sys.argv)>2 else 12):
        print(f'   {100*n/tot[g]:5.1f}%  {d}')
