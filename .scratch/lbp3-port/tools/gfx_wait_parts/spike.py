import sys, collections
rows=[l.rstrip('\n').split(',') for l in open(sys.argv[1]) if not l.startswith('#')]
tn={r[1]:r[2] for r in rows if r[0]=='TN'}
ge=[r for r in rows if r[0]=='GE']; ge.sort(key=lambda r:-float(r[5])); top=ge[0]
t1=int(top[1]); t0=t1-int(float(top[5]))
print('spike',top[3],'dur',top[5],'flip',top[2])
# compute waits overlapping
for r in rows:
    if r[0]=='CE' and float(r[5])>3000 and t0-20000<=int(r[1])<=t1: print(' long compute wait',r[3],r[5],'us ending',int(r[1])-t0)
c=collections.Counter(); busy=collections.Counter()
for r in rows:
    if r[0]=='BW' and t0<=int(r[1])<=t1:
        c[(tn.get(r[3],r[3]),r[4])]+=1; busy[(tn.get(r[3],r[3]),r[4])]+=float(r[5])
for k,n in c.most_common(12): print('  ',n,round(busy[k]/1000,1),'ms',k)
