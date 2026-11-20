# Frame generation, lane C: the present path (2026-11-12)

The human asked for frame generation ("give it a try with frame gen", after [fps60.md](fps60.md)). This lane builds the plumbing with a trivial generator, so the present path is proven before a real generator (a motion-vector warp, then FSR 3.1) plugs in. It is opt-in (`LBP3_FRAMEGEN=1`), lives on branch `exp/framegen`, and isn't merged into `port` until the human has seen it.

- **Branches:**
  - runtime `exp/framegen` `7f86658` on `port` `0570df8` (worktree `D:\Recomps\lbp3-wt\fgp`);
  - shadPS4 `exp/framegen` = `d5af2ae7`, no changes (worktree `D:\Recomps\lbp3-wt\fgp-shadps4`).

  Both are pushed to the forks.
- **Files:** `runtime/src/video/framegen.{h,cpp}` (new), `video/presenter.{h,cpp}`, `hle/videoout.cpp`, `tests/framegentest.cpp` (new).

## How it works

```
game flip N ──► CP thread: readback ─► WaitFlipSlot ─► hand over ─► SignalFlip     (unchanged order)
                                                          │ (a buffer swap, as PresentPixels)
                                                          ▼
                 worker: decide ─► generate G(N-1, N) ─► queue G (due slot + D)
                                                      └► queue N (due slot + D + P/2)
                 presenter, at each refresh: show the latest frame due by the next refresh
```

- **Display times:**
  - **P** is the game's flip period: 33.3 ms at its 30 Hz.
  - **D** is the delay that leaves room to make G: one refresh + 4 ms by default, or `LBP3_FRAMEGEN_DELAY_MS`.
  - Without frame generation a real frame is handed to the presenter at its slot and goes out one or two refreshes later. With it, N goes out about D + P/2 later than that, and G takes N's old place.
  - Expected extra latency for real frames: ~P/2 + 4 ms, about 21 ms (measured below).
- **The presenter:**
  - At each refresh it takes every queued frame due by `wake + refresh − 1 ms` and shows the latest.
  - A frame due between two refreshes goes out on the first refresh after its due time.
  - The ImGui overlays (keyboard, dialogs) are drawn on every present, so they appear on both G and N.
- **When no G is made** (only N is shown, and the screen holds N−1 until then):
  - the first frame, or the frame size changed;
  - the game's own 60 Hz (the boot loading screen);
  - a late frame or a load: the slot is not within P ± P/2 of the previous one;
  - the game ran 0 or 2 simulation steps for the frame;
  - a scene cut: mean B/G/R difference over a sparse 8×8 grid above `LBP3_FRAMEGEN_CUT` (default 40);
  - the generator declined;
  - G was ready too late: it would land on N's refresh.
- **Generators** (`LBP3_FRAMEGEN_MODE`):
  - `blend`, the default: a 50/50 rounded average of N−1 and N (SSE2, about 2 ms for 1080p on the worker; measured below). It ghosts on motion. It is only there to prove the path.
  - `repeat`: N−1 again. The window shows 30 distinct frames at the 60 Hz cadence, which is useful for timing checks.
  - The interface (`GenInputs`) also has slots for the motion vectors, depth and HUD-less colour that lane A is locating.
- **Status line** every 30 flips:
  > [framegen] blend: N real in, N generated (N a refresh late), N dropped (first, size, rate, late, steps, cut, generator, gen-late); gen avg/max ms; cut diff avg/max; shown N real + N generated, N skipped
- **Timing log:** `LBP3_FRAMEGEN_LOG=1`, with frame generation on or off, writes `analysis\framegen.csv`, one row per new frame on screen: kind (R off, N, G), slot, due, queued, present and scan-out times.
  - `D:\Recomps\lbp3-wt\_tools\fg_score.py` turns it into cadence (intervals in refreshes) and latency (slot to scan-out).
  - `fg_runs.sh` runs the measurements below, with the exe swapped into the clone and put back by trap with an md5 check.

## The game side doesn't move (code reasoning)

- **Off (the default):**
  - In the flip interrupt, the only new code before `SignalFlip` is one `Enabled()` check, a function-local static bool.
  - In the flip submit, `FrameGenSimSteps()` returns at the same check without reading anything.
  - `PendingFlip` has one more field.
  - The presenter's loop checks a flag that only `PresentTimed` sets, reads the clock once per refresh, and keeps the same upload rule.
  - `PresentPixels` reads the clock only with `LBP3_FRAMEGEN_LOG=1`.
  - The game sees the same code.
- **On:**
  - **Flip interrupt:** `WaitFlipSlot` (its slot, `NoteFlip` for the flip-anchored clock) and `SignalFlip` (the flip event) run unchanged and in the same order. Between them, `SubmitReal` (a buffer swap under a lock and a notify) replaces `PresentPixels` (a swap under a lock and its stats).
  - **Flip submit:** the main thread calls `GetSimFrame__` once per flip, the same read-only call `LBP3_TRACE_SIM` makes.
  - **No copies on the game's critical path:** the worker copies and generates on its own thread. The presenter now uploads 60 frames a second instead of 30, on its own thread through SDL's D3D11. That is the one shared cost: the GPU and the driver.
  - **No writes to the game's images:** generators read host copies of the display buffer, which the readback already made. No second readback, nothing new in the texture cache. A GPU generator (FSR) will add one readback per generated frame; the frame-split columns `CopyEnd` and `SlotEnd → Flipped` are where it shows.
  - **The early-flip gate and `PatchFlipRequest`** are untouched.
  - **Pacing caveat:** vblank counting comes from the DXGI vblank thread (the default `LBP3_PACING=vblank`), not from the presenter, so its extra presents don't move flip slots. With `LBP3_PACING=present` (upstream's mode) the presenter counts the vblanks, and frame generation would then double the counted rate. Not supported there.

## Offline checks

- Built clean (clang-cl, RelWithDebInfo).
- `framegentest`: 66 checks, 0 failed, five runs in a row. It covers:
  - Blend at every size, both generators, and the cut difference (alpha ignored);
  - every drop rule and the delay defaults;
  - the presenter queue: sorted insert, due selection, skips, the 8-frame cap, the spare-buffer pool;
  - the worker end to end on the real clock: A first → only N; B → G = blend, due at slot + D, then N at + P/2; C 2 steps → no G; D cut → no G; E late → no G; F handed over 20 ms late → gen-late.
- The six offline test programs pass: `cooptest` 81, `dlgtest` 154, `osktest` 263, `dialogtest` 368, `jpegtest` 157, `imetest` 403; 0 failed.

## In-game measurements (2026-11-12 11:53–12:18, 165 Hz display, blend generator)

Run with `D:\Recomps\lbp3-wt\_tools\fg_runs.sh` on the `exp/framegen` exe (`7f86658`), swapped into the clone and put back by trap with an md5 check. The settings were `LBP3_TRACE_SIM=1 LBP3_FRAME_SPLIT=1 LBP3_FRAMEGEN_LOG=1` plus `LBP3_FRAMEGEN=0|1`, with on and off alternating. The data is in the clone's `analysis\framegen\` (`<tag>_stderr.txt`, `_split.csv`, `_fg.csv`, `summary.txt`) and in `analysis\ab_fgth_{1,2,3}_stderr.txt`.

**Cadence and latency**, measured by `fg_score.py` on `framegen.csv`. The Introduction window is from 60 s; the Needlepoint window is its last ~43 s (the walk and the standing at the basket).

| | new frames on screen | interval between them (refreshes of 6.06 ms) | real frame: slot → scan-out |
|---|---|---|---|
| Introduction on (2 runs) | 59.9/s, half G | 2: 24.9%, 3: 74.9%, 5 or more: 0.1–0.2% | 30.4 / 30.9 ms (p95 33.7) |
| Introduction off (2 runs) | 30.0/s | 5: 50%, 6: 50% | 8.9 / 9.2 ms (p95 11.9) |
| Needlepoint on (2 runs) | 59.9 / 60.0/s | 2: 24.9%, 3: 74.7–75.0% | 30.5 / 31.0 ms |
| Needlepoint off | 30.0/s | 5: 50%, 6: 50% | 9.2 ms |

- **Cadence:** with it on, the window shows 60 new frames a second in the 3/3/3/2 refresh pattern that 60 on 165 Hz allows (mean 16.70 ms). Without it, 30 frames alternate 5 and 6 refreshes.
- **Latency:** real frames reach the screen **+21.5 ms later** (P/2 = 16.7 ms plus the 4 ms generation budget, within the refresh quantization).
- **Generated frames** go out 14.0–14.4 ms after the slot.
- **No frame went out before its due time.** Due → scan-out averages 3.7–4.2 ms, at most 7.2 ms, which is the refresh quantization.

**Generated per real frame, and why not** (the `[framegen]` lines summed over the whole run, boot included):

| run | real | generated | drops |
|---|---|---|---|
| Introduction on 1 / 2 | 3,030 / 3,030 | 96.4% / 96.0% | rate 21 (boot), late 9/11, steps 78/87, first 1, cut 0 |
| Needlepoint on 1 / 2 | 3,600 / 3,600 | 96.9% / 97.0% | rate 21/20, late 11/11, steps 79/76 |
| Thruster runs 1–3 | 2,490–2,520 | 92.6–93.5% | steps 132–152, late 6–12 |
| photo | 1,800 | 94.9% | steps 59, late 8 |

- **Generation time** with blend: 1.42–1.62 ms on average, at most 5.49 ms.
- **Dropped as too late:** none.
- **Shown a refresh late:** 0–8 per run.
- **Skipped by the presenter** (two frames due by the same refresh): 3–10 per run.
- **The cut rule never fired.** The large differences (up to 94) came on frames already dropped for their 0/2 steps or a late slot. See the follow-ups below.

**The game side** (frame split; `sim_steps_level.py` for steps ≠ 1):

| | steps ≠ 1 in the level | flips | hand-over (SlotEnd → Flipped), mean / p95 |
|---|---|---|---|
| Introduction on 1 / 2 | 0.26% / 0.26% | 33.33 ms | 0.05 / 0.02 ms |
| Introduction off 1 / 2 | 0.39% / 0.13% | 33.33 ms | 0.23 / 0.96 ms |

- **Steps ≠ 1 and the flips** are unchanged. The hand-over is cheaper than `PresentPixels`; one 10.8 ms outlier in one run is unexplained.
- **Readback** stays 0.70–0.80 ms, and **GPU submit + run** 4.2–4.8 ms, either way.
- **The game's main thread is about 0.7–1.0 ms slower in the heavy level with it on.** This is the cost.

  | Needlepoint, last ~38 s | main thread | patch + copy | walk work | standing work | walk frames > 40 ms |
  |---|---|---|---|---|---|
  | on 1 / 2 | 6.18 / 5.90 ms | 6.96 / 6.68 ms | **20.58 / 20.04 ms** | 18.31 / 18.12 ms | 3 / 0 |
  | off 1 / 2 | 5.19 / 5.25 ms | 6.72 / 6.60 ms | **19.17 / 19.19 ms** | 17.59 / 17.36 ms | 0 / 0 |

  - The ranges don't overlap (measured).
  - In the Introduction the main thread was 6.10 / 6.28 ms on against 5.53 / 6.07 ms off, within noise.
  - **Likely cause (inferred, not separated):** memory-bandwidth and cache contention. The worker blends and copies about 33 MB per real frame, and the presenter now uploads 60 frames of 8 MB a second instead of 30, while the main thread runs.
  - A GPU generator would drop the CPU blend and copy; a Vulkan presenter would drop the uploads.
  - The three frames over 40 ms were in one of the two runs with it on; the other had none.

**Regression with it on:**
- **Thruster recipe:** 3 of 3 OK, with no device loss and no stuck wait.
- **Photo** (`t_m2v_photo.sh`): encoder PASS, capture readback PASS, 1 JPEG in the save.
- **`[fills]` per frame is identical on and off:** 3.2 refreshes (31.7 MB), 57.4 cleared, 1.5 pre-syncs and 5.4 skipped in Needlepoint. The texture cache is untouched.
- **GPU warnings:** 0. **nvlddmkm events 11:53–12:18:** 0.

**Measurement caveat:** `camera_check.py`'s hitch count (`pace_run_intro.ps1`) isn't valid with it on. The frame dump now includes the blended frames, so "2 hitches in 1 frame moving" and "0 frames moving" are artifacts. Use the steps ≠ 1 count and `framegen.csv` instead.

**State left:** the clone is on `port` with its own exe (md5 `0dfda1b9…`, restored by trap). No game, test marker or kept copy is left behind. The human's saves are identical to `human_play_20261111_2104`.

## Follow-ups

1. **The cut rule (from lane A, perf/fg_inputs.md):**
   - Detect camera cuts from the main view matrix at `DAT_013a2a60+0x20` at submit: a jump of 5,753 units, against at most 250 normally. The game's own motion-blur clear comes one frame late.
   - Treat loading screens (frames with no composite draw) as "don't generate".
   - The image-difference rule can stay as a backstop.
2. **Latency:**
   - The delay D (one refresh + 4 ms) could be one refresh + 2 ms: blend averages 1.5 ms, but its max of 5.5 ms would then sometimes drop as gen-late.
   - With a GPU generator, re-measure the generation time first.
   - Real frames cost +21.5 ms now.
3. **Main-thread cost** (+0.7–1.0 ms in Needlepoint): measure it with `LBP3_FRAMEGEN_MODE=repeat` (copy instead of blend) and with the upload skipped, to split worker from presenter.
4. **Cadence on 165 Hz** is the 3/3/3/2 pattern. 240 Hz (4 refreshes per frame, even) and G-Sync aren't measured.
