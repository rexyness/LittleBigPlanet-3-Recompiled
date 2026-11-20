# Why does the game hang with the main thread in a job barrier and the compute queue waiting on a label?

Type: task
Status: resolved
Blocked by: none
Map: [LBP3 native port](../map.md)

## Question

Split out of [What hangs the GPU when a Thruster fires?](07-thruster-freeze.md). This freeze is separate from the GPU reset: it causes no reset, the GPU idles at about 44 W, and only the game's frames stop. It has been seen twice, both times on the default vblank pacing:
- the agent's Change to Play Mode → Save Changes with a Thruster switched on (flip 31,380);
- the human's play session (flip 3,180; what they were doing isn't known).

In both:
- **The main thread** spins with `scePthreadYield` in a job barrier at `eboot+0x8aa370`, always with the same call chain. It waits for the minimum of 7 `u16` counters at `[*r15 + rbx + 0x42…0x4e]` to reach a ticket.
- **The workers** (JobManagerWorker0–3, SceneThreadWorker0–4, Spu 0–4) are all idle.
- **The compute queue** sits in an equal-compare `WaitRegMem` on one slot of an 8-slot label ring at `0x1e1303cd8…d10`. The slot holds the last round's value plus one, and the wait wants the next round's value minus one (0x7e higher both times).
- **The gfx queue** waits for a flag (`0x1e1303c70` or `0x1e1303c28`) to become 1.

Which side stalls first, and what change prevents it? How big is that change?

## Leads

- **Two hypotheses, both open:**
  1. **Lost wake-up or dropped job on the CPU side.** The barrier never completes, so the main thread never arms the next compute slot or sets the gfx flag. The two stuck waits are then symptoms. This was the reading after the first hang.
  2. **The runtime breaks an ordering the game relies on.** The compute `RELEASE_MEM` is applied on the CPU as soon as the command processor parses it (`liverpool.cpp`, `SignalFence`), before the dispatch has run on the GPU. Any early release, or the runtime's "early releases / learned waits" for blocked waits, could let the game consume results out of order.
- **The ring protocol, as read from logs and live memory:**
  - Compute `RELEASE_MEM` writes `seq<<4 | 9` round-robin.
  - The game then raises the slot to `…a`.
  - Each compute submission first waits for its slot to equal `…8` of the next round. Some game code arms that value. Which code it is is unknown.
- **One live read of the barrier** (`analysis\t07h2_barrier.txt`, a single read):
  - Ticket 275.
  - Two counters in use, at 47 and 46. The other five are `0xffff`.
- **`LBP3_HWWATCH=<eboot offset>`** puts a hardware write watchpoint on 8 bytes and logs the writing RIP, thread and frame-pointer chain for the first 60 hits (`runtime/src/core/debugger.cpp`). The ring slot at `0x1e1303ce0` is offset `0x1e0f03ce0` from the eboot base `0x400000`. The ring sat at the same address in both runs.
- Evidence (in `D:\Recomps\lbp3-recompiled\analysis\`, not in git):
  - `t07_run1_stderr.txt`, `t07_f10.txt` and `perf\t07_freeze_perf.txt` (the Save Changes hang);
  - `t07h2_stderr.txt`, `perf\t07h2_stall1.txt`, `t07h2_labels.txt` and `t07h2_barrier.txt` (the human's hang).

## Checklist

1. **AFK.** Run once with `LBP3_HWWATCH` on a ring slot, with no hang needed, to name the code that writes `…8` and `…a` (thread and RIP). Name the owners of the barrier's counters `+0x42`/`+0x44` the same way.
2. **AFK.** Reproduce with remote input: load the saved level (Home → Recently Edited → Edit Now), switch the Thruster on, then Change to Play Mode → Save Changes. Do it twice, plus once with the Thruster off as the control. That shows whether the hang is deterministic and whether the Thruster matters.
3. **AFK.** From 1 and 2, settle which hypothesis holds. Try the smallest fix on a throwaway branch, for example applying compute `RELEASE_MEM` only once its dispatch has finished, or fixing the lost wake-up. Rerun step 2.
4. **HITL.** The human plays the session that hung for 10 minutes on the fix.

Keep logs out of git. The game dump stays read-only. Start the build with `cmd /d /c .\build.bat` from `D:\Recomps\lbp3-recompiled`.

## Done when

An `## Answer` says which side stalls first and why. It gives the change that removes the hang in the step 2 repro (a diff on a throwaway branch), or the best-supported hypothesis if none does, and an estimate in sessions for the milestone ladder.

## Comments

- 2026-11-10 (agent, `diag/gpu-checkpoints` `80588d4`, vblank pacing, remote input; the human's hint "the Popit cursor must be in the middle of the Thruster" made the tweak menu reachable).
  - **Reproduced on demand, and the Thruster is required.**
    - With the Thruster firing: load the saved level, then Options → Change to Play Mode → Save Changes. Flips stopped at once (flip 9,600), with the same pair of stuck waits (compute on `0x1e1303cf8` wanting `0x39c8` while holding `0x394a`; gfx on `0x1e1303c20` wanting 1). That makes 2 of 2 with the agent's first attempt.
    - Control, the same steps with the Thruster off: the level saved and Play mode started (0 of 1 hung).
    - Logs: `analysis\t10_run1_stderr.txt`, `t10_run1_labels.txt`, `perf\t10_run1_perf.txt` and `t10_control_stderr.txt`.
  - **The ring's other writer is a CPU job in `spu.prx`, not the GPU.** This answers upstream's open question in `NOTES.md` ("the …8 stage comes from another writer").
    - Method: `LBP3_HWWATCH=1e0f03ce0` (slot `0x1e1303ce0`) with the Thruster firing. The ring moved only while it fired, and never idling in the Pod or in Create mode.
    - Writers, per round:
      1. `spu.prx+0x18f9b` writes `…8` on an `Spu N` thread.
      2. The command processor thread writes `…9` (compute `RELEASE_MEM`).
      3. The same `Spu` thread writes `…a` at `spu.prx+0x18fb8`.
    - The code at `spu.prx+0x18f7d` is a label-command interpreter. Its command word's top 16 bits select the action:
      - `0xcafe`: store the low half, then spin until it becomes +1.
      - `0xbeef`: spin until equal, then store +1.
      - `0xdead`: spin until equal.
      - anything else: store.
    - So a Thruster SPU job hands each compute dispatch its go-ahead (`…8`), spins until the dispatch is released (`…9`), and acknowledges it (`…a`).
    - `spu.prx` is the dump's `gamedata_orbis/spu/spu.prx`, unwrapped to `analysis\spu.elf`. In that run it loaded at `0x246bebe0000`.
  - **At the hang no SPU job is running.** All of `Spu 0–4` wait in `sceKernelWaitEventFlag` (from `spu.prx+0x4451`), and the JobManagerWorkers wait on semaphores. In the human's run (`t07h2`), by contrast, `Spu 0` was spinning at `spu.prx+0x18fa4` (a `0xcafe` wait for `…9`) on a label that wasn't read.
  - **The barrier, read live:** ticket 275 again, with one counter in use at 0 (the human's run had two, at 47 and 46). `r15 = 0x14ad9bb40` and `rbx = 0x30` in both runs, so `LBP3_HWWATCH=*14a99bb40+72` should name whoever advances those counters.
  - **Reading so far.** Leaving Create mode while the Thruster's SPU/compute pipeline runs leaves the compute queue waiting on a go-ahead that no job will give, and the main thread waiting on a job that never starts. Hypothesis 1 (the CPU side) is favoured over the early `RELEASE_MEM`: in this repro nothing on the CPU side is running at all. Why the job isn't started is still open: it could be the runtime's event flags, the save flow cancelling the Thruster's job chain, or a job waiting on a compute result that is never produced.

- 2026-11-10, scripted runs (`analysis\t10_repro.sh` logic, run from the agent's scratchpad; lbp3-recompiled `ecd1d60`, `diag/gpu-checkpoints`):
  - **The same trigger gives both freezes.** With the Thruster firing: Options → Change to Play Mode → Save Changes.
    - **Deadlock (this ticket), 5 runs:** the agent's first attempt, `t10_run1`, `t10w`, `t10s` and `t10e`. The save never starts (13 savedata mounts).
    - **GPU reset (ticket 07), 4 runs:** `t10r`, `t10y`, `t10k` and `t10d`. The save completes (17 mounts), and the GPU is reset during the switch to Play mode.
    - **Thruster off, 2 runs** (the first control and `t10x`): Play mode starts normally.
    - **The outcome follows the sync trace, not how the level was loaded** (pointed out in review). All 4 resets ran with `LBP3_TRACE_SYNC=1`, whose slow per-call formatting was active at the save. None of the 5 deadlocks had it active at that moment: `t10s` had the earlier time-limited trace, which had already stopped. `t10e` loaded the level with the Thruster firing, ran without the trace, and deadlocked.
      - By how the Thruster was switched on: in the session, 6 runs (4 deadlocks, 2 resets); already firing at load, 3 runs (2 resets, 1 deadlock).
      - **Reading:** the deadlock is a timing race, and slowing the sync calls avoids it. The reset may be what follows once the deadlock is avoided. If so, fixing this ticket exposes ticket 07 rather than curing it.
      - To confirm: an A/B on the same restored save, 3 runs with the trace and 3 without. The trace in its current form cannot record a deadlock, so a lighter recorder is needed (raw fields only, formatted at dump time), and it has to be shown to still deadlock.
  - **Owners of the barrier's counters** (`LBP3_HWWATCH=*14a99bb40+72`):
    - The main thread raises `+0x40` (tickets issued) at `eboot+0x8aa67b`.
    - One consumer raises `+0x42` (tickets done). The writer is job code at `+0x3a9` of a separate executable allocation (`0x1c140c10000` in that run, not `spu.prx` itself), running on the `Spu` threads.
    - So the barrier waits for an SPU job chain to drain. At the hang it has 275 tickets issued and none done, and every `Spu` thread sleeps in `sceKernelWaitEventFlag`.
  - **Next for this ticket:**
    1. The A/B above.
    2. Runs under `LBP3_PACING=present`, `LBP3_SYNC_STRICT=1` and `LBP3_GPU_ASYNC=1`, existing switches that move timing and the submit-release policy.
    3. A light event recorder that still deadlocks. It shows whether the main thread set `SpuEventFlag` for the stuck chain, and whether a `Spu` thread woke and went back to sleep.
  - **Side finding (retracted 2026-11-10):** this said `sceKernelPollSema` returns `SCE_OK` without checking or taking the count. That is wrong: since upstream's first version it returns `EBUSY` when the count is short and decrements it otherwise (`kernel_sync.cpp`, `sceKernelPollSema`). `sceKernelPollEventFlag` is also right (a zero-timeout wait, with the timeout mapped to `EBUSY`).

- 2026-11-10, A/B and an event trace of the deadlock (lbp3-recompiled `diag/gpu-checkpoints`):
  - **A/B on the same restored save** (`analysis\savedata_thruster_on`, Thruster firing at load):
    - with the old formatted `LBP3_TRACE_SYNC`: 3 of 3 reset (ticket 07);
    - without it: 3 of 3 deadlocked;
    - with the new light recorder (raw fields, formatted at F10): 2 of 2 deadlocked.
    - So the outcome is a timing race, and the light recorder doesn't disturb it.
  - **Event trace of a deadlock** (`analysis\t10M_sync_trace.txt`, joined lines in `t10M_sync_joined.txt`; thread `43360` is the game's main thread):
    - **ComputeSubmit** (a game thread, entry `eboot+0x451140`) loops once a frame. It sets bit 1 of event flag `'event'` (`eboot+0x4511a7`), then waits for bit 2 (`eboot+0x451187`). Its last wait began at 4,145 ms and never ended.
    - **The main thread** waits for bit 1 in `eboot+0x451300…0x45133c`, which is the same function as the barrier's caller `0x4513fa`. Its last wake was at 4,164.68 ms. It then called `eboot+0x7c2f0` (wait for the frame's SPU jobs: the barrier at `0x8aa370`) and never returned. So bit 2 is never set.
    - **The Spu threads** each wait on their own bit of `SpuEventFlag` (mode `0x21`: AND, clear pattern; no timeout, from `spu.prx+0x3f52`). Kicks (`|= 1f`) come from:
      - the main thread and JobManagerWorker0 (`eboot+0x8ab1ca`);
      - `Orbis::Gfx::Callback` (the GPU interrupt thread, about every 33 ms);
      - the Spu threads themselves (`spu.prx+0x3de3`).
    - The last kicks were at 4,164.53 ms. Every Spu woke, found nothing to run, and was asleep again by 4,164.555 ms. No kick came after that. The GPU interrupt thread's last kick was at 4,130 ms: with the gfx queue stuck on its label wait, no further GPU interrupts arrive.
    - **The runtime's primitives behaved correctly** in the trace: every set woke its waiter, and nothing was lost. The stall is in what the job kernel finds runnable. The kernel's workload table has 16 entries of `0x28` bytes, with ready masks at `+0x38`, read at `spu.prx+0x3fec…0x40b2`.
  - **Reading:**
    - The frame's SPU chain (275 tickets issued, 0 done) is not runnable when the Spu threads look.
    - The chain includes the job that arms the compute ring.
    - The compute queue waits for that arm.
    - The gfx queue waits on a compute-written flag, so no GPU interrupt comes to kick the Spu threads again.
    - The main thread waits for the chain, so ComputeSubmit never gets bit 2.
    - Open: what the chain is blocked on when the Spu threads look. Two candidates: a GPU-label dependency that the kernel checks without spinning, or readiness not yet published when the kick arrived.

- 2026-11-10, **the deadlock cycle, measured** (lbp3-recompiled `diag/gpu-checkpoints`, new tools below; runs `t10S`, `t10P`, `t10Q`, all deadlocked):
  - **The job scheduler at the hang** (found by scanning memory for the `SpuEventFlag` pointer; `analysis\t10S_spurs.bin`):
    - `spu.prx` runs a SPURS-like scheduler with 16 workloads. The state word at `+0x38 + 0x28*i` holds, by byte: running count, **ready count**, an unknown flag, max contention.
    - A job thread takes workload `i` only while running < min(ready, max) (`spu.prx+0x406f…0x414d`).
    - At the hang, all 16 workloads had ready count 0. The stuck chain is **workload 11** ("SPLUID1"; the barrier's `r15` points at `{chain block 0x14fce8700, spurs 0x15067e500, wid 0xb}`, and `0x14ad9bb40` holds the same in every run). Its block shows 275 jobs issued and 0 done.
  - **Workload 11 over time** (`LBP3_POLLWATCH`, a thread that polls memory every ~20 µs without slowing the game's threads; merged with the sync trace by absolute time in `analysis\t10Q_merged.txt`, via `analysis\t10_merge.py`). Normal frames, every 66 ms:
    1. The main thread resets the chain and issues 0x113 jobs in about 0.1 ms.
    2. ComputeSubmit wakes (bit 2), submits the frame's compute work, and sets bit 1.
    3. GPU end-of-pipe interrupt #1 arrives (`EOPQueue`). The `Orbis::Gfx::Callback` thread only signals `CMMSema`.
    4. About 21 ms later, **interrupt #2 arrives, and the callback marks workload 11 ready** (`eboot+0x8ab130`, ReadyCountStore, which kicks `SpuEventFlag`).
    5. The job threads run the chain to 0x113. Its jobs arm the compute ring (`spu.prx+0x18f9b`) and acknowledge it.
  - **The save frame:** steps 1–3 happen, then **interrupt #2 never comes**.
    - The gfx queue is waiting on a flag that compute writes (`0x1e1303c18/c20/c28/c70 == 1`; the gfx stream reset it to 0 itself with WRITE_DATA).
    - Compute is waiting for a ring slot that a workload-11 job arms.
    - Workload 11 waits for the kick from interrupt #2, which sits behind the gfx wait.
    - The main thread waits for workload 11 (the barrier).
    - That is a closed cycle: no lost wake-up, and every primitive behaved correctly.
  - **Open:** why the save frame's gfx stream reaches the compute wait before the interrupt that kicks the chain. Normal frames get interrupt #2 first. Possibly a capture or snapshot submitted at Save Changes, or a submit-order difference from the runtime's synchronous submits. The next step is a gfx-submission trace (each SubmitGfx call with its EOP and WaitRegMem packets) for normal frames and the save frame.
  - **Ruled out:**
    - A runtime lost wake-up: the sync trace and the scheduler state show none.
    - Frame pacing (`LBP3_PACING=present`): 2 of 2 deadlocked.
    - The submit-release policy (`LBP3_SYNC_STRICT=1`, `LBP3_GPU_ASYNC=1`): 2 of 2 deadlocked each.
    - A guest `and`-without-`lock` at `eboot+0x8ab0bb`: it is workload setup (AddWorkload), not a hot path.
    - The single-shot CAS in `spu.prx+0x3e00` (ReadyCountCompareAndSwap, one `cmpxchg` and no retry): a failure there leaves the newer value in place, so it can't zero a ready count.
  - **Tools added on `diag/gpu-checkpoints`:**
    - `LBP3_POLLWATCH=*off+add/*off+add`: values polled from a background thread, written on F10 to `analysis\pollwatch.txt`.
    - `LBP3_HWWATCH_RING=1`: hardware-watch hits kept in memory. It perturbs timing; with it, runs reset instead of deadlocking.
    - Sync-trace entries for equeue triggers (GPU interrupts).
    - Absolute QPC milliseconds in all dumps.
    - The traces are written before a lost device ends the process.

- 2026-11-10, **the frame protocol behind it** (runs `t10A`–`t10E`, `analysis\t10_phases.py`, `analysis\t10_flags.py`):
  - **The stuck work is the fluid simulation.**
    - The job lists live in a struct array near `0x14ad9ba20`, stable across runs: `{block*, spurs*, wid, name}`. Names: `SKIN`, `NORMALS`, `SPLUID0`–`SPLUID3` (wids 8–13 in one run; the order varies).
    - Each `SPLUID` list issues 0x113 or 0xd8 jobs. Lists are issued in alternating frames (`SPLUID1`, then `SPLUID0`+`SPLUID2`, …).
    - A list becomes ready about 50 ms after it is issued, when a GPU-interrupt callback kicks it, and then runs in about 15–20 ms.
    - The barrier the main thread hangs in is "wait for list X": `SPLUID1` in one run, `SPLUID0` in another.
  - **The GPU-interrupt callback** (`eboot+0x5dd50`, thread `Orbis::Gfx::Callback`) works through a ring of `{fn, arg, label}` entries in order. Each is registered with an EOP in the frame's command buffer that writes 1 to its label (`eboot+0x5dc70`). It runs an entry only once that entry's label is 1, and stops at the first one that isn't. The fluid kick is one of these entries.
  - **Each frame starts with a flag wait**, in a 3-frame rotation:

    | frame | first wait (must already be 1) | then | fluid flag waited later |
    |---|---|---|---|
    | A | `c20` | reset `c80`/`c68`, EOP → **fluid kick** | `c80` |
    | B | `c28` | reset `c70`/`c68`, EOP → fluid kick | `c70` |
    | C | `c18` | reset `c78`/`c68`, EOP → fluid kick | `c78` |

    - The first-wait flags (`c18`/`c20`/`c28`) are reset to 0 by the main thread's frame loop (`eboot+0x46a528`, called from `0x42d9e4`). A job thread sets them back to 1 about 8–12 ms later (`spu.prx+0x464eb`, from the job kernel at `0x3bf5`). Both writers were found with `LBP3_HWWATCH` plus `LBP3_HWWATCH_RING` in normal play.
    - The fluid flags (`c70`/`c78`/`c80`) are reset by the gfx stream (WRITE_DATA 0) and set by a fluid job (`spu.prx+0x18fda`, the label-store command).
  - **The deadlock:** the save frame's first wait finds its flag at 0 (`c20` in `t10B`/`t10G`). So that frame's fluid-kick EOP never runs, no fluid list becomes ready, compute waits on fluid arms, and the main thread waits on a fluid list.
  - **The reset path** (`t10C`, which saved and then reset): every first wait found its flag at 1. In each frame the reset and set happened between the waits, two frames ahead.
  - **Open:** in a deadlock run, does the save frame's first-wait flag get reset late, or never set? That decides whether the main thread reset it at the wrong moment, or the job that sets it never ran. A deadlock run with the three flags polled is in progress; the extra polling seems to tilt runs toward the reset path.
  - **Also tried:**
    - `LBP3_BREAK_STUCK_WAITS=2000` (new, opt-in): releases GPU waits stuck for 2 s. The waits were released, but the main thread stayed in the barrier, 2 of 2. So releasing GPU waits is not a stopgap.
    - `LBP3_READBACKS=relaxed` still deadlocked, 2 of 2. `precise` never got past boot (black screen, no frames).

- 2026-11-10, **cause found and fixed** (run `t10F`, with the per-frame flags read at every submit; fix runs `ab_gfxwait_1..4`):
  - At the last submit the save frame's first-wait flag (`c28`) was **1**. About 1.8 ms later, when the port's command processor reached that buffer's first packet (`WAIT c28 == 1`), it was **0**. In every normal frame the processor reaches that packet within about 0.2 ms.
  - In those 1.8 ms the main thread had already started its next frame: the frame-begin routine (`eboot+0x46a500`) rotated to the same slot and reset its flag. On the console the GPU starts a submitted buffer within microseconds, so the game never loses that race there.
  - **Why the port let the game run ahead:** `WaitGpuIdleOrBlocked` returns as soon as *any* queue is blocked on a learned wait address. The compute queue sits on the fluid ring's learned slot addresses most of the frame, so a graphics submit returned before the graphics queue had started on it.
  - **Fix:** graphics submits wait for the graphics queue only (`WaitGfxIdleOrBlocked`: idle, or blocked with the same 20 ms grace and learned-address rules). A blocked compute queue neither holds nor releases them. `LBP3_SUBMIT_WAIT=all` restores the old behaviour.
  - **Result:** the recipe that failed in all 15+ earlier runs (deadlock or reset) **passed 4 of 4**. The level saved, Play mode started, and the logs show no stuck waits, no device loss and no garbage-buffer errors.
  - **Normal play:** the scripted route (which played the Legend of Bunkum cutscene, not the Introduction; see ticket 09) holds 33.3 ms with work at 24.3–25.3 ms per frame, against 23.0–24.7 ms with the old wait. The real Introduction also holds 33.3 ms with the fix (`pace_t09_intro_*`). Camera hitches are ticket 09's.

- 2026-11-10, **HITL session `t10_hitl1`** (240 Hz display, `all` preset: every fix on, vblank pacing, `diag/gpu-checkpoints` `1c80781`). The human added 3 Thrusters and a water emitter to the test level and fired them, and at 18:21:18, about 1.75 minutes after launch, the game wrote the profile save `bigfart8` (2,736 → 6,644 bytes; the log doesn't show whether through Save Changes or an autosave, and the game stayed in Create). It kept running with everything firing. The human stopped after about 4.5 minutes (planned: 10), leaving the game open. Log to frame 8,370: 33.3 ms frames, none over 40 ms after boot (worst 37.6 ms), no stuck wait, no device loss, no rejected sampler, and 0 submits while the graphics queue was blocked. This is the fix's first profile save at 240 Hz (vblank pacing), and with more Thrusters and water than the recipe. The human's words: "it ain't crashing i even added 3 thursters and the water thing". The ticket 07 A/B the same evening is in 07.
  - **The session went on** after that first report. The game ran to frame 42,360 (about 23.5 minutes), and the human kept building and playing until about frame 30,000, then left it idle in the attract video. Their verdict at 18:43 was "seems to be working great". There was still no deadlock, device loss, stuck wait or rejected sampler. 86 frames took over 40 ms after boot, the worst 359 ms at frame 27,810. They fell in windows with new images (asset loads) or with the frame work near or over the 33.3 ms budget (25–48 ms) as the scene grew. Those are hitches for gap 10, not freezes. The log, copied at 18:43, was later overwritten by a stray watcher (09 Comments, tools note); these numbers come from it before that.

## Answer

**Which side stalls first, and why.** The GPU side, because of the runtime.

Each frame, LBP3 rotates through three flag slots (`c18`/`c20`/`c28`, label ids `0x43`–`0x45`). Its frame-begin routine (`eboot+0x46a500`):
1. resets the slot's flag to 0;
2. puts `WAIT flag == 1` at the head of the frame's graphics command buffer;
3. lets the frame's SPU job work set the flag to 1 about 10 ms later (`spu.prx+0x464eb`).

The command buffer also carries an EOP whose callback kicks the fluid simulation's job lists (`SPLUID0`–`3`, `NORMALS`, `SKIN`).

The runtime's synchronous submit released the game as soon as *any* queue was blocked on a learned address, and the compute queue almost always is (the fluid ring's slots). So the game could reach its next frame-begin, which can reuse the same slot (as in the save frame), before the port's command processor reached the new buffer's first `WAIT`. That wait then saw 0 forever. The fluid-kick callback queued behind it never ran, and the rest followed: no fluid list became ready, the compute queue waited for fluid arms, and the main thread waited in its job barrier.

Leaving Create mode with Save Changes while a Thruster's fluid effect runs makes the main thread hit its next frame-begin right after a submit. That turns a rare race into a near-certain one. When timing went the other way (with slower sync calls), the save went through, then the GPU reset, after garbage-buffer errors in the same frames (ticket 07). Under the conditions that produce the reset (the flag poll plus the sync trace), the old wait reset 2 of 2 times, and the fix passed 3 of 3. So in this recipe the reset comes from the same race. The likely mechanism is frames rendered from fluid data the stopped pipeline never finished; that part is not proven. Whether the human's two resets (in Create mode, with no save, at fast pacing) share the cause is still open, because the 240 Hz display needed for fast pacing is off while they're away (ticket 07).

**The change.** [patches/10-gfx-submit-wait.patch](../patches/10-gfx-submit-wait.patch), from branch `fix/gfx-submit-wait` in lbp3-recompiled (`e85156b`, on `local/frame-pacing`) and in its shadPS4 submodule (`40d6adc5`, on `4640726e`). It changes 64 lines in `liverpool.h`/`.cpp` and `runtime/src/gpu/renderer.cpp`. Graphics submits wait for the graphics queue alone: until it is idle, or blocked with the same grace and learned-address rules. `LBP3_SUBMIT_WAIT=all` restores the old wait.

**Verification:** the recipe is Thruster firing, then Change to Play Mode → Save Changes, on the same restored save.

| build | conditions | result |
|---|---|---|
| old wait (`diag` build, `LBP3_SUBMIT_WAIT=all`) | plain | **3 of 3 deadlocked**. At each hang: `c28 = 0`, slot index (`0x1741414`) = 2, so the latest frame-begin had claimed and reset `c28`, the flag the graphics queue waits on (observed, not inferred) |
| old wait | flag poll + sync trace (shifts timing) | 2 of 2 GPU resets |
| fix (`diag` build) | plain | **4 of 4 OK**: saved, Play mode, no stuck waits, no device loss, no garbage-buffer errors |
| fix (`diag` build) | flag poll + sync trace | **3 of 3 OK** |
| clean patch (`fix/gfx-submit-wait`) | plain | **3 of 3 OK** |

All of these ran on the 165 Hz display, on the timer path; the original deadlocks were at 240 Hz. **At 240 Hz (2026-11-10, `tools/t10_ab.sh fix240:3:- fix240present:3:LBP3_PACING=present` on `diag/gpu-checkpoints` `1c80781`): 6 of 6 OK**, 3 with vblank pacing and 3 with present pacing (the human's fast setup), each with 20 save mounts, the completed save. The old wait wasn't rerun there on purpose: at 240 Hz it resets the GPU (07).

- **Normal play:** the scripted route (which played the Legend of Bunkum cutscene, `c01_intro`, not the Introduction; see ticket 09) holds 33.3 ms. Work is 24.3–25.3 ms per frame with the fix and 23.0–24.7 ms with the old wait. The real Introduction (`00_craftworld`) also holds 33.3 ms with the fix (`pace_t09_intro_*`).
- **8-minute Create soak with the Thruster:** OK with both waits. (Correction, 2026-11-10: after 5 minutes without input the game leaves the scene for its attract video (`lbp3_attract.bik`), so the Thruster scene ran only for the first 5 minutes of it, frames 960 to 10,020. The averages below mix the scene and the video. A 60-minute soak with the scene kept alive then passed with every fix on, 04 Comments.) Work averaged 16.2 ms with the fix against 15.0 ms with the old wait; early releases fell from 222 to 17 per 30 frames; 12 and 13 frames took over 40 ms. These soaks ran at 33.3 ms on the 165 Hz display, not at fast pacing, so they say nothing about the human's resets.
- **The residual window** (a submit arriving while the graphics queue is already blocked): 0 in every window of the three reset-condition fix runs.

**Estimate:** half a session to land it on `port` (milestone 1) and recheck there the scenes that motivated the old wait: the Bink intro video, the Newton cutscene, rockets and Thrusters.

**HITL (2026-11-10, checklist step 4):** the human's session `t10_hitl1` at 240 Hz with every fix on: 3 Thrusters and a water emitter fired, a profile save, about 23 minutes, with no deadlock and no reset (Comments). The same evening the human's play at fast pacing reset the GPU with the old wait and didn't with the fix ([07](07-thruster-freeze.md) Answer). On the diagnostic build the intro movie played without tearing (the human listened: "intro plays perfectly") and the human walked past Newton's entrance. **Left for milestone 1:** land the patch on `port` and recheck the Bink intro, the Newton cutscene and rockets there.

- Record keeping:
  - From about 09:50 on, the game window opened on the 165 Hz display (`\.\DISPLAY2`), so frames were paced on the timer path ("30 on timer"). The deadlock reproduced there as well (`t10F`), so display and pacing don't decide it. This matches the A/B with `LBP3_PACING=present`.
  - "The console starts a submitted buffer within microseconds" is an assumption about hardware, not measured.
  - The frame-begin routine resetting the flag is now observed at the hang: the slot index was 2 and `c28` was 0 in each control deadlock (`analysis\ab_oldplain_*_peek.txt`).
  - The fix leaves a residual window. `WaitGfxIdleOrBlocked` still returns at once when the graphics queue is blocked on a learned address from an *earlier* buffer, before the new buffer has started. A counter of such submits ("submits while gfx blocked" in the `[video]` line) read 0 in every reset-condition run (boot, Pod, Create with the Thruster, save, Play). It hasn't been checked in story levels yet.

- 2026-11-10, regression checks of the fix (`diag/gpu-checkpoints` `0640740`, which includes it):
  - **The scripted route** (actually the Legend of Bunkum cutscene, `c01_intro`): 33.3 ms throughout, and "0 submits while gfx blocked" in all 99 status windows. The real Introduction runs (`pace_t09_intro_*`, which include the fix) also show 0 throughout.
  - **The Bink intro movie** (booted without `--skip-intro`; the tearing it once showed is why the runtime waits after each submit): 90 presented frames captured by remote `frames 90`. They contain 31 distinct movie frames and no torn frame: no frame matched the previous image in one half and the next image in the other (`analysis\frames\`). Graphics submits still wait for the graphics queue, which is what fixed the tearing.
