# Slow frames in the heavy walk (Needlepoint Peaks): what the waits wait on

Read-only, 2026-11-11. Nothing built or run.

**Follow-up (2026-11-12):** `gfx_wait.md` names Q2's wait (ticket 10's fluid flag, set by a `Spu` job; the first-wait flag never blocks), times the chain behind it, and ranks the fixes again.

- **Code:** the `perf/instrument` worktree (`D:\Recomps\lbp3-wt\perf`, `d41c4c6`, shadPS4 `bacc9fba` in `lbp3-wt\perf-shadps4`). Line numbers below are from that tree. The audit cites `port`, where `WaitIdle` is at `videoout.cpp:456`; here it is `:494`.
- **Paths:**
  - `videoout.cpp` is `runtime\src\hle\videoout.cpp`;
  - `renderer.cpp` and `frame_split.cpp` are under `runtime\src\gpu\`;
  - `liverpool.*` is `src\video_core\amdgpu\liverpool.*` in shadPS4.
- **Data:** split1's CSV was overwritten. The rows below come from four runs of the same route in `D:\Recomps\lbp3-recompiled\analysis\`:
  - flush off: `m5_hyp1_split.csv`, `m5_ab_off1_split.csv` (the same instrumentation; walk work 27.1–27.9 ms, as in split1);
  - flush on: `m5_ab_on1_split.csv`, `m5_flush_smoke_split.csv`.
  - `m5_split1_stderr.txt` shows the same pattern in its `[perf]` lines: wait-idle grace releases 0+3, then 25+5, and the learned addresses go 0 → 3 → 8.
  - All four runs agree frame for frame (below).

## Summary, ranked

1. **8 of the slow frames are a one-time learning burst, caused by the port.**
   - In 8 consecutive frames, main's pre-submit `WaitIdle` waits out the full **20 ms grace**: `waitidle_rel=2`, 20.0–21.2 ms each. The wait is on the **async compute queue** (the game's only one), parked on a label wait whose address isn't learned yet.
   - Each grace release teaches the runtime one address. "Wait addresses learned" goes 0 → 8 across exactly those frames.
   - From then on, that wait returns at once in every such frame: "known", 283 of 283.
   - It happens once per process: the human's sessions show the same 0 → 8 jump once, with 8 frames over 40 ms. Every fresh-process A/B run on this route therefore carries it, **whatever fix is under test**.
   - The ticket's "+13.7 ms" is this burst averaged over all 12 slow frames: 8 × ~20.5 ms / 12.
   - **Fix:** make the pre-submit wait graphics-only, as ticket 10 did for the submit wait. Low risk.
2. **The "gfx blocked" frames are the frames where the compute queue is parked.**
   - Below, "fluid frames" means that measured fact; that it is ticket 10's fluid chain is inferred.
   - In every frame where that compute queue is parked at the submit, the frame's last command buffer (of 14) blocks once, for 4.2 ms median (p95 5.7). It never blocks in other frames.
   - At one spot of the route the block reaches **17–19 ms**: the same frame in all four runs, 375 frames after the first grace frame. That is 1–3 ms short of the 20 ms grace.
   - It is not walking as such: it happens in two stretches of the walk and nowhere else.
   - Probably the game's own fluid-simulation chain (inferred), put on the critical path by the synchronous submit. The share that is the port's own command-processor work is unmeasured.
3. **The single command-processor thread is a plausible amplifier, unmeasured.** The flip handler running on that thread shows no sign of causing these frames.

## The data

### Every run, the same frames

| run (flush) | grace frames (`waitidle_rel=2`) | `waitidle_us` | spike frame | spike `gfx_blocked_us` | spike offset from first grace |
|---|---|---|---|---|---|
| hyp1 (off) | 2520–2527 | 20.0–21.2 ms | 2895 | 18.1 ms | 375 |
| ab_off1 (off) | 2524–2531 | 20.1–21.0 ms | 2899 | 18.8 ms | 375 |
| ab_on1 (on) | 2528–2535 | 20.1–21.0 ms | 2903 | 19.3 ms | 375 |
| flush_smoke (on) | 2529–2536 | 20.4–20.9 ms | 2904 | 17.1 ms | 375 |

- **The grace frames' periods:** 49–59 ms. On the 60 Hz timer grid at 165 Hz, a late frame lands on 50 ms.
- **The rest of the slow list** is 1–2 frames at 34.1–34.6 ms, a frame that arrives within a millisecond of its grid point (`main_cpu` 11 ms, or `prep` 8 ms).
- **The flush fix** cuts `finish_us` from 10.9 to 6.1 ms and leaves all of the above unchanged.

### Frames with a blocked graphics wait ("fluid frames") against the rest (ab_off1, medians in ms; hyp1 and ab_on1 match)

| frames | n | main CPU | waitidle | prep | record | gfx blocked | finish | slot | `waitidle_rel` |
|---|---|---|---|---|---|---|---|---|---|
| walk, with a gfx blocked wait | 283 | 6.84 | 0.00 | 6.42 | 4.93 | 4.35 | 10.94 | 3.53 | 1 (known) in **all** |
| walk, without | 224 | 6.20 | 0.00 | 5.55 | 0.41 | 0 | 10.81 | 9.78 | 0 (idle) in **all** |
| standing at the basket | 1000 | 6.18 | 0.00 | 5.92 | 0.38 | 0 | 10.95 | 9.20 | 0 |

- **The partition is exact.** In each frame whose last buffer blocks, the compute queue was parked on a learned address when that same frame's submit began (`waitidle_rel=1` in the same row). In every other frame both are idle.
- The grace frames are left out. The 8-frame burst and the 375 offset are the same in all four runs; the medians are ab_off1's.
- **The cost of a fluid frame** on the critical path is about +6 ms: the block 4.4, prep +0.9, main +0.6.
- **This explains the audit's suspect 3.** In `play_log.1`, windows with 20–30 early releases averaged work 36.2 ms against 29.4 ms with none.
- **Where the blocked frames fall.** The `[video]` windows show them in flips ~2520–2670 and ~2800–2970. Flips 2700–2790 are still walking, with no blocked waits and `WaitIdle` idle. The level start and the basket have none.

### Where the graphics wait sits, and what the graphics queue was doing

- **All blocked time is in the frame's last command buffer.**
  - `gfx_blocked_us` counts from the snapshot taken when the frame's last buffer is handed over (`frame_split.cpp:160`; `flip_buffers=14`).
  - Per `[video]` window, the CSV sum equals the `[perf]` line's total exactly.
  - So no earlier buffer of the frame blocked.
- **The graphics queue was idle whenever `WaitIdle` ran.**
  - On every frame after 2400 of every run: `submit_rel=0`, `release_after_flip_us` +5…+11 µs, and `pre_submits=0`.
  - That is, the previous submit returned only after the flip, and nothing else was submitted in between.
- **The game maps one compute queue**: `[gnm] compute pipe 0 queue 3 mapped to vqueue 1 (ring 1024 dwords)`, once in every log, including the play logs.

## Q1. What `WaitIdle` waits for in the 8 frames

**The chain:**
- `videoout.cpp:494` calls `gpu::WaitIdle`, `renderer.cpp:604-609`, which calls `WaitGpu`, `renderer.cpp:585-592`, which calls `WaitGpuIdleOrBlocked`, `liverpool.h:99-126`.
- That function waits while `num_submits ≠ 0`, which counts every queue: compute submits add to it at `liverpool.cpp:1469`.
- It returns at once only if some queue is blocked and the latest blocked address is learned (`:104-112`); otherwise it returns after 20 ms (`:113-119`).
- Ticket 10 moved the **submit** wait to the graphics queue alone (`renderer.cpp:715`, `liverpool.h:133-160`) but left this pre-submit wait on all queues.

**Measured:**
- **The return value says blocked.** Result 2 comes only from the blocked branch, so in each of the 8 frames a queue sat blocked on a wait for 20 ms. It was not running dispatches: recording doesn't count as blocked. Nor was it an end-of-pipe event, which never blocks.
- **The queue was the compute queue.** The graphics queue was idle (above), so it can only be the compute queue, vqueue 1. Its blocking packets are `WAIT_REG_MEM` (`liverpool.cpp:1343-1367`) or `MEM_SEMAPHORE` (`:1317-1342`).
- **The wait was on memory the game's CPU writes.** Each grace release added one learned address (0 → 8 over the 8 frames). `EndBlockedWait` learns only addresses that no command stream wrote during the wait (`liverpool.cpp:93-112`).
- **It happens once per session.** In the play logs:
  - `play_log.1`: learned 0 → 8 at flip 65,400, 8 frames over 40 ms;
  - `play_log.txt`: 0 → 3 → 8 at flips 7,680 and 7,710, 3 + 5 frames over 40 ms;
  - `play_log.3`: 0 → 8 at flip 3,570.

**Inferred, not confirmed by these logs:**
- **The 8 addresses are probably the 8 slots of the compute label ring from ticket 10.** There, every compute submission first waits for its slot to read `…8`, which an SPU job writes (`spu.prx+0x18f9b`). That reading is based on ticket 10's Create-mode addresses `0x1e1303cd8…d10`.
- **A cycle, broken only by the grace.** The arming job belongs to a fluid job list that becomes ready only when the game's GPU-interrupt callback handles an end-of-pipe event (EOP) in the frame's graphics buffers. Those are the buffers main submits *after* this wait. That would explain both observations:
  - none of the 8 waits ended before the grace;
  - in every later fluid frame, the compute queue is still parked at this point.
- This is the same shape as ticket 10's deadlock, with this wait as the waiting party.

**To confirm:**
- **Cheapest, no build:**
  - `LBP3_HWWATCH=1e0f03ce0` (ticket 10's slot `0x1e1303ce0` as an eboot offset) on the heavy route. Writers `spu.prx+0x18f9b` and `+0x18fb8` during the fluid stretches would mean the same ring at the same address.
  - Valid only if story mode allocates it there. The watch shifts timing, but this run only needs the writers' names.
- **One line of code:** in `EndBlockedWait`'s learn and forget branches (`liverpool.cpp:105-111`), print the address and whether the queue is graphics or compute.
- **CSV columns:** at `WaitIdle`'s return (`renderer.cpp:606`), add:
  - `last_blocked_addr`;
  - graphics submits pending;
  - compute submits pending.
- **Sub-millisecond timing:** a timestamp when the compute wait ends, against the frame's `Enqueued` and EOP times, would show it ends only after main submits.

## Q2. The graphics queue's blocked waits

**Measured:**
- **One wait per fluid frame**, in the last of the frame's 14 buffers (above): 4.2 ms median, p95 5.7, 17–19 ms at the spike.
- **The address is never learned.** Main's submit wait never released early (`[perf]` submit 0+0 in every window), and an address is learned only after a grace release. So each block either ended within 20 ms or was ended by a command-stream write.
- **The waits never occur outside fluid frames** (the partition above).

**Not in the data:**
- The address.
- The packet: `WAIT_REG_MEM` (`liverpool.cpp:968-1003`) or `MEM_SEMAPHORE` (`:928-953`).

**Candidates**, from ticket 10's Create-mode frame protocol:
- **Favoured: the per-frame fluid flag** (`c70`/`c78`/`c80` there). The frame's graphics stream resets it with `WRITE_DATA 0`, and a fluid job sets it (`spu.prx+0x18fda`) after the compute round trips on the ring.
- **Disfavoured: the frame's first-wait flag** (`c18`/`c20`/`c28`, set by `spu.prx+0x464eb`). Non-fluid frames reach the last buffer only a little earlier (main + prep 11.8 ms against 13.3 ms) and never block, so it isn't a wait present in every frame.
  - This argument assumes the fluid work doesn't also delay the job that sets the first-wait flag. If it does, this candidate is back in play.
  - If the wait turns out to be this flag, fix 2 below is ticket 10's race and is ruled out.

**`gfx_blocked_us` is not idle time.**
- While the graphics task waits it yields (`YIELD_GFX`, `:998`). The same thread then runs the compute task (`liverpool.cpp:208-236`), and that work (recording the fluid dispatches, `:1256-1300`) is billed to "blocked".
- How the 4.2 ms splits between the game's SPU job time and this thread's compute work is unknown.
- **A weak check from the rows** finds no trade-off between the block and the time spent before the last buffer: r = −0.07…+0.07 against `first_to_last_enq_us`, over about 282 frames per run. So the block isn't simply what remains of a fixed chain started early in the frame. That fits a fluid kick late in the frame, but it is confounded by frame weight.

**Why only while walking:**
- It isn't the walking. It follows the two stretches of the route where the fluid simulation runs. Flips 2700–2790 are walking with no blocks; the start and the basket have none.
- **Discriminating run:** stand still inside one of those stretches.
- On a PS4 the same wait would stall only the GPU while the CPU built the next frame. Here main waits for the graphics queue to go idle (`renderer.cpp:715`), so the whole chain lies on the frame's critical path.

**The spike at offset 375:**
- Content, at the same spot in every run, with a heavier frame all round: prep 11 ms, main 7.7–8.8 ms.
- It sits 1–3 ms below the grace. On a heavier scene the wait would cross 20 ms. The submit wait would then grace-release main and learn that graphics address. From then on every flip submit would release as soon as the graphics queue blocks there: ticket 10's residual window, opened in normal play.
- **The play logs may show this.**
  - `learned_waits` counts additions, so a 9th to 11th "learn event" may be a new address or a forgotten ring slot learned again.
  - On this route the count stays at 8, so no forgetting was seen here.
  - After such an event, windows with more than 30 releases per 30 flips appear, so a second call site is releasing:
  - `play_log.1`: 0 of 934 windows at 8 learned, 103 of 477 at 9;
  - `play_log.3`: 18 of 77 windows once the 10th address is learned.
- The old status line doesn't name the site.

## Q3. Structural causes

1. **Yes: the all-queue pre-submit wait closes a cycle with the compute ring** (Q1). The cause is in the port, and it produces the 8 slow frames.
   - The learning that later hides it is fragile. An address is forgotten if a command stream writes within ±7 bytes of it during a wait, or if more than 64 label writes of any kind happen during the wait (`liverpool.cpp:69-77`, `:105-106`). A forgotten address costs another 20 ms grace.
2. **The single command-processor thread, cooperative round robin** (`liverpool.cpp:208-236`).
   - A task runs until it yields. Graphics yields only at a blocked wait, a `REWIND`, or when a sub-buffer yields (`:958-1016`); compute is the same.
   - So compute waits through whole graphics buffers, and the next graphics buffer waits through any compute batch the thread has picked up. That batch is counted in `prep`: +0.9 ms median in fluid frames, and the spike frame's +5 ms prep may be this or more draws.
   - Every compute round trip of the fluid chain goes through this thread, and the chain is on the critical path. **Size unmeasured.**
3. **The flip handler on the same thread:** the `PatchedFlip` NOP signals synchronously (`liverpool.cpp:431-434`), running the handler at `renderer.cpp:125-133` and `videoout.cpp:218-245`.
   - That covers `Finish` ~11 ms (6 with flush), the copy 0.7 ms and the slot wait (3.5 ms median in fluid frames). During it no queue is processed and no EOP or `RELEASE_MEM` is signalled.
   - **No sign it causes these frames.** In the steady state the next compute batch is submitted only after the game is released, which is after the flip. `DingDong`'s wait found no blocked queue in any window (`[perf]` compute 0+0).
   - Its waiting time is not measured, though.
4. **The thread spins when every queue is blocked**: the loop at `:208` never sleeps. With 24 logical CPUs (Ryzen 9 5900X) it is unlikely to slow the SPU jobs. Not a candidate.

## Q4. Fixes, by likely gain and risk

| # | Fix | Gain | Risk | Cheapest confirmation |
|---|---|---|---|---|
| 1 | **Graphics-only pre-submit wait:** `gpu::WaitIdle` (`renderer.cpp:604-609`; its only caller is `videoout.cpp:494`) calls `WaitGfxIdleOrBlocked`; `LBP3_SUBMIT_WAIT=all` restores the old wait. ~3 lines | Removes the 8-frame burst: 8 × ~20 ms of `WaitIdle`; 8 of the 9 frames over 40 ms in every walk run; one ~0.4 s hitch per session. Also removes any later grace at this site, so the fragile learning no longer matters here. Steady state unchanged | **Low.** In this state the wait already returns at once (283 of 283 fluid frames, "known"), and the graphics queue is idle here on every frame (it returned idle at the previous submit). Ticket 10's race needs the game to run before the processor reaches its *own new* buffer's first wait; this wait comes before that buffer is submitted. **Watch:** `DingDong`'s all-queue wait (`renderer.cpp:652`) could meet the parked compute queue on an unlearned slot and grace there instead, which moves the hitch to `ComputeSubmit`, which main waits on. The data show no such case (compute 0+0) | Heavy route A/B: `waitidle_rel=2` 8 → 0, frames over 40 ms 9 → 1, `[perf]` compute stays 0+0. Regressions: ticket 10's Thruster Save Changes recipe ×3, and the Bink intro tearing check (`frames 90`) |
| 1a | **Until 1 lands, A/B method:** leave the `waitidle_rel=2` rows out of the over-40 count, or warm up first. The fresh-process burst pins the count at 8 | Makes the flush and other A/Bs readable | none | none |
| 2 | **Release the game at the flip submit when the graphics queue blocks in the frame's last buffer**, after it has passed the frame's first packet: one frame in flight; the next pre-submit wait (graphics-only after 1) waits for that frame's flip | Takes the 4.2 ms block off the critical path in ~55% of walk frames, and overlaps main's next frame with the spike frame's 18 ms. The largest gain here | **Medium-high:** the class ticket 10 records ("released too early"), plus the Bink tearing that made submits synchronous; the readback of frame N runs while the game builds N+1. **Ruled out if the wait is the first-wait flag** (Q2). It may already be happening by accident in long sessions (learn events 9–11), unconfirmed | First name the wait (Q2 checks), then A/B with ticket 10's recipe and the Bink intro |
| 3 | **Give compute a turn inside long graphics buffers** (yield the graphics task between draws while a compute submit is runnable), so the fluid round trips don't queue behind whole buffers | Unknown, at most the block (4.2 ms; 18 ms spike); only if the chain is bound by this thread's latency | **Medium:** compute recorded mid render pass splits passes (GPU cost, load and clear behaviour); changes recording order | Instrument first: time inside compute `task.resume()` per frame (`liverpool.cpp:223`), split by whether graphics is blocked, plus the gap before a runnable compute task is resumed. If that is small, drop this fix |
| 4 | **Move the flip handler** (`Finish`, readback, slot wait) off the processor thread | None seen on slow frames; belongs to the throughput lane | Medium-high (flip ordering, the timing of the job kicks) | Compute submits pending when the flip interrupt begins; `DingDong`'s wait time |

## What stays unknown

- The 8 learned addresses (ring slots: inferred), and the graphics wait's address, packet and writer (fluid flag: favoured). Check with `LBP3_HWWATCH`, then the `EndBlockedWait` print and the CSV columns in Q1.
- How the 4.2 ms block splits between the game's SPU job time and this thread's compute recording and round-trip latency. That decides between fixes 2 and 3.
- Where in the 14 buffers the fluid-kick EOP sits. Timestamp the GfxEop interrupts per frame in the handler, `renderer.cpp:129`.
- What the spike at offset 375 is, beyond "a heavier frame at that spot".
- How long `DingDong` waits: its duration is not measured, only its releases.
- Which site releases in the human's long sessions after the 9th learn event, and whether those events are new addresses or forgotten ones learned again. A long session on the instrumented build, whose `[perf]` line splits releases by site, would show it.
- Why this stretch of the level runs the fluid simulation. The standing-still run in a stretch separates motion from place.
