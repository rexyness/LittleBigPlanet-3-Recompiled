import sys, bisect, statistics as st
rows=[l.rstrip('\n').split(',') for l in open(sys.argv[1]) if not l.startswith('#')]
irq=sorted(int(r[1]) for r in rows if r[0]=='I')
wd=sorted(int(r[1]) for r in rows if r[0]=='W' and r[3]=='gfx WRITE_DATA' and int(r[4],0) in (0x1e1303c70,0x1e1303c78,0x1e1303c80))
A=[];B=[];C=[]
ob=None
for r in rows:
    if r[0]=='GB': ob=int(r[1])
    elif r[0]=='GE' and ob is not None:
        gs=int(r[15]) if int(r[15])>=0 else int(r[1])
        t_reset=wd[bisect.bisect_right(wd,ob)-1]; t_irq=irq[bisect.bisect_left(irq,t_reset)]
        A.append(ob-t_irq); B.append(gs-t_irq); C.append(gs-ob); ob=None
def corr(x,y):
    mx,my=st.mean(x),st.mean(y); n=len(x)
    return sum((a-mx)*(b-my) for a,b in zip(x,y))/n/(st.pstdev(x)*st.pstdev(y))
keep=[i for i in range(len(C)) if C[i]<12000]
a=[A[i] for i in keep]; b=[B[i] for i in keep]; c=[C[i] for i in keep]
print(f'n={len(a)} (spike out) sd irq->block {st.pstdev(a):.0f} us, sd chain {st.pstdev(b):.0f} us, sd wait {st.pstdev(c):.0f}')
print(f'corr(irq->block, irq->flag) = {corr(a,b):.2f}')
print(f'corr(irq->block, block->flag) = {corr(a,c):.2f}')
# regression slope of chain on recording
mx,my=st.mean(a),st.mean(b); slope=sum((x-mx)*(y-my) for x,y in zip(a,b))/sum((x-mx)**2 for x in a)
print(f'slope chain vs recording: {slope:.2f} (1 = chain moves with recording, 0 = fixed chain)')
