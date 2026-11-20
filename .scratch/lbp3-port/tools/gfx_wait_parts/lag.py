import sys, statistics as st
rows=[l.rstrip('\n').split(',') for l in open(sys.argv[1]) if not l.startswith('#')]
lag_in=[];lag_out=[]
gwin=[];ob=None
for r in rows:
    if r[0]=='GB': ob=int(r[1])
    elif r[0]=='GE' and ob is not None: gwin.append((ob,int(r[1]))); ob=None
import bisect
gs=[g[0] for g in gwin]
for r in rows:
    if r[0]!='CE': continue
    sat=int(r[10]); first=int(r[11]); e=int(r[1])
    if sat<0 or sat==first: continue
    i=bisect.bisect_right(gs,sat)-1
    inside= i>=0 and gwin[i][0]<=sat<=gwin[i][1]
    (lag_in if inside else lag_out).append(e-sat)
for lab,v in (('compute slot armed while graphics blocked',lag_in),('armed while graphics recording (not blocked)',lag_out)):
    v.sort(); n=len(v)
    print(f'{lab:46s} n={n:5d} lag med {v[n//2]} p90 {v[9*n//10]} p99 {v[99*n//100]} max {v[-1]} us; over 100us: {sum(x>100 for x in v)}; total {sum(v)/1000:.1f} ms')
