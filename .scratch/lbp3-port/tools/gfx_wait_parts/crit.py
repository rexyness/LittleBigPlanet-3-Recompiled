import sys, bisect, statistics as st
rows=[l.rstrip('\n').split(',') for l in open(sys.argv[1]) if not l.startswith('#')]
cw=[]; ob=None
for r in rows:
    if r[0]=='CB': ob=int(r[1])
    elif r[0]=='CE' and ob is not None:
        sat=int(r[10]); cw.append((ob, sat if sat>=0 else int(r[1]), int(r[1]))); ob=None
cb=[c[0] for c in cw]
out=[]; ob=None
for r in rows:
    if r[0]=='GB': ob=r
    elif r[0]=='GE' and ob is not None:
        g0=int(ob[1]); gs=int(r[15]) if int(r[15])>=0 else int(r[1]); g1=int(r[1])
        guest=cp=0
        i=max(0,bisect.bisect_right(cb,g0)-1)
        t=g0; last_end=None
        while i<len(cw) and cw[i][0]<=gs:
            b,s,e=cw[i]
            if e< g0: i+=1; continue
            # gap before this compute wait: compute was running (recording) if previous ended before
            lo=max(t,g0)
            if b>lo: cp+=b-lo            # compute recording / CP work between waits
            # parked until the CPU armed (guest), then noticed (cp)
            guest+=max(0,min(s,gs)-max(b,g0)); cp+=max(0,min(e,gs)-max(s,g0))
            t=max(t,e); i+=1
        tail=max(0,gs-t)  # after the last compute wait: compute recording its last batch, then the guest finishing
        out.append((gs-g0,guest,cp,tail,g1-gs,int(r[21]) if len(r)>25 else -1,float(r[22]) if len(r)>25 else -1, g0))
        ob=None
d=[o[0] for o in out]
print('waits',len(out),'median wait to satisfied',st.median(d))
for k,lab in ((1,'compute parked, waiting for a guest CPU job'),(2,'compute recording / CP between round trips'),(3,'after the last compute wait (CP last batch + guest tail)'),(4,'satisfied -> noticed')):
    v=[o[k] for o in out]; print(f'{lab:58s} med {st.median(v):7.0f} us  share {100*sum(v)/sum(d):5.1f}%')
big=sorted(out,reverse=True)[:5]
print('largest:'); [print('  ',o) for o in big]
