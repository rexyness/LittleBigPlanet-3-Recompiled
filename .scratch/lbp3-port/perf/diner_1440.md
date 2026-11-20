# 1440p over budget in the Diner and the Pod (2026-11-13)

Follows finding 1 of [log_audit.md](log_audit.md). Phase 1 is offline only: the human's logs plus a little decompilation, with nothing run in the game. Labels: **measured** (counted in logs or timed), **decompiled**, **inferred**.

Scripts are in the job's tmp folder `diner\`:
- `win.py`: one row per `[video]` window, with work, wait, over-40, early releases, gfx blocked, compiles, new images, fills, frame-generation GPU time, protect count and timed-lock samples;
- `lvlsum.py`: one row per level visit;
- `lockbench.cpp`: a timed-lock cost benchmark.

## What the logs show

### The Diner: the same stretch is fine at 1080p and heavy at 1440p (measured)

Both sessions played the same part of the Diner, right after Marlon's quest lines `tt_q01_qh90/qh100`. Each row covers the next 150 one-second windows from that dialogue:

| Session | Work median | p90 | max | Windows over 33.3 ms | Frames > 40 ms | Timed-lock samples | Early releases |
|---|---|---|---|---|---|---|---|
| 1080p + frame gen, `play_fg_20261112_1458.txt` from line 9703 | 18.2 | 22.5 | 24.2 | 0 | 0 | 58 | 0 |
| 1440p + frame gen, `play_1440_log.txt` from line 31902 | 23.6 | **43.4** | **88.9** | **40** | **611** | 228 | 162 |

At 1440p the first ~80 windows stay at 18–28 ms. From line 32,703 on, work jumps to 36–89 ms for about 1.5 minutes, then falls back to about 19–20 ms. The human's actions after the dialogue may have differed between the two sessions, so "same stretch" is the level position, not proven identical gameplay (inferred).

**In the heavy 1440p windows:**
- **Absent:** compiles, new images and texture uploads (0); `[fills]` is unchanged (3.0 refreshes, 64 cleared); no 1440p shader logged as unlisted.
- **Graphics queue:** blocked 0 ms in most heavy windows. The exceptions are the fluid frames: the `[protect]` count is 40–57k against about 13k elsewhere, gfx blocked is 2–4.6 ms, and 10–31 early releases, all from learned ("known") addresses. At 1080p the same level's fluid frames had **no** early releases. So at 1440p one fluid wait went over the 20 ms grace once and its address was learned; after that every fluid frame released early (inferred from how `EndBlockedWait` works).
- **Frame generation's own GPU pass** took 2.5 ms on average in heavy windows against 1.6 ms in light ones, in both 1440p sessions. At 1080p it was 1.8 against 1.6. A fixed-size pass slowing down hints at a busier GPU (inferred).
- **Timed-lock samples:** 3–16 per window against 0–1 in light ones. At 16 per window, the game's main thread made about 1.6 million timed-lock calls that second.

### Heavy windows across all sessions (measured)

Windows over 33.3 ms with no compiles and fewer than 5 new images:

| Session | Heavy windows | Timed-lock samples per window, heavy / light | Heavy windows with gfx blocking |
|---|---|---|---|
| 1080p + frame gen (90 min) | 14 | 3.6 / 0.8 | 0 |
| 1440p + frame gen (76 min) | 83 | 2.3 / 0.45 | 9 |
| 1440p, audio build | 13 | 6.4 / 0.3 | 0 |
| 1440p + DLC | 1 | 0 / 0.4 | 0 |

Every session shows the same signature: heavy frames come with a lot more main-thread lock polling, and seldom with graphics-queue blocking. That includes the 1080p Bite Size slowdown already in the audit.

### What the polled locks are (decompiled)

- **`eboot+0x2152d`** is inside `FUN_00021500(obj, state)`. It takes `obj+0x70` with a 2 s timed lock, retrying forever on `ETIMEDOUT`. Then it counts the entries of an array (stride 0x28) whose field `+8` equals `state`, unlocks, and returns the count. That's a "how many jobs or items are in this state" query, with 47 call sites (`xrefaddr 21500`). A main thread calling it about a million times a second is waiting for work on other threads to finish, i.e. busy-polling.
- **`eboot+0xc3b8d`** is `FUN_000c3b60`: it links an object into a global intrusive list under the global mutex `DAT_012c9070`, a reference or handle registration. `0xc3c3d` is most likely the matching unlink.

### What a timed lock costs in our runtime (measured, `lockbench.cpp`)

The benchmark runs a copy of the `scePthreadMutexTimedlock` path: the shared sample counter, `Clock::now`, SRW try-acquire, then `SwitchToThread` on contention.
- **Uncontended:** about 23 ns per call. Even 1.6 million calls a second costs about 37 ms per second, roughly 1.2 ms per frame.
- **Contended:** the shared sample counter and contention on one mutex add about 20–100 ns per call with 2–8 threads on an idle machine.
- **Not measurable offline:** how much `SwitchToThread` loses in game, where other threads are runnable.

So the polling **is a symptom, not the cause** (inferred). The main thread waits for something, and the call rate rises with the wait.

### The Pod after the Diner (measured; a different pattern)

- **The stretch:** from line 34,192, work is 33–46 ms for about 40 windows, then 17–20 ms again from line 34,833.
- **What's absent:** timed-lock samples stay at about 1 per window, so no rise, and there's no graphics blocking.
- **What happened then:** that's when the costumes menu first opened. It showed 14 new pipelines and 44 new images, and played the tutorial video `video_costumes_narrator.bik` (opened at line 34,300, 14 MB). Bink decodes on the CPU through the job system (NOTES.md).
- **The same Pod in other 1440p sessions:** p90 25 ms (`play_1440a_log.txt`), and the DLC session's costume browsing at 1440p had 1 heavy window.
- **So most likely a one-off,** since the tutorial video plays only the first time (inferred). The 353 early releases in this visit come later, in fluid frames (lines 34,932–35,030), and work there is normal (18–20 ms).

## Hypotheses, ranked

1. **The GPU is over budget at 1440p in this Diner section** (most likely).
   - **For:** the same section is clean at 1080p (max 24 ms) and 36–89 ms at 1440p. Frame generation's fixed-size GPU pass slows from 1.6 to 2.5 ms in heavy windows. Fluid waits cross the 20 ms grace only at 1440p.
   - **Mechanism:** the GPU spends 1.78× the pixels on full-screen passes (fog, volumetric light, DOF, bloom, SMAA, the stacked 2560x2880 targets). The game's job threads, which round-trip with the GPU for fluid, wait longer, and the main thread busy-polls for them.
   - **Not known:** which passes, since there's no GPU timing in these logs.
2. **CPU job-thread load or starvation in this section.** The main thread's lock polling points at waiting on jobs. At 1440p the CPU side also does more per frame: frame generation's readback of two 14.7 MB frames and 60 window uploads a second. Lane C measured +0.7 ms on the main thread at 1080p, so it can't explain 50–90 ms on its own (inferred).
3. **Early-release churn.** Once a fluid wait address is learned at 1440p, every fluid frame releases the game early. Whether that makes the following frames slower (the game racing ahead, then waiting) is open. It affects fluid windows only.
4. **Pod: the first-time costume tutorial video** (Bink on the CPU) plus new menu content. A one-off; check that it doesn't recur.
5. **Ruled out by the logs:** pipeline compiles, texture uploads, the render-target fills, unlisted 1440p shaders (none logged) and the lock-call cost itself (benchmark).

## Phase 2 plan, once the game is free

1. **Route.** Install a copy of the human's profile (`analysis\savedata_backups\human_play_20261113_0046`) as test saves via `saves.sh` (`use_baseline`); never touch the live saves.
   - Script Pod → map → Manglewood → the Diner, using screenshots to navigate.
   - Walk to Marlon, let `qh90`/`qh100` play, then follow the path for about 3 minutes, holding right with jumps.
   - If the heavy stretch doesn't show up, find what the human did from the log's file and dialogue events after line 32,700. Recording the window position at which the heavy part starts also helps.
2. **Arms,** alternated with two runs each, all with `LBP3_FRAME_SPLIT=1` (CPU game, record and GPU phases plus GPU timestamps):
   - 1440p with frame generation;
   - 1440p without;
   - 1080p with;
   - 1080p without.
   If 1440p is the heavy one, add 1440p with `LBP3_RES_SHADERS=0`, and 1440p with early releases off (the learned-address switch), to test hypothesis 3.
3. **Extra counters,** cheap and diagnostics only:
   - timed-lock calls per window by caller (`0x2152d`, `0xc3b8d`, `0xc3c3d`), with the contended count and `SwitchToThread` count;
   - learned wait addresses with their first-learn time.
4. **If GPU-bound,** run one traced frame in the heavy stretch (`fg_passes`-style per-draw GPU timestamps) to find the passes that grow with resolution, and fix those by sizing them for 1080p-equivalent cost, behind a switch, with before/after screenshots at 1440p. If one pass dominates and can't be fixed cleanly, tell the human the Diner is heavy at 1440p and that `RES=1080` is the fallback.
5. **If CPU-bound,** sample the job threads' CPU time per window, and see whether frame generation's CPU side (readback copies, uploads) or the game's own jobs take the time.

Rules for the run:
- use a shader-cache copy via `LBP3_SHADER_CACHE_DIR`;
- use the 165 Hz display;
- clean up after interruptions with `tools/stop_cleanup.sh`;
- the human's saves must end identical to `human_play_20261113_0046`.

- 2026-11-13, **phase 2 stopped at the human's request** ("feel free to stop") before any results. The test game was killed and the human's saves restored by `human_restore`: identical to `human_play_20261113_0046`. The clone's exe is unchanged (`d5d335ad…`, `port` `34e3e8d`), and the shader cache is untouched. Resume from the phase 2 plan above.
