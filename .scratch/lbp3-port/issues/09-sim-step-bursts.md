# Why does the sim run 0 or 2 steps per frame at a steady 33.3 ms?

Type: task
Status: resolved
Blocked by: none
Map: [LBP3 native port](../map.md)

## Question

With flips held at exactly 33.3 ms ([Why doesn't the 30 fps cap hold on this PC?](06-frame-pacing.md)), LBP3's simulation still runs 0 or 2 steps in 7–43% of flips, in bursts (`120210…`, long `0202…` runs). `tools/camera_check.py` shows this as camera snaps: a double step followed by one or two near-zero steps, 7–19 per 240 frames while walking in the Introduction. Upstream reports the same problem as game-side and unsolved (`NOTES.md:633-650`). What decides how many sim steps a frame runs, why does it run 0 or 2 when frames are exactly 33.3 ms apart, and what change makes it run 1? How big is that change?

The milestone ladder needs the answer to place smooth camera movement: is it a quick runtime fix or a reverse-engineering risk? Priority is low: in the human's 5.5-minute play test on the fixed pacing (ticket 06, step 4), they "didn't see anything wrong with the camera". The snaps are measurable but weren't noticed in normal play.

## Leads

- **Measurements from ticket 06** (table in its Answer; logs in `D:\Recomps\lbp3-recompiled\analysis\pace_*_stderr.txt`, with `LBP3_TRACE_SIM=1`):
  - Steps ≠ 1 ran 7–43% and camera hitches 7–19 per walk across all the correctly paced variants. Repeat runs of the same variant vary as much as the variants do, and the two measures disagree, so **no pacing variant is shown to help**.
  - Unpaced at 26 ms frames (too fast): 0.8% and no hitches. The sim then runs 1 step per frame even though frames are 22% shorter than a step, so the step count is not simply elapsed time ÷ 33.3 ms on the frame clock.
- **Clock reads at 30 per second** (`LBP3_TRACE_TIME=1`, `pace_vblank_trace_stderr.txt`, 150 per 5 s each): `sceKernelClockGettime` through the timer wrappers `eboot+0x1b1df`/`0x1b423`, called from `0x34ab66` (< `0xa0e2`), `0x382a44` (< `0x34ac89`), `0x78d421`, `0x92f929` (< `0x42cbc5`) and `0x39e827` (< `0x39f891`). Upstream had already named `0x34a…` (the main update) and the other four as candidates for the frame delta.
- Sim and game frame counters: `GetSimFrame__` at `eboot+0x7c79e0`, `GetGameFrame__` at `0x7c79f0`. They advance together.
- The "game host thread" sends one simulation tick per frame (`kernel_time.cpp`, `OnFrameClock`). Its timing relative to the main thread's frame start is a suspect for 0/2 races.
- `sceKernelReadTsc` and `sceKernelGetProcessTime` return real time, not the frame clock. Check whether the step logic reads either.

## Checklist

1. **AFK.** Use the `reverse-engineering` skill on the eboot (from the dump, read-only). Starting from the 30 Hz callers above, find the code that computes the frame's sim step count and the clock or counter it reads.
2. **AFK.** Use `--break=` or a temporary log on `local/frame-pacing` to log, per frame, the delta that code sees next to the step count. Put the step count in `analysis\pace.csv` too, so steps and camera hitches are measured over the same frames (in ticket 06 they weren't). Show which input produces the 0s and 2s.
3. **AFK.** On a throwaway branch, try the smallest fix, for example feeding that code the flip-anchored time or an exact 1/30 s delta, or hooking its thread's sleep or tick. Measure it with the ticket 06 script ([tools/pace_run.ps1](../tools/pace_run.ps1), copied into the clone's `analysis\`): 1,200 flips with `LBP3_TRACE_SIM=1`, plus `camera_check.py`, at least two runs per variant.
4. **HITL.** The human walks the Introduction for two minutes on the best variant and says whether the camera is smooth.

Keep logs out of git. The game dump stays read-only. Start the build with `cmd /d /c .\build.bat` from `D:\Recomps\lbp3-recompiled`.

## Done when

An `## Answer` names what sets the step count and why it runs 0 or 2 at a steady 33.3 ms. It gives the change that brings steps ≠ 1 under 1% across two runs (a diff on a throwaway branch, not merged), or the best-supported hypothesis if none does, and an estimate in sessions for the milestone ladder.

## Comments

- 2026-11-10 (agent, AFK). Steps 1–3 done, measured on the 165 Hz display (the 240 Hz one was off). Builds: `diag/gpu-checkpoints` `542b151`/`e78139b`, and branch `fix/epoll-precise` `ea230a4`.
  - **What sets the step count.** Each frame:
    1. The main thread (`LBP3_TRACE_SYNC` with new trace points for loopback sends and receives, epoll waits, flips and the sim counter) sends one message to the "game host thread" just after the flip, then reads that thread's tick.
    2. The host thread sends a tick whenever its `sceNetEpollWait` with a 33,333 µs timeout expires. It may send two when it judges itself behind.
    3. The sim runs one step per tick it has received.
  - **Why 0, then 2.** The runtime's `sceNetEpollWait` truncated the timeout to whole milliseconds (33 ms) and then slept in 1 ms polls past the deadline. So each host wake landed at a different point of the frame: about +22.4, +24.3, +26.2, +28.8 ms after successive submits in `analysis\t09a_sync.txt`. When the send time crossed the main thread's read just after the flip, that frame got 0 ticks and the next one 2.
  - **The fix:** microsecond timeouts, with the last stretch finished by yielding, not by a 1 ms sleep. [patches/09-epoll-precise.patch](../patches/09-epoll-precise.patch), 15 lines in `net_offline.cpp`.
  - **Measured.** The Introduction route from `pace_run.ps1`, with "up" added because Home now opens on Create. Steps ≠ 1 over the frames after the first 60:

    | build | runs | steps ≠ 1 |
    |---|---|---|
    | old timeouts (`LBP3_EPOLL_PRECISE=off`, same build) | 2 | 4.6%, 4.9% |
    | earlier builds today, same display | 3 | 4.4%, 4.5%, 5.0% |
    | precise timeouts (`diag` build, with or without ticket 10's fix) | 7 | 0.4–0.7% |
    | clean patch alone on `local/frame-pacing` (`fix/epoll-precise`) | 2 | 0.5%, 0.5% |

  - **Also tried:** phase-locking the host thread's ~1-frame waits to 10 ms before the next flip (`LBP3_HOST_TICK_PHASE=on`, now opt-in). It added nothing measurable on top: 0.4% and 0.4%.
  - **Not yet checked:**
    - The 240 Hz vblank path, where ticket 06 measured 7–46% with the old timeouts.
    - `camera_check.py`. It reported 10–12 "hitches" in every variant, but the route catches the camera moving in only about 30 of 240 frames (the Introduction's opening shot), so it says nothing here.
    - Step 4 (HITL).

- 2026-11-10, **the human walked the Introduction at 240 Hz with every fix on** (`all` preset, `diag/gpu-checkpoints` `1c80781`, `LBP3_TRACE_SIM=1`).
  - **Walk 1, `t09_hitl1`** (GPU checkpoints on, as `hitl_start.sh` always sets them). The human said "there's little camera stutter when it transitions". The level counted 6,120 flips with steps ≠ 1 at 0.69% (`'0'` 30, `'2'` 10, `'?'` 2). 24 of those are the load's tail (flips 31–54, which the script's settle rule kept this time), so play itself was about 0.3%. The stutters were 0/2 pairs: four in half a second at 72 s, as a tutorial video (`video_acting_narrator.bik`) started, and single pairs at 108, 114 and 117 s next to music streaming reads. Frame work averaged 27.6 ms, against about 24 ms in earlier runs without checkpoints. The log itself was lost (see the tools note below); these numbers come from the analysis at 18:50–18:52.
  - **Walk 2, `t09_nockpt`** (the same, with checkpoints off: `remote_start.ps1 LBP3_CLEAR_SWIZZLE=off LBP3_TRACE_SIM=1`). The human said "still the same". Frame work averaged 24.9 ms. There were 7 single 0/2 pairs from 19 to 72 s, then two longer stalls. At 78 s, in a 5 s window with 14 MB of reads ending in a sound bank, the game made no new frames for about 1 s (game frames 0 for 30 flips) as Newton's dialogue started. At 84 s, right after the story save (`playfart_12_349_24`), it made none for about 2.5 s in a 5 s window with about 50 MB of reads ending in the sound banks, then caught up with 0/2/1 steps. From 84 to 210 s there were no stutters. The last 69 s had zero steps throughout: the game had been idle for 5 minutes while the human read a message, and its attract video (`lbp3_attract.bik`) was playing. That is why the whole log reads 26.6%. (Corrected 2026-11-10: this line first guessed a pause menu.) Log: `analysis\t09_nockpt_stderr.txt`.
  - **Verdict (agent):** ticket 09's constant camera snaps are gone at 240 Hz too: play between transitions is smooth, by the human's feel and by the numbers. The stutter the human felt is load stalls at transitions (tutorial videos, Newton's entrance and its story save), with or without checkpoints. That is gap 10, not this ticket. The drive can't explain it (the dump is on a Samsung 990 PRO NVMe), but the log's read totals cover 5 s across all files, so the read rate during a stall isn't measured: the runtime's read path and the game's own work are both open. Next for gap 10: an F10 profile and a read trace (`LBP3_TRACE_READS`) during one of those stalls. Whether a PS4 also hitches there is unknown; footage of Newton's entrance would tell.
  - **Tools note:** two session logs were overwritten (`t10_hitl1` now empty, `t09_hitl1` holding the start of the next run). Stopping a Monitor with TaskStop does not end `watch_freeze.sh`'s shell. A watcher whose game was relaunched within its 2 s poll never saw the exit. When the next game closed, it copied `remote_stderr.txt` while the new launch was resetting it. `watch_freeze.sh` now follows one PID and keeps its copy as it goes, never overwriting a larger copy with a smaller one, and `hitl_start.sh` kills leftover watchers first.

- 2026-11-10, **the 240 Hz A/B** (`tools/t09_ab.sh t09_intro240 2`: the scripted route into the real Introduction, alternating; `diag/gpu-checkpoints` `1c80781`, vblank pacing on the 240 Hz display, no GPU checkpoints). Steps ≠ 1 over the level only: **precise timeouts 0.30% and 0.44%**, old timeouts (`LBP3_EPOLL_PRECISE=off`) 10.53% and 12.55%, all of them 0/2 pairs. That matches the 165 Hz numbers (0.31%/0.12% against 12.10%/9.76%), so the display doesn't change the result. The level screenshots show the Introduction's opening ("Use the LEFT STICK to move your Sackperson"). Logs: `analysis\pace_t09_intro240_{precise,imprecise}{1,2}_stderr.txt`.

## Answer

**What sets the step count:** the number of ticks the game host thread has sent when the main thread reads them, just after each flip. In paced play, 183 of 190 ticks were sent when the host's 33.3 ms `sceNetEpollWait` timed out, and the rest were catch-up ticks after a receive. The full rule isn't known: unpaced play shows ticks can also be frame-driven (see Comments).

**Why 0 or 2 at a steady 33.3 ms:** the runtime's epoll wait truncated the timeout to 33 ms and overslept in 1 ms polls. The host's tick drifted through the frame and periodically crossed the main thread's read.

**The change:** [patches/09-epoll-precise.patch](../patches/09-epoll-precise.patch) (branch `fix/epoll-precise`), precise epoll timeouts. **In the real Introduction** (`00_craftworld`, 165 Hz display), steps ≠ 1 over the level only (`tools/sim_steps_level.py`) fell from 12.10%/9.76% to 0.31%/0.12%, and camera hitches from 14/17 to 2/2 over the same frames. That meets the 1% target in two runs. (The earlier 4.7% → 0.5% table was measured in the Legend of Bunkum cutscene by mistake; see Comments.)

**Checked at 240 Hz (2026-11-10, Comments):**
- The scripted A/B in the real Introduction: 0.30% and 0.44% with precise timeouts, against 10.53% and 12.55% with the old ones.
- Step 4, the human's walk: the camera is smooth between transitions. The stutter they felt at transitions (a tutorial video starting, Newton's entrance and the story save after it) is the game making no new frames for 1–2.5 s while it loads sound banks. That is gap 10, for the performance milestone, not this ticket.

**Estimate:** 0.25 session to land it with milestone 1. It is a 15-line runtime change with no reverse-engineering risk.

- 2026-11-10, corrections after review:
  - **The tick rule is only partly known.** In the paced trace (`t09a`), 183 of 190 host ticks came right after an epoll timeout and 7 right after a receive from the main thread (catch-up ticks; both threads use socket 64). But the unpaced runs from ticket 06 (21–26 ms frames) mostly took 1 step per frame and ran the game too fast. A purely timeout-driven 33.3 ms tick would have given at least 20% zero-step frames there. So some ticks are frame-driven, and the "why" above holds for paced play only.
  - **Where the leftover 0.5% falls.** In every precise run, the non-1 steps cluster in two places: at the frames just after measurement starts (about 60–67) and around frame 1,120. Both look like load or scene cuts, and they sit in the same place in every run. The rest are 1–3 isolated 0/2 pairs per run. With the old timeouts the pairs were spaced about every 30 frames, the signature of drift. So most of what's left is at loads, not drift.
  - **Phase lock not proven inactive.** Nobody checked that the lock actually engaged (host timeouts at a fixed offset from "flip shown") before recording that it adds nothing.
  - **The 240 Hz vblank path is unverified.** Ticket 06's 7–46% there (old timeouts, real Introduction) is in the range of the 9.8–12.1% measured with old timeouts in the real Introduction at 165 Hz, so the display may matter little. (The 4.7% this line first compared against was the Legend of Bunkum cutscene.) The ladder's milestone 1 check doesn't promise "under 1%" until this passes on the 240 Hz display (2 runs each with `LBP3_EPOLL_PRECISE` on and off).

- 2026-11-10, **correction, and the real measurement:**
  - **The earlier runs were the wrong level.** All of today's earlier "Introduction" runs played the Legend of Bunkum cutscene (`c01_intro`, voice lines `cs01_*`). After the human's play test the Prologue map's cursor starts on that node, and the scripted route's last press played it. Ticket 06's runs loaded `00_craftworld`, the real Introduction. So the 4.7% → 0.5% table above was measured in a cutscene.
  - **Rerun in the Introduction** (`00_craftworld`; the route now presses left on the map; `analysis\pace_t09_intro_*`). Same diagnostic build, only `LBP3_EPOLL_PRECISE` changed, with the camera moving in about 190 of 240 frames:

    | epoll timeouts | steps ≠ 1, level only | camera hitches (`camera_check.py`, same frames) |
    |---|---|---|
    | old (`off`) | 12.10%, 9.76% | 14, 17 |
    | precise | 0.31%, 0.12% | 2, 2 |

    "Level only" counts from the level's `.farc` open, after the first second with no zero-step flip, to the end of the run (about 1,600 flips, 53 s). A first count from boot (9.6/7.7% against 0.7/1.0%) mixed in the Pod, the map and the load. The one-second settle rule was chosen after seeing these logs (without it, run 1 reads 1.85% because the load's tail counts). The first figure measured with the rule fixed in advance is the milestone 1 dry run: 0.25% (04 Comments).

  - These old-timeout numbers match ticket 06's 7–19 hitches on the 240 Hz display, so the display wasn't what lowered today's earlier numbers; the cutscene was.
  - With precise timeouts steps ≠ 1 drop by more than 30× and camera hitches by about 8×, which meets the ticket's "under 1% across two runs" (0.31% and 0.12%, level only). This was on the 165 Hz display; the 240 Hz check was done later (0.30%/0.44% against 10.53%/12.55%, below).
