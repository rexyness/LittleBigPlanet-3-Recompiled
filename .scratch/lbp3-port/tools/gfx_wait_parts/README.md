# gfx_wait parts

One-off readers of `analysis/m5_gw*_trace.csv` (LBP3_GFX_WAIT_TRACE=1, `perf/gfx-wait`), written for `perf/gfx_wait.md`. `../gfx_wait.py` is the main report; these give the numbers it doesn't. Each takes the trace path as its first argument.

| script | prints |
|---|---|
| `chain.py` | the fluid chain's timing per fluid frame: flag reset, first EOP interrupt, first ring slot armed, graphics blocks, flag set |
| `crit.py` | each graphics wait split into compute parked on a guest job / processor recording compute / tail |
| `lag.py` | how long an armed compute wait sat before the processor resumed it, with graphics blocked or recording |
| `react.py` | histogram: compute release to the next ring slot armed by the guest |
| `bw.py` | guest sleeps and waits next to ring arms and flag sets, and inside graphics waits (needs BW rows) |
| `rt.py <trace> <n> <us before>` | the n-th graphics wait's round trips with Spu wakes, as a timeline |
| `spike.py` | the longest graphics wait: long compute waits and guest waits inside it |
| `rs.py <trace> [top]` | where the sampled threads were (RS rows, LBP3_GFX_WAIT_SAMPLE=1) |
| `b0.py` | buffer 0's hand-over to the flag reset (where the kick sits) |
| `before.py` | round trips, guest parking and processor starvation before the block |
| `gw_faults_before.py` | faults and handler time before and during the block (needs `gw6`'s cumulative columns) |
| `chain_vs_recording.py` | slope / correlation of the chain's end and the wait against the time to the block |
| `gate.py` | ring arms against the latest graphics label write |
| `ctl.py <split.csv>...` | per run, from frame_split.csv: frames with a graphics wait, blocked median / p95 / max |
