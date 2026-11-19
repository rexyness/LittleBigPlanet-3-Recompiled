# The Introduction's stalls: which thread holds the frame, and in what (2026-11-12)

Measured for [ticket 15](../issues/15-milestone-5-performance.md) (symptom 2; [audit](audit.md) C2 and C3; [spec](../spec.md) gap 10). Nothing was built or changed: the clone `D:\Recomps\lbp3-recompiled` on `port` `2d92d65` (shadPS4 `31071685`) as built, on the 165 Hz display (timer pacing). Data is in the clone's `analysis\intro_stalls\`; the scripts are in [tools/](../tools/) (listed under Method).

## Answer

**The headline stalls did not reproduce.** The human's 1 s stop at Newton's entrance and 2.5 s stop after his story save (`t09_nockpt`, 10 Nov) did not happen in 5 scripted walks through the same places.
- The walks were on `port` at 165 Hz: four from the Thruster baseline and one from a copy of the human's own save of that evening (`before_t09_nockpt_1852`).
- Within 10 s of the tutorial video (`video_costumes_narrator.bik`) and of Newton's first line (`np_tu01b_01_newton.mp3`), no walk had more than one flip with no game frame. The worst frame there was 50 ms.
- The build, the refresh rate (240 Hz then) and the human at the controls all differ from those walks, and these runs can't tell which of the three matters. So this is "not reproduced", not "fixed".
- The next step is a measurement: a human walk on `port` at 240 Hz with `intro_drive.py stamp`, `LBP3_FRAME_SPLIT=1` and `LBP3_TRACE_SIM=1` (section C).

What the route does show: three kinds of stall. None of them is the runtime's file read path.

1. **Level loads pause the game's world, not the frames.**
   - Frames keep flipping every 33.3 ms, but the game runs no simulation step.
     - Entering the Introduction: 0.70–0.85 s (20–22 flips with no game frame, 5 runs without read tracing; 21 flips on every build since 10 Nov).
     - Leaving it for the Legend of Bunkum after Newton's button and the story save: 0.91–1.11 s (27–31 flips, 2 runs without read tracing; 1.41 s in the 3 traced runs).
   - The main thread draws a still frame in 1.3–4 ms and waits for its slot. The GPU runs 1.3–1.7 ms a frame.
   - The loader reads 146 MB in about 2,880 reads (Bunkum: 143 MB in about 3,150).
   - The read path costs about 16 µs a read, about 50 ms per load, and no read took over 20 ms. The asynchronous I/O thread `SceFiosIO` is idle in 2,983 of 3,000 samples.
   - **Not the read path.** The time is in the game's loader: mostly game code, and the game's own semaphore waits in the three loader threads sampled.
     - Whether those waits are only the game's design, or partly the runtime's wake-up latency, isn't measured: 3 of about 50 threads were sampled.
2. **The first frame of a newly loaded scene is slow, with no compile.**
   - The Pod at boot takes 180–195 ms, or 183–233 ms spread over two frames. The Prologue map (Adventure) takes 191–204 ms, and 326 ms once.
   - The time goes to the command processor recording the frame's first 7 or 13 command buffers (69–122 ms, while main waits), then to the GPU (26–200 ms).
   - **The runtime's side** (the command processor and the GPU; the main thread isn't involved).
     - The mechanism is not confirmed. The candidates are first-use buffer and image uploads (about 100 new images), driver allocation (seen only in the map's window), and the session's 144 volume expansions (the map only).
     - The GPU part isn't split between our work and the game's own.
3. **The boot's 3.0, 2.0 and 2.0 s frames** are the game's own sleeps ([04](../issues/04-milestone-ladder.md), unchanged). So is one 100–157 ms frame of main-thread game code at boot (frame 21–24).

**About the human's two stalls:**
- The "14 MB ending in a sound bank" before Newton is the game streaming his area from `craftworld_001.farc` and `base_001.farc`. The bank is only the last file read in that 5 s log window.
  - In our walks the stream ends about 2 s before Newton's first line.
  - In the human's log it falls in the window that ends with the 31-flip pause, just before his line.
  - So their pause looks like the game waiting for that stream. Why the stream came later for them is unknown: they reached Newton about 13 s later than our walks, not sooner.
- **The 2.5 s after the save has a load's signature, but it isn't shown to be the same load as ours.**
  - Signature: every bank closes, about 52 MB is read, 85 new images appear, and the 64 MB `FindImageFromRange` warning follows, as after every level load in these logs. No farc open is logged there, but file opens are logged once per session (`LogOnce`).
  - What loaded, and why it took about 71 flips with submits 35–41 ms apart, is unknown. Our walks load the Legend of Bunkum at that point instead (0.91–1.11 s, 143 MB).

## Method

**Routes:**
- **Stock:** `tools/pace_run_intro.ps1`, run through [tools/intro_stock.sh](../tools/intro_stock.sh) for timestamps (runs `is_s1` and `is_s2`).
  - It stands at the spawn about 40 s, `camera_check` walks for 12 s, then it idles 20 s.
  - It never reaches the tutorial video or Newton: neither `video_costumes_narrator.bik` nor `np_tu01b_01_newton.mp3` opens in any of the 79 `analysis\pace_*_stderr.txt` logs. So it covers the boot, the Pod, the Prologue map and the Introduction's load only.
- **Extended:** [tools/intro_walk.sh](../tools/intro_walk.sh) with [tools/intro_drive.py](../tools/intro_drive.py).
  - The same menu route, then 12 s for the spawn.
  - Then it holds walk-right (the `move_right` keyboard action, which runs beside the button queue) with a long jump and a hop every 3.6 s.
  - It walks past the tutorial video and Newton until 25 s after the story save, by which time the Legend of Bunkum has loaded.
  - The earlier stick-then-jump-in-place loop (`t09g_walk.sh`) stalled before the tutorial video (run `is_w1`, 200 s).
  - Saves go through `saves.sh`: the Thruster baseline, or `SAVES_BASELINE=analysis/savedata_backups/before_t09_nockpt_1852` (the human's save from just before their walk).

**One clock:**
- `intro_drive.py` stamps every log line with `time.perf_counter()`.
  - Python's `perf_counter` is QueryPerformanceCounter divided by its frequency: it read 25 µs apart from a raw QPC call made right after it. MSVC's `steady_clock` is QPC too.
  - So the log and `frame_split.csv`'s `submit_ms` share a clock, to the 10 ms poll.
  - The cross-check against the runtime: a `[video] N frames flipped` line is stamped a median 39 ms after frame N's submit, which is its flip.
- `LBP3_TRACE_SIM`'s per-flip "game frames" string matches the split's rows with offset 0 in every run: "ms between submits" against the `submit_ms` deltas, with a mean error of 0.25 ms.
- A stall is either a run of flips with 0 game frames, or a flip period over 100 ms.
- Analysis: [tools/intro_stalls.py](../tools/intro_stalls.py) (per stall), [tools/intro_stalls_table.py](../tools/intro_stalls_table.py) (across runs) and [tools/perf_cats.py](../tools/perf_cats.py) (F10 samples by category).

**Runs** (all with `LBP3_FRAME_SPLIT=1`, `LBP3_TRACE_SIM=1` and `LBP3_PACE_LOG=1`):

| Run | Route, save | Also |
|---|---|---|
| `is_w1` | walk (old loop, stuck), Thruster | `LBP3_TRACE_READS=1000000` |
| `is_w2` | walk, Thruster | trace reads, screenshots |
| `is_h1` | walk, human's 18:52 save | trace reads, screenshots |
| `is_f1` | walk, Thruster | trace reads; F10 at the Prologue map's first frame and at the story save; `LBP3_SAMPLE=main fast thread:53` (the Introduction's load) |
| `is_f2` | walk, Thruster | F10 30 s into the walk (control) and at the story save; `LBP3_SAMPLE=SceFiosIO:53` |
| `is_f3` | walk to the spawn, Thruster | F10 at the boot logos (the Pod's first frame) and at `craftworld.farc`'s open (mid-load); `LBP3_SAMPLE=JobManagerWorker0:53` |
| `is_s1`, `is_s2` | stock, Thruster | none |
| `is_w4` | walk, Thruster | none (clean timings) |

**Caveats:**
- **Read tracing lengthens the loads.** `LBP3_TRACE_READS` adds an unbuffered `fprintf` to every read. It lengthens the Introduction's pause from 21 to 29 flips, and Bunkum's from 30 to 40. So the timings come from the untraced runs, and the traced runs serve only to say who read what.
- **F10 perturbs what it measures.** Its all-thread dump suspends every thread for about 0.2 s (frame 2679 of `is_f2`, 124 ms, is that dump), and its sampling slows the threads it samples.
- **F10's coverage is limited.** It samples only `main` and `GpuCommandProcessor` (3,000 samples over about 4.5 s, with no timestamps), and prints only each thread's top 15 host chains.

## The stalls

Per frame split phase, in ms. "Before" is the 5 s before the stall.

| Stall (time from the first frame) | Length | Phase that grew | Main thread (F10) | Reads in the window | Whose |
|---|---|---|---|---|---|
| Boot sleeps, 4.7, 6.7, 8.8 s | 3.0, 2.0, 2.0 s frames | main thread 3,005, 2,004 and 2,003 ms | sleeping (`perf1` of `is_f3`: 1,191 samples in sleep) | none | game (deliberate sleeps, [04](../issues/04-milestone-ladder.md)) |
| Boot frame, about 0.5 s (frame 21–24) | 100–157 ms | main thread 105–150 ms | not sampled | `intro_001.farc` and patch | game |
| Pod's first frame, 9.1 s (frame 70–74) | 180–195 ms in 5 runs; 183 and 233 ms over two frames in the stock runs | patch + copy (command processor on buffers 1–7) 69–86, recording the 8th 21–23, GPU 81–87; main 3.4; no compile | waiting for the graphics queue | FMOD loads `sfxbank_compressed.fsb` (5 MB) | **runtime side** (command processor and GPU; mechanism unconfirmed) |
| Pod load, 9.1 s | 7 flips with no game frame, 0.43–0.46 s (includes the frame above) | the frame above | – | about 10 MB, banks and `base_001` | game (load), plus the frame above |
| Prologue map's first frame, 44.4 s (frame 1124–1147) | 191–204 ms (`is_s1`: 326) | patch + copy (buffers 1–13) 93–102, GPU 77–85 (`is_s1`: 200); main 17–27; no compile | waiting for the graphics queue (`perf1` of `is_f1`) | 33 MB from `base_001`, `craftworld_001` and `patch_001`, 0.3 s before | **runtime side** (115 new images; the session's 144 volume expansions; driver allocation; mechanism unconfirmed) |
| Introduction load, 51.5 s | 20–22 flips with no game frame, 0.70–0.85 s (5 untraced runs); all flips 33.3 ms | none grows. Main 1.24–1.29 (5.2 before), GPU 1.3–1.6 (6.3 before), slot 29.4–29.8 | main (`is_f3`, F10 mid-load): waiting for the graphics queue 2,517 of 3,000, game code 241, write-tracking faults 68. `main fast thread`: 163 in game code, 20 in `ReadFile`, the rest semaphore waits | about 2,880 reads, 146 MB in about 1 s: `main fast thread` 1,940 / 78 MB, `SceFiosIO` 245 / 63 MB, FMOD 650 / 6 MB; no slow read | **the game's loader**, not the read path |
| Newton's entrance, about 116 s | none (one 0/2 pair, one 50 ms frame) | none | – | 14.5 MB, mostly `main fast thread` streaming `craftworld_001` and `base_001` at 108–114 s | – |
| Tutorial video (`video_costumes_narrator.bik`), about 91 s | none (worst 33.8 ms) | none | – | 9–13 MB, the video and `base_001` | – |
| Story save and the Legend of Bunkum's load, about 192.5 s | 27–31 flips with no game frame, 0.91–1.11 s (`is_w4`, and `is_f2` with F10); all flips 33.3 ms | none grows. Main 3.7–4.2, GPU 1.4–1.6, slot 26.4–27.1 | waiting for the graphics queue 2,367–2,415; game code 280–283; write-tracking faults 76–77 (as when walking) | about 3,150 reads, 143 MB: `SceFiosIO` 445 / 78 MB, `main fast thread` 1,570 / 58 MB, FMOD 840 / 6 MB, a job worker 250 small reads; no slow read | **the game's loader**, not the read path |

## A. Level loads: the game pauses its world while its loader works

**Frames:**
- Throughout both pauses the game submits and flips every 33.3 ms. So no flip-interval trigger can catch them, and the frame split shows no phase growing.
- What changes is that the game runs no simulation step: `GetGameFrame` doesn't advance.
- The main thread's own time is 1.3 ms a frame in the Introduction's pause (5.2 ms before) and 3.7–4.2 ms in Bunkum's (3.3–3.4 before). The GPU's drops to 1.3–1.7 ms (6–7 before). The game draws a still frame and waits.

**The load:**
- Readers:
  - The Introduction's load reads 146 MB in about 2,880 reads (the same in all 4 traced runs, ±10 reads).
  - `main fast thread` does 1,940 reads of 40 KB on average, through `libSceFios2`, which calls `sceKernelPread` inline.
  - `SceFiosIO` does about 245 reads of 257 KB.
  - The FMOD nonblocking thread reloads the sound banks (about 650 small reads).
- How busy the threads are during the pause:
  - **`SceFiosIO` is idle.** Of 3,000 samples over the 4.5 s around the pause, 2,983 are in its condition-variable wait; only 9 are in `ReadImpl` and 5 in open or stat (`is_f2`).
  - **`main fast thread`** (`is_f1`, traced): 163 samples in game code, 119 in the trace's `fprintf`, 20 in `ReadFile`, and the rest in `sceKernelWaitSema` (`eboot+0x210b8`/`0x210d8`, the game's semaphore wrapper).
  - **`JobManagerWorker0`**: 142 of 3,000 samples in game code, the rest in semaphore waits (`is_f3`).
  - **The all-thread snapshot mid-load** (`is_f3`, F10 at the farc open) found only `JobManagerWorker3` in game code (`eboot+0x9a426d`); `main fast thread` and `respump thread` were at waits.
  - None of the three loader threads sampled is saturated. Apart from the waits, whose host chains go through the runtime's `sceKernelWaitSema` or `scePthreadCondWait`, the only runtime frames in them are reads, opens, and in `main fast thread` the trace's `fprintf`. That is 3 of about 50 threads, over 4.5 s windows around a pause of under 1 s.
  - `main fast thread` spends about half its time in the game's semaphore waits. Whether that is only the game's design (a chain of work across threads, perhaps paced by frames) or partly the runtime's wake-up latency in `sceKernelWaitSema` isn't measured.
- The read path:
  - `ReadFile` plus the copy costs about 16 µs a read for 40 KB from the cache. That is 20 samples × 1.5 ms over 1,940 reads; for `SceFiosIO`, 63 MB in about 13 ms.
  - So the whole path is about 45–60 ms per load, an upper bound if all of it is on the critical path.
  - No read took over 20 ms (`[fs] slow read` never printed, in 9 runs).

**Per-read cost does reach the pause.**
- The trace's `fprintf`, about 90 µs on the reading threads, lengthened it by 8–10 flips (0.27–0.33 s for 2,900–3,150 reads).
- So making `ReadImpl` cheaper could save at most the ~50 ms it costs now, about 7%.

**The same on every build:** the Introduction's pause is 21 flips with no game frame in the human's Nov 10 walk (`t09_nockpt`), in the three pre-milestone-5 runs (`pace_iab_pre1`–`3`), in the three merged runs (`pace_iab_m51`–`3`), and 20–22 now. That shows only that the GPU-side changes since (the flush, the graphics-only wait) didn't move it.

**Against a PS4 (an estimate, not measured):** 143–146 MB from a PS4's hard disk, at a typical 60–100 MB/s sequential, would take 1.5–2.5 s. These loads take 0.7–1.1 s here.

## B. The first frame of a new scene: the command processor and the GPU

**Which frame:**
- The slow frame is always the first full frame of the new scene: the flip goes from 1 command buffer to 8 (the Pod) or 14 (the Prologue map).
- Its split:
  - **Patch + copy, 69–102 ms.** This equals `first_to_last_enq_us`: the command processor recording buffers 1 to N−1 while each `SubmitGfx` waits for the graphics queue.
  - **Recording the last buffer:** 21–23 ms (the Pod), 0.6–1.0 ms (the map).
  - **The GPU's `Finish`:** 76–89 ms, against about 6 normally; 200 ms once (`is_s1`).
  - **No compiles:** 0 pipelines and 0 shaders in every run, so not [compile_stalls.md](compile_stalls.md).
  - **Main thread:** 3–27 ms.
- In the stock runs the Pod's work was split over two frames: 107–122 ms of patch + copy in the first, where the main thread is released before the flip, then a 26–62 ms `Finish` in the next.
- Status lines:
  - The Pod's window has 89–93 new images.
  - The map's has 114–115 new images and the session's 144 volume expansions (audit C2).
  - The Pod's frame costs the same without expansions, so the expansions are not the bulk.

**What the command processor does: a lead, not a finding.**
- `GpuCommandProcessor` samples were taken around the Pod's frame (`is_f3` `perf1`, whose window is otherwise the idle boot sleep) and the map's frame (`is_f1` `perf1`).
- Their top host chains show:
  - `BufferCache::SynchronizeMemory` → `MemoryManager::CopySparseMemory`, copying guest memory into GPU buffers (vertex and dispatch buffers): 42 and 22 samples;
  - `TextureCache::RefreshImage` → `ObtainBufferForImage` → `CopySparseMemory`, image uploads: 9 samples (the Pod's window);
  - fence waits while the GPU runs: 116 and 60.
- **Only one thing is specific to the map's window: driver allocation churn**, `DestroyAllocation2` 28 and `D3DKMTCreateAllocation` 8. The walking control (`is_f2` `perf1`) has none.
- **The upload chains are no higher than in that walking control** (texture cache 38, buffer cache 25, other recording 36).
- **Coverage is thin:**
  - About 850 of each window's 3,000 samples fall outside the 15 chains the report prints.
  - The sampled Pod frame was itself shortened by F10 (147 ms, with 12 ms of patch + copy).
  - The sampler has no timestamps, so the one slow frame can't be isolated from the 4.5 s window.
- So the command-processor time is unattributed. The candidates are first-use buffer and image uploads (about 100 new images), the driver creating and destroying allocations, and, for the map, the 144 volume expansions.
- **The GPU part (`Finish`, 76–200 ms) is unsplit.** It could be our uploads and detiling, or the game's own first-frame GPU work.
- On a PS4 the GPU reads loaded data in place, so whatever upload share there is would be the port's alone.
- Both frames happen once per scene entered. Second visits to these two scenes weren't measured.

## C. Newton's entrance and the tutorial videos

**What our walks saw:**
- From both saves, the walks reach the costume tutorial video about 28 s into the walk, and Newton about 52 s in. His five lines, `np_tu01b_01` to `_09_newton.mp3`, play over 23 s.
- **The tutorial video:** the worst frame was 33.8 ms (`is_w2`, 88–96 s) with no zero-step flip.
- **Newton's entrance:** one 0/2 pair and one 50 ms frame (`is_h1`, 110–121 s).
- **Across all 5 walks** (`is_w2`, `is_h1`, `is_f1`, `is_f2`, `is_w4`), within 10 s of the video's open and of Newton's first line: at most 1 flip with no game frame, and a worst flip of 50 ms. `is_f2`'s 124 ms frame at 96 s is its own control F10 dump.
- The 14.5 MB window "ending in `sfxbank_compressed.fsb`" is `main fast thread` streaming 6 MB of `craftworld_001.farc` and 5 MB of `base_001.farc` at 108–114 s. It finishes 2 s before Newton appears.

**The human's walk** (`t09_nockpt`, 10 Nov, the build of ticket 09's walk, 240 Hz, the same save):
- **At Newton, 31 flips with no game frame.**
  - The 14 MB stream falls in the 5 s log window that ends with this pause, just before Newton's first line. In our walks it ends about 2 s before that line.
  - So their pause looks like the game waiting for that stream.
  - They reached Newton about 13 s later than our walks (77 s after the level-load pause, against 64 s), so it isn't that they arrived early. What made the stream late is unknown: the route through the level, the Nov 10 build, or 240 Hz.
- **After the save, 51 + 22 flips.**
  - Their log then shows every bank closing, 52 MB of reads, 85 new images and `FindImageFromRange ... size=0x4000000`, which in every one of our runs follows a level load (`is_f2` line 866, `t09_nockpt` lines 897 and 1334).
  - So it has a level load's signature. No farc open is logged there, but `kernel_fs.cpp:100` logs each path once a session, so a reopen wouldn't show.
  - It isn't shown to be the same as our load. It took about 71 flips for 52 MB, with submits 35–41 ms apart. Our walks' Legend of Bunkum load takes 27–31 flips for 143 MB, with flips at 33.3 ms and main idle. What loaded is unknown.
- `t09_hitl1`'s "four 0/2 pairs at 72 s" came at `video_acting_narrator.bik`, which our walks didn't trigger.

## Fix candidates, ranked

1. **The first frame of a new scene (runtime side; 180–330 ms once per scene entered, at the Pod and the Prologue map on this route): instrument, then choose.**
   - **Instrument first:** per frame on the `[perf]` line, the time and bytes of buffer-cache and texture-cache synchronisation, image creation, and driver allocations; plus GPU timestamps around the uploads inside `Finish`.
   - That tells which of the candidates in B holds the 69–122 ms of command-processor time, and how the 76–200 ms of GPU time splits between our uploads or detiling and the game's own work.
   - **If it is uploads,** the level-load pause before the frame leaves the command processor and the GPU nearly idle for 20–30 frames (GPU 1.5 ms a frame), after the game has written the data. Doing the synchronisation in that pause rather than at first use (eager uploads of written regions while the queue is idle, or a transfer queue) could hide most of it, with no change in game behaviour.
   - **If it is allocation churn,** pooling or suballocating the new images and staging buffers would be the fix.
   - This is the only measured stall on the route that is on the runtime's side, which is why it ranks first. Its size is about 150–300 ms, twice per session on this route.
2. **Nothing for the level-load pauses beyond small change.**
   - The runtime's share is the read path, at about 16 µs a read, 45–60 ms per load at most (≤ 7%), and write-tracking faults, which are no higher than when walking (main: 76–77 of 3,000 samples in a load against 95 walking).
   - A cheaper `ReadImpl` could take off tens of ms: no `QueryPerformanceCounter` pairs per chunk, and no bounce copy when the destination isn't write-protected. Not worth it on its own.
   - Keep `LBP3_TRACE_READS` off when timing anything.
3. **Write-tracking faults on the main thread every frame** (for symptom 1, not a stall).
   - In every F10 window, loading or walking, `main` spends 68–95 of 3,000 samples in `GuestFaultSignalHandler` → `BufferCache::InvalidateMemory` → `UpdatePageWatchersForRegion` → `VirtualQuery`/`VirtualProtect`, including contention on the region manager's mutex.
   - That is about 1 ms a frame (while walking, 95 samples against 275 in game code).
   - Batching re-protection, or caching the committed-range query (audit B, item 15), would cut it.
4. **Newton and the story save: not reproduced, so nothing to fix yet. Measure next.**
   - Have the human walk on `port` at 240 Hz at their own pace, with `tools/intro_drive.py stamp` running and `LBP3_FRAME_SPLIT=1` and `LBP3_TRACE_SIM=1` set. Add `LBP3_TRACE_READS=1000000` on a second walk if a pause shows: it costs about 90 µs a read.
   - A zero-step pause with frames flowing means the game is waiting for its stream or load (section A). A long frame means section B's kind.

## What stays open

- **The level load's critical path across the game's threads.** The three threads sampled aren't saturated. Sampling `main slow thread`, `respump thread`, `ResLocateThread` and the `Streaming Deity` threads would say whether the game paces loading by frames. Timing `sceKernelWaitSema` wake-ups would say whether the runtime adds latency to the semaphore waits.
- **What the command processor does in a scene's first frame, and how its 76–200 ms of GPU time splits** (fix 1's instrumentation), and why `is_s1`'s map frame ran 200 ms on the GPU.
- **Whether the human's two stalls reproduce on `port`** at 240 Hz with a human at the controls, and if so, what loaded after the story save.
