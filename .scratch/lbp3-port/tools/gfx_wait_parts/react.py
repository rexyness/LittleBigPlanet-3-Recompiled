import sys, bisect, collections
rows=[l.rstrip('\n').split(',') for l in open(sys.argv[1]) if not l.startswith('#')]
rel=[int(r[1]) for r in rows if r[0]=='W' and r[3]=='compute RELEASE_MEM' and 0x1e1303cd8<=int(r[4],0)<=0x1e1303d10]
# CE: t,flip,addr,value,dur,polls,max_gap,gres,pc,sat,first,writer,writer_t
react=[]; quick=0
for r in rows:
    if r[0]!='CE': continue
    sat,first=int(r[10]),int(r[11])
    if sat<0: continue
    if sat==first: quick+=1; continue
    j=bisect.bisect_right(rel,sat)-1
    if j>=0 and sat-rel[j]<20000: react.append(sat-rel[j])
print('ring-slot arms after compute release: n',len(react),'already armed when compute arrived:',quick)
h=collections.Counter(min(x//100,40) for x in react)
for b in sorted(h): print(f'{b*100:5d}-{b*100+99:5d} us {h[b]:5d} '+'#'*(h[b]//10))
