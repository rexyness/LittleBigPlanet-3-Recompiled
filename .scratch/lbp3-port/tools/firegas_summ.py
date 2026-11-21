import re,sys,csv,statistics as st
log,split,f0,f1,tag=sys.argv[1],sys.argv[2],int(sys.argv[3]),int(sys.argv[4]),sys.argv[5]
V=re.compile(r'^\[video\] (\d+) frames flipped \(avg ([\d.]+) ms, worst ([\d.]+) ms, (\d+) over 40.*work ([\d.]+) ms')
P=re.compile(r'^\[perf\] .*gfx blocked ([\d.]+) ms/frame')
F=re.compile(r'^\[faults\] (\d+) write faults, (\d+) pages')
W=[];cur=None
for l in open(log,encoding='utf-8',errors='replace'):
    m=V.match(l)
    if m:
        cur=dict(flip=int(m[1]),avg=float(m[2]),over=int(m[4]),work=float(m[5])); W.append(cur); continue
    if cur:
        m=P.match(l)
        if m: cur['blk']=float(m[1])
        m=F.match(l)
        if m: cur['wf']=int(m[1]); cur['ua']=int(m[2])
w=[r for r in W if f0<r['flip']<=f1]
md=lambda k:st.median([r.get(k,0) for r in w])
rows=[r for r in csv.DictReader(open(split)) if f0<int(r['frame'])<=f1] if split else []
def ms(c):
    v=[float(r[c]) for r in rows if r[c] not in('','-1')]; return st.median(v)/1000 if v else -1
fps=1000/st.mean([r['avg'] for r in w])
print(f"RESULT {tag}: frames {f0}-{f1} ({len(w)} windows) | work {md('work'):.1f} ms | fps {fps:.1f} | over40 {sum(r['over'] for r in w)} | gfx blocked {md('blk'):.2f} ms | faults/frame {md('wf')/30:.0f} | split main {ms('main_cpu_us'):.2f} prep {ms('prep_us'):.2f} record {ms('record_us'):.2f} finish {ms('finish_us'):.2f} copy {ms('copy_us'):.2f}")
