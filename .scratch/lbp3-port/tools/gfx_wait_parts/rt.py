import sys, bisect, collections
rows=[l.rstrip('\n').split(',') for l in open(sys.argv[1]) if not l.startswith('#')]
tn={r[1]:r[2] for r in rows if r[0]=='TN'}
ring=lambda a: 0x1e1303cd8<=a<=0x1e1303d10
ev=[]
for r in rows:
    k=r[0]
    if k=='W' and r[3]=='compute RELEASE_MEM': ev.append((int(r[1]),'REL '+r[4]+'='+r[5]))
    elif k=='CI': ev.append((int(r[1]),'CI'))
    elif k=='BW' and tn.get(r[3],'').startswith('Spu'): ev.append((int(r[1]),f'wake {tn[r[3]]} after {r[5]}us'))
    elif k=='BW' and float(r[5])<5000: ev.append((int(r[1]),f'wake {tn.get(r[3],r[3])} {r[4]} after {r[5]}us'))
    elif k=='CE' and int(r[10])>=0: ev.append((int(r[10]),'ARMED '+r[3]+' (compute waited '+r[5]+'us)'))
    elif k=='CB': ev.append((int(r[1]),'compute waits '+r[4]))
    elif k=='GB': ev.append((int(r[1]),'GFX BLOCKS '+r[4]))
    elif k=='GE' and int(r[15])>=0: ev.append((int(r[15]),'GFX FLAG SET '+r[3]))
    elif k=='I': ev.append((int(r[1]),f'gfx EOP irq buf {r[3]}'))
ev.sort()
n=int(sys.argv[2]); t0=None
# find the n-th GFX BLOCKS and print from 6 ms before it to its flag set
idx=[i for i,e in enumerate(ev) if e[1].startswith('GFX BLOCKS')][n]
t0=ev[idx][0]
for t,s in ev:
    if t0-int(sys.argv[3])<=t<=t0+6000:
        print(f'{t-t0:7d} {s}')
        if s.startswith('GFX FLAG SET'): break
