# What the graphics queue waits for in the heavy walk, who ends the wait, and why it takes ~4 ms

Measurement only, 2026-11-12. This follows `slow_frames.md`, Q2 ("the graphics queue's blocked waits") and its list of unknowns.

- **Code:**
  - runtime worktree `D:\Recomps\lbp3-wt\gfxwait`, branch `perf/gfx-wait` (`9fff263`, from `port` `bac46c2`);
  - shadPS4 worktree `D:\Recomps\lbp3-wt\gfxwait-shadps4`, branch `perf/gfx-wait` (`24f2276a`, from `d5af2ae7`).
  - Both are local and not pushed. Nothing changes with the variables unset.
- **Switches:**
  - `LBP3_GFX_WAIT_TRACE=1` writes `analysis\gfx_wait_trace.csv`. The row formats are in shadPS4's `src/video_core/amdgpu/lbp3_gfx_wait_trace.h` and the runtime's `runtime/src/gpu/gfx_wait_trace.h`. The trace records:
    - every blocked graphics and compute `WAIT_REG_MEM`/`MEM_SEMAPHORE` (address, compare, place in the frame, the compute queue's state, who wrote the releasing value);
    - every command-stream label write, the EOP and compute interrupts, submits, flips, and learn/forget events;
    - the time when the guest satisfied each wait, from a watcher thread spinning on the address;
    - the processor thread's time per wait;
    - write-tracking faults and `Protect` calls per wait;
    - the guest threads' sleeps and blocking waits.
  - `LBP3_GFX_WAIT_SAMPLE=1` adds a RIP sampler of the job threads while graphics is blocked. It stops threads briefly, so it is for diagnosis, not timing.
- **Runs:** `m5_np_route.sh` (Needlepoint Peaks from `analysis/savedata_story_20261111`) with `M5_EXE` set to the `gfxwait` build, on the 165 Hz display, `HOLD=20`. Files are `analysis\m5_<tag>_{trace.csv,split.csv,stderr.txt}` in the clone.
- **Readers:**
  - `tools/gfx_wait.py <trace> [--split <csv>] [--timeline n]` gives the main report.
  - `tools/gfx_wait_parts/*.py` are the one-offs behind the numbers below (README there).

## The answers

1. **What it waits for.** It waits for the **fluid flag**: a `WAIT_REG_MEM` for "equal 1" on `0x1e1303c70`, `…c78` or `…c80`. The three rotate frame by frame, 96–98 waits each per run.
   - **Where the wait sits:** in the frame's **last** top-level buffer (13 of 14), 4,082–4,991 dwords before its end (of 10.6–11.4k).
   - **How the flag is reset:** the frame's first buffer resets it with `WRITE_DATA 0`, 299 dwords before that buffer's end.
     - That happens in every frame of the level (~640 times per address).
     - The wait packet is only there in frames where the fluid simulation runs: 291 of the walk's 521 frames (56%), in two stretches. No other graphics wait blocked in any run.
   - These are ticket 10's fluid flags (`c70`/`c78`/`c80`) at the same addresses, so `slow_frames.md`'s favoured candidate holds.
   - **The first-wait flag (`c18`/`c20`/`c28`) never blocked.** So that document's rule-out of fix 2 doesn't apply.
2. **Who ends it.** A guest CPU thread, in all 291 waits of the clean run and in every wait of the other runs.
   - No command stream wrote the flag during any wait.
   - **The writer is a `Spu` job (inferred):**
     - of the job threads sampled while graphics was blocked, only `Spu 0–4` ran;
     - ticket 10's hardware watch, at the same addresses in Create mode, named the instruction: the fluid job's label store, `spu.prx+0x18fda`.
     - The watch was not rerun here.
   - **The processor notices at once.** From the watcher's "satisfied" to the processor's resume: median 0 µs, p95 1 µs, max 17 µs.
   - **The address is never learned.** There were 0 learn/forget events in all six trace runs, so no early release is involved.
3. **Why ~4 ms.** The wait is the **tail of a serial chain of about 12 ms**: fluid jobs on the CPU alternating with compute work on the GPU.
   - **How the chain runs:**
     - It starts, inferred, at the frame's first EOP interrupt (buffer 0, 0.16 ms after that buffer is handed over). The first ring slot is armed 2.6–3.05 ms after it (p10–p90); the callback itself was not traced.
     - It runs about 15 round trips through ticket 10's 8-slot compute label ring (`0x1e1303cd8…d10`): a job arms a slot, compute records its dispatches and releases, and the next job reacts.
     - It ends when a job sets the flag.
   - **The arithmetic:** the processor reaches buffer 13's wait 7.6 ms after the kick, and the flag comes at 11.9 ms. Graphics waits for the difference: 4.27 ms in the clean run, 4.03 ms median in the one trace-off run.
   - **The chain is not fixed-length.**
     - Its end moves with the time the processor takes to reach the wait: slope 0.93–1.08 in four runs (1.33 in one), r 0.53–0.68.
     - The wait's length doesn't depend on it (r −0.05…+0.21).
     - So faster graphics recording isn't shown to just lengthen the wait. Two readings fit, and this data doesn't separate them:
       - the processor holds the chain back: 1.3 ms of armed compute waits behind graphics buffers, and compute is recorded on the same thread;
       - or the frame's weight drives both.
     - Before the block, the chain does only 3 of its 15 round trips. Compute sits 5.4–5.9 ms parked on guest jobs, and the jobs take 862 faults (`gw6`).
     - The jobs aren't gated by graphics labels: only 5% of ring arms fall within 60 µs of a graphics label write.
   - **Within the wait, by critical path** (clean run, medians; shares of total wait time):
     - 55% compute parked waiting for a guest job (2.4 ms);
     - 20% the processor recording compute between round trips (0.8 ms);
     - 25% after the last compute batch, mostly the job tail before the flag (1.1 ms);
     - 0% noticing.
   - **The single processor thread's part:**
     - recording compute, about a fifth of the wait;
     - **before the wait**, ~1.3 ms per fluid frame of armed compute work waiting for the processor to finish the graphics buffer it is in (per round trip: median 0.18 ms, p90 0.85 ms, max 2.2 ms);
     - nothing in noticing.
   - **The guest's part:** job stages on `Spu 0–4`, each started by an event-flag broadcast.
     - **No timers are involved:** the job threads' only blocking call is an untimed `sceKernelWaitEventFlag`.
     - **Write-tracking faults are heavy.** Per fluid frame the jobs take ~1,380 page faults: 862 before the block and ~520 during it. That costs ~8.6 ms of thread time inside the page manager's handler (5.6 + 3.0 ms), of which `VirtualQuery` alone is ~1.15 ms per wait window.
     - **The spike** (14.7 ms in the trace-off run, 17.7–19.8 ms traced) is a burst of job work with 1,739 faults and 25 ms of handler time.
4. **Safe fixes, ranked against ticket 10's risk:** see the table at the end. In order:
   1. cheaper fault handling (no `VirtualQuery`);
   2. fewer faults on the fluid buffers;
   3. compute turns inside graphics buffers;
   4. faster compute recording;
   5. only then fix 2 (release the game at the block): the largest gain, and ticket 10's risk class.

## The runs, and how much the trace perturbs

| run | trace | walk work | fluid frames (walk) | graphics blocked: median / p95 / max |
|---|---|---|---|---|
| `gw0` | off (control) | 21.25 ms | 291 of 521 | 4.03 / 5.45 / 14.7 ms |
| `gw1` | processor side + watcher (the clean run) | 21.72 ms | 291 | 4.27 / 5.75 / 17.7 ms |
| `gw2` | + guest sleeps/waits | 21.72 ms | 291 | 4.61 / 6.13 / 18.7 ms |
| `gw3s` | + RIP sampler (diagnostic) | 22.60 ms | 291 | 5.03 / 6.61 / 17.7 ms |
| `gw4` | + write-tracking counters | 22.26 ms | 292 | 4.79 / 6.15 / 17.9 ms |
| `gw5` | + `VirtualQuery`/`VirtualProtect` split | 21.81 ms | 291 | 4.91 / 6.46 / 19.8 ms |
| `gw6` | + fault counts before the block (`HOLD=10`) | 21.93 ms | 291 | ~4.7 (trace) |

- The fluid frames are the same frames in every run. The trace's per-frame sum matches `frame_split.csv`'s `gfx_blocked_us` (median difference 6–8 µs).
- **How much the trace costs:**
  - **The control is a single run (n=1),** and its 4.03 ms sits inside the earlier untraced runs' 4.2–4.35 ms (`slow_frames.md`; spike 17–19 ms there).
  - So the clean trace's cost, ~0.25 ms (+6%) on the wait, is within run-to-run spread.
  - `gw5`'s traced spike reached 19.8 ms, 0.2 ms short of the 20 ms grace.
  - The guest-side rows add another ~0.3–0.6 ms: the wait scopes sit in the jobs' event-flag waits.
  - Shares and structure below come from `gw1` where it has them; absolute guest-side times come from the heavier runs and run high by up to ~20%.
  - The trace-off control is the true size: 4.0 ms median.
- Every run printed the 165 Hz display line ("DQI27165"), 0 GPU warnings and 1 frame over 40 ms in the walk.

## The wait

From `gw1`; `gw2`/`gw4`/`gw5` agree to within a few percent.

| address | n | median | p95 | max | compare | value begin → end | writer |
|---|---|---|---|---|---|---|---|
| `0x1e1303c80` | 97 | 4.50 ms | 6.07 | 6.58 | `== 1`, mask `ffffffff` | 0 → 1 | CPU (97 of 97) |
| `0x1e1303c70` | 98 | 4.33 ms | 5.72 | 17.67 | same | 0 → 1 | CPU |
| `0x1e1303c78` | 96 | 4.20 ms | 5.21 | 5.58 | same | 0 → 1 | CPU |

- **The packet** is `WAIT_REG_MEM` (memory, function 3). It sits in top-level buffer 13 (the last of the frame's 14) at 4,082, 4,823 or 4,991 dwords before the buffer's end.
- **Command-stream writes to these addresses** are only the graphics `WRITE_DATA 0` in buffer 0, 299 (sometimes 298) dwords before that buffer's end: 640–646 per address over the run. Nothing else in any stream writes them.
- **The guest satisfied it** (watcher) a median 4.32 ms after the block (p95 5.81 ms). 267 of 291 were seen. In the rest, the processor saw the value first and disarmed the watcher before its next look; its first look comes ~4 µs after the block. The processor resumed 0–1 µs later, max 17 µs.
- **The processor thread while graphics is blocked:**

  | activity | share of blocked time |
  |---|---|
  | testing the wait | 33% (median 13,331 tests per wait) |
  | compute resumes | 35%; 20% ran packets (median 558 packets, 29 dispatches per wait) |
  | `ProcessCommands` | 13% |
  | loop and overhead | 20% |

  Its longest gap between two tests was a median 0.21 ms (p95 0.29 ms): the length of one compute batch.
- **The compute queue when graphics blocked:**
  - parked on a ring slot in all 291;
  - in 237 of those, the slot was **already armed** but not yet resumed (the processor had been inside graphics; see "the processor thread" below);
  - one submit was pending (the frame's).
  - No compute submit arrives during the wait: the frame's compute work was submitted 4.3 ms before buffer 0 started.

## The chain

Medians per fluid frame; `gw1`, with `gw2`/`gw4` in brackets.

| step | time |
|---|---|
| buffer 0 handed over → flag reset (`WRITE_DATA 0`) | 0.16 ms |
| reset → the frame's first EOP interrupt (buffer 0, 269 dwords before its end) | 3 µs |
| first EOP interrupt → first ring slot armed by a job | 2.8 ms (2.3 / 2.9) |
| first EOP interrupt → graphics blocks in buffer 13 (buffers 0–13 recorded) | 7.6 ms (7.4 / 7.8) |
| first EOP interrupt → flag set | **11.9 ms** (11.8 / 12.6) |
| graphics blocks → flag set | 4.27 ms (4.60 / 4.79) |

- **Round trips.** Each fluid frame has ~15 compute waits on the 8 ring slots: each slot about twice, values stepping by `0x10`.
- **Each slot is armed by the CPU, never by a stream:**
  - 4,365 compute waits, all on `0x1e1303cd8…d10`, 545–546 per slot;
  - each lasted a median ~0.2 ms (max 17.6 ms, in the spike).
- **After each slot,** compute records its dispatches (~70 µs) and writes the slot's value +1 with `RELEASE_MEM` and an interrupt. The next job reacts to that.
- **The kick.** The first slot is armed 2.3–2.9 ms after buffer 0's EOP interrupt.
  - That fits ticket 10's protocol: the GPU-interrupt callback thread runs the fluid kick once that EOP's label is set.
  - Which callback entry it is was not traced.
- **How fast the job answers a compute release** (`react.py`) splits in two:
  - **46% within 100 µs:** the job was already waiting for the compute result, so the processor was the critical step.
  - **The rest at 0.5–1.8 ms,** with a gap at 400–500 µs: the job reacts, runs 3–4 stages, then arms the next slot.
- **What the job stages are.**
  - All five `Spu` threads wake together from `sceKernelWaitEventFlag` (`spu.prx+0x3f57`, no timeout) every 120–260 µs. Each wake is a broadcast. In the timelines read, one `Spu` thread is missing from each wake, presumably the one running a job. That is inferred: waits under 20 µs aren't logged.
  - Ring arms and flag sets often fall within 60 µs of such a wake. With five threads waking every 120–260 µs that is weak evidence, and 224 of 1,971 slow arms had no wake in that window.
  - **No timed sleep or timed wait is in the chain.** The threads that sleep during a graphics wait are unrelated: `PSMove Read Thread` 1 ms, the FMOD threads 10 ms, `Pad` 33 ms, `KdConvert` 100 ms, and so on.
  - So the 0.5 ms floor is 3–4 stages of job work, not timer quantisation.
- **Before and during the block** (`gw6` adds cumulative fault counts to the interrupt and block rows; `before.py`, `gw_faults_before.py`):

  | | time | round trips | compute parked on guest jobs | armed compute waiting for the processor | faults | fault-handler time |
  |---|---|---|---|---|---|---|
  | first EOP interrupt → block | 7.1 ms | 3 | 5.4 ms | 1.3 ms | 862 | 5.6 ms (6.6 µs each) |
  | block → flag | 4.7 ms | 12 | ~2.5 ms | 0 | 519 | 3.0 ms (6.1 µs each) |

  - The early stages are the heavy ones.
  - The cost per fault is about the same while the processor is recording graphics. So the handler isn't contending with the processor's recording.
- **The chain's length against the processor's** (`chain_vs_recording.py`, the spike left out):
  - the time to the flag rises with the time to the block: slope 0.93 / 0.93 / 1.08 / 1.33 / 1.02 in `gw1`/`gw2`/`gw4`/`gw5`/`gw6`, r 0.53–0.68;
  - the wait itself is flat against it: r −0.05 to +0.21.
  - **A chain of fixed length would show slope 0 and a wait that shrinks as recording grows. Neither holds.**
  - What couples them is open: the processor holding compute back (above), or the frame's weight driving both. Only 5% of ring arms come within 60 µs of a graphics label write (`gate.py`), so the jobs don't wait on graphics labels.

## Inside the wait: whose time it is (critical path)

`crit.py`: each wait split along the compute queue, which runs one step of the chain at a time.

| run | compute parked on a guest job | processor recording compute | after the last compute batch (job tail) | noticing |
|---|---|---|---|---|
| `gw1` | 55% (median 2.42 ms) | 20% (0.81 ms) | 25% (1.09 ms) | 0% |
| `gw2` | 51% (2.28 ms) | 23% (1.02 ms) | 25% (1.17 ms) | 0% |
| `gw4` | 54% (2.63 ms) | 22% (1.04 ms) | 24% (1.15 ms) | 0% |

- The tail is mostly the job: from the last compute interrupt to the flag takes a median 1.07–1.11 ms. Compute's last batch is ~70 µs of it.
- **So about three quarters of the wait is the game's job work,** and about a fifth is the processor recording compute.

## The processor thread: compute starved behind graphics (before the wait)

- A compute slot armed **while graphics was blocked** was resumed within µs: median 0, p99 1–3 µs, max 18–65 µs.
- A slot armed **while graphics was recording** waited for the processor to finish the graphics buffer it was in:
  - per round trip, median 0.18–0.21 ms, p90 0.76–0.89 ms, p99 1.3 ms, max 2.2–2.6 ms;
  - 1,168–1,260 such round trips per run, 378–390 ms in total, so **~1.3 ms per fluid frame**.
- Graphics yields only at blocked waits, `REWIND` and sub-buffer ends (`slow_frames.md`, Q3.2), so compute gets the processor only between graphics buffers.
- **That delay is on the chain**, since the job waits for the compute result. It is the measured ceiling of `slow_frames.md`'s fix 3.

## The guest side: where the job threads are

- **The sampler run** (`gw3s`, 9,730 samples of `Spu 0–4` while graphics was blocked):
  - 68% waiting (`NtWaitForAlertByThreadId`, under `sceKernelWaitEventFlag`);
  - 32% running. Of the running time:

  | where | share of running time |
  |---|---|
  | **page-protection faults:** `ZwQueryVirtualMemory` 6.0%, `ZwProtectVirtualMemory` 1.4%, exception dispatch (`KiUserExceptionDispatcher`, `ZwContinue`, `RtlQueryFeatureConfiguration`) ~1.0% of all samples | **~26%** |
  | spinning on a label: `spu.prx+0x18fa2`/`18fa4`, ticket 10's `0xcafe` wait | ~15% |
  | waking the other threads (`ZwAlertThreadByThreadId`) | ~4% |
  | job code, `memset`/`memcpy` | the rest |

  - `JobManagerWorker0–3`, `main`, `Orbis::Gfx::Callback` and `ComputeSubmit` were waiting in 97–100% of samples. Of the threads sampled, the chain runs on the `Spu` threads alone. `SceneThreadWorker0–4`, `main fast thread` and `respump thread` weren't sampled.
- **The write-tracking counters** (`gw4`, `gw5`), per graphics wait window:
  - 519–527 faults on guest threads, none on the processor thread;
  - 3.2 ms in the page manager's handler (6.1–6.3 µs per fault);
  - that includes one `Protect` per fault, 1.56 ms (3.0 µs each), split into **`VirtualQuery` 1.15 ms** and `VirtualProtect` 0.35 ms;
  - the rest of the handler, ~3 µs per fault, is the buffer and texture caches' invalidation bookkeeping.
  - The kernel's exception dispatch around each fault isn't in these numbers.
  - The processor thread itself made 35 `Protect` calls per window (0.27 ms: `VirtualQuery` 0.09, `VirtualProtect` 0.18).
- **Faults go with the wait.** Correlation of faults with wait length: 0.93, or 0.87 without the spike.
  - That doesn't prove the faults cause the length, since more fluid work means both more writes and a longer chain.
  - How much of the 3.2 ms of thread time lies on the serial chain needs an A/B. The sampler puts ~1.6 `Spu` threads running on average.

## The spike

- One frame in each run at the same place: 14.7 ms with the trace off, 17.7–19.8 ms traced. It is the same spot as `slow_frames.md`'s offset-375 frame.
- **What happens in it:**
  - The compute queue sits ~15–17 ms on one ring slot waiting for a job; graphics blocks for the last part of that.
  - `Spu` threads were busy ~45 of the 94 thread-ms (`gw2`), about 2.4 threads at once.
  - In `gw4` that frame took **1,739 faults with 25.3 ms of handler time** (14.5 µs per fault, against 6.3 µs normally). That suggests the threads contend in the handler.
- **It never reaches the 20 ms grace and never learns the address.**
  - Its trace-off size, 14.7 ms, is lower than the 17–19 ms in `slow_frames.md`.
  - On a slower CPU it would cross 20 ms. The submit wait would then grace-release main and learn the flag's address, and every later fluid frame would be released early (that document's warning).

## Safe fixes, ranked against ticket 10's risk

Ticket 10's risk class is the game being released, or a label consumed, before the processor or the GPU has done what the game ordered.

| # | Fix | What it shortens | Likely gain | Ticket 10 risk | Other risk | Confirm with |
|---|---|---|---|---|---|---|
| 1 | **`Protect` without `VirtualQuery` when it can:** call `VirtualProtect` on the range first and walk the regions only if that fails (a range across allocations) (`runtime/src/gpu/video_core_glue.cpp`, `AddressSpace::Protect`). Possibly also skip the handler list's copy in `Dispatch` | the fault handler on the job threads (1.15 ms per window of `VirtualQuery`), the spike, and the processor's own `Protect` calls | up to ~0.3–0.5 ms per fluid frame on the chain (thread time 1.2 ms per window spread over the jobs); more in the spike. Small but free | **no ordering change:** the same pages get the same protections in the same order; no wait, label or release moves. Any speed-up of the chain shifts the timing of ticket 10's race, so rerun its recipe | very low; keep the walk as the fallback | the route with `LBP3_GFX_WAIT_TRACE=1` (`vq_us` → ~0, `fault_us`), then trace off: blocked median, walk work |
| 2 | **Fewer faults on the buffers the fluid jobs rewrite each frame.** Either unprotect the whole tracked range (or a large block) on the first write fault instead of one page, or leave such ranges untracked and upload them at each GPU use. First log the fault addresses to find the buffers | ~1,380 faults and ~8.6 ms of handler time per fluid frame (862 before the block, ~520 during it); 1,739 faults and 25 ms in the spike's wait | the largest lever on the game's side of the chain; plausibly 1–2 ms per fluid frame and most of the spike. **Unknown until A/B** | **no ordering change:** the waits, labels and releases are unchanged. Like any chain speed-up it shifts ticket 10's race timing, so rerun the recipe | **GPU coherence:** a missed CPU write leaves stale data on the GPU, a visual bug. Needs screenshot A/B on the fluid scenes (Needlepoint stretches, the Thruster/water level). It is a shadPS4-wide change of write tracking | fault counts and blocked median A/B; screenshots; the Thruster recipe for safety |
| 3 | **Give compute a turn inside long graphics buffers** (yield the graphics task between draws while a compute submit is runnable); `slow_frames.md` fix 3 | the ~1.3 ms per fluid frame that armed compute waits behind graphics recording | ≤ ~1.3 ms per fluid frame (the measured delay, on the serial chain) | **low–medium:** nothing is released early, but graphics and compute recording interleave differently. Ticket 10's deadlock was a timing race, so rerun its recipe | medium: compute recorded inside a render pass ends the pass (GPU cost, load/clear behaviour); recording order changes | A/B; ticket 10's Thruster Save Changes recipe ×3; Bink intro |
| 4 | **Cheaper compute recording** (~70 µs per round trip, 0.8–1.0 ms per wait): profile the dispatch path; buffer-cache uploads and `Protect` are suspects | the processor's fifth of the wait | ≤ ~1 ms, partly overlapping 1 | none | low | a profile of compute `task.resume()` first |
| 5 | **Release the game at the flip submit when graphics blocks on the fluid flag** (`slow_frames.md` fix 2): only for that wait (the three addresses, or "last buffer, after the frame's first packet"), never through a learned address | the whole block, off main's path: 4.0 ms median (14.7 ms spike) in 56% of walk frames | the largest | **high, ticket 10's class:** the game builds frame N+1 while N's chain and recording finish. It is not ruled out, since the wait is the fluid flag, not the first-wait flag. The three rotating flags suggest the game expects frames in flight, but nothing here shows it is safe. Bink tearing is why submits are synchronous | — | only after 1–3; ticket 10's recipe ×3, the Bink intro, long play sessions |

**Open:** whether faster graphics recording shortens fluid frames. The chain's end moves with the block time (slope ~1), but that may be frame weight.

**What will not help here:**
- the processor's noticing, already 0–1 µs;
- timer resolution, since no timed sleep or wait is in the chain;
- the job threads' wakeups: the release interrupt reaches the `Spu` threads in ~7 µs.

## What stays unknown

- **What ties the chain's end to the processor's progress** (slope ~1): the processor holding compute back (1.3 ms of starvation, compute recorded on the same thread), or the frame's weight driving both. This decides whether faster graphics recording shortens fluid frames. Fix 3's A/B would tell.

- **Which buffers the faults hit and which jobs take them.** Fault addresses aren't logged; that is the first step of fix 2.
- **How much of the fault time lies on the serial chain** rather than in parallel jobs. An A/B of fix 1 or 2 measures it.
- **The kernel's exception dispatch cost per fault,** outside the handler. The sampler shows ~1% of `Spu` samples in exception dispatch.
- **Whether the spike's 14.5 µs per fault is lock contention** in the handler (`InvalidateMemory`, the caches' locks).
- **Which callback entry kicks the chain.** Buffer 0's EOP interrupt fits, 2.3–2.9 ms before the first arm, but the callback wasn't traced.
- **The flag writer's RIP in this level.** It was taken from ticket 10's Create-mode hardware watch (same addresses).
  - `LBP3_HWWATCH` wasn't rerun: its 60-hit log would be spent on the per-frame resets long before the walk.
  - Ghidra wasn't used: the job code is in `spu.prx`, outside the analysed eboot project.
- **How long this chain takes on a PS4.** The jobs there run on slower cores, but without write-tracking faults.

## Fix 1, done (2026-11-12)

Branch `fix/protect-fast` (`0570df8`) calls `VirtualProtect` once and walks the regions only when that fails; `LBP3_PROTECT=old` restores the walk. The A/B is 3 + 3 runs on the same exe, trace off; ticket 15 has the table.

| | old | new |
|---|---|---|
| Walk work | 21.0 ms | 18.2 ms |
| Blocked in fluid frames, median | 4.1 ms | 2.6 ms |
| Blocked, p95 | 5.4 ms | 3.3 ms |
| Blocked, max (the spike) | 17–19 ms | ≤ 4.2 ms |
| Recording | 2.7 ms | 1.9 ms |
| Main thread | 6.4 ms | 4.6 ms |

- **Fallbacks:** none. All ~1,100 `Protect` calls per frame took the single call.
- **Why it beat the ~0.3–0.5 ms estimate:** that counted only the job threads' `VirtualQuery` in the wait window, but `Protect` runs ~1,100 times a frame across threads (inferred: the main thread and the recording times dropped too; no per-thread count was taken).
- **Regression:** clean, with the Thruster recipe 3 of 3 OK.
- **Next on the game's side:** fix 2, fewer faults.
