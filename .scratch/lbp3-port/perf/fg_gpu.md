# Frame generation, lane E: the warp generator on the GPU (2026-11-12)

Lane D's motion-compensated generator ([fg_warp.md](fg_warp.md)) took 8–11 ms of CPU per generated frame, with bursts to 20 ms: too slow for the present path's delay ([fg_present.md](fg_present.md)), and it needed an extra 16 MB/frame readback for its inputs. This lane moves it to Vulkan compute on shadPS4's device, with the inputs copied GPU-side at lane A's hook points ([fg_inputs.md](fg_inputs.md)).

**Labels:** **[measured]** from runs, logs and the offline evaluator; **[inferred]** otherwise.

- **Branches** (pushed to the forks; `port` untouched):
  - runtime `exp/fg-gpu`: lane D's `exp/fg-warp` with lane A's `exp/fg-capture` merged (`f7ae7fc`), then `8c3e902` (the GPU generator) and `83e36d3` (margin, counters, stills, cut rule). Worktree `D:\Recomps\lbp3-wt\fggpu`.
  - shadPS4 `exp/fg-gpu` = lane A's `55d38d87` (the two null-by-default hooks), no further changes. Worktree `D:\Recomps\lbp3-wt\fggpu-shadps4`.
- **Use:** `LBP3_FRAMEGEN=1 LBP3_FRAMEGEN_MODE=warp` now runs on the GPU. `LBP3_FRAMEGEN_WARP_GPU=0` keeps the CPU version; `LBP3_FRAMEGEN_MODE=warp-cpu` / `warp-gpu` pick one explicitly (offline tools).

## How it works

- **Shaders:** `runtime/src/gpu/fg_shaders/fg_warp.comp`, one GLSL file compiled once per pass (`-DPASS=n`) by the glslang built from shadPS4's externals (`runtime/cmake/fg_shaders.cmake`, same recipe as shadPS4's host shaders; no outside tools). Ten passes: HUD share, decide, luma level 0, downsample ×2, the game's vectors, the coarse full search (one workgroup per block, window in shared memory), the finer-level init + refine, the neighbour smoothing (Jacobi, ping-pong), the in-between field, and compose with the HUD put-back.
- **Same integer arithmetic as the CPU version:** the clamped plane reads match its edge-padded planes, the medians are the same order statistic, ties break the same way, the bilinear sampling and blends use the same fixed-point steps. The game's-vector decode uses `precise` floats.
- **Host side:** `runtime/src/gpu/fg_gpu.{h,cpp}` (`WarpGpu`: buffers, pipelines with push descriptors and push constants, `Record`, three readback slots, timestamps), `fg_offline.h` + the offline runner in `fg_live.cpp` (its own headless `Vulkan::Instance` for the tools), `video/framegen_gpu.{h,cpp}` (the generators).
- **In game** (`runtime/src/gpu/fg_live.{h,cpp}`), copies only, on the command-processor thread:
  - after the composite (the first draw with a pixel shader into a display buffer), the display image → the generator's HUD-less buffer;
  - at the game's `007f7f00` masked fill, before the clear, the velocity target → its vector buffer;
  - in the flip's readback (`ReadbackDisplayBuffer`), the display image → its frame buffer, one more `copyImageToBuffer` before the readback's own `Finish`.
  - Each copy waits (pipeline barrier) for the previous generation's reads of the same buffer. The game's images are only read (transfer source), as lane A's capture and the readback already do; the texture cache's flags aren't touched (`[fills]` identical on and off, below).
- **Kick:** after the readback's `Finish` (the frame's command buffers are done), the passes are recorded into the scheduler's next command buffer and submitted at once (`Flush`, no wait). That submission holds nothing but the generation; the game's next frame is recorded after it. Its tick goes with the real frame to lane C's worker (`RealInfo::gpu`).
- **Fetch:** the worker waits for the tick (`Semaphore::Wait`), copies the 8 MB result from its host-visible readback slot (seqlock-checked against reuse), and queues it as lane C's G. The game-facing side is lane C's, unchanged: `WaitFlipSlot`, the flip-anchored clock, the gate and `SignalFlip` run as before. The only new work in the flip interrupt is `Kick` (recording ~25 dispatches and a submit) between the readback and the slot wait.
- **Delay:** the GPU warp's frame is ready about 1 ms after the slot (below), so its G is shown one refresh + **2 ms** after the slot (`Generator::MarginMs`), not + 4 ms. `LBP3_FRAMEGEN_DELAY_MS` still overrides.
- **Cuts:** lane C's image difference, lane D's saturation decline, plus the camera jump from lane A: the main view's translation (`DAT_013a2a60`, camera at +0x20, translation floats 12–14), read at the flip submit. A cut needs a jump over `LBP3_FRAMEGEN_CAMJUMP` (default 1000) **and** at least 4× the previous frame's move. The first, absolute version fired 25 times per Needlepoint route on the Prologue map's camera swoop, which moves 1,800–6,000 units a frame smoothly [measured]; a cut jumps from ≤250 to thousands [lane A].
- **Status:** `[framegen] gpu: N fetched (N with HUD-less images, N with the game's vectors), N without GPU work, N declined; gpu avg/max; wait avg/max`, and `[framegen] cut: the camera jumped X (Y the frame before)`.
- **Stills:** the remote command `fgstill <name> [count]` writes the next generated frames with N−1 and N to `analysis\fg_stills\` (raw); `D:\Recomps\lbp3-wt\_tools\fg_stills_png.py` makes PNG strips.

## Offline check [measured]

`fgeval <set> --gens warp,warp-gpu,blend --compare` on lane A's six dump sets: **all 322 skip-one triples byte-identical** between the CPU and GPU warp, the same declines (1, the `bk_a` cut), so the same PSNR as lane D's table (`np_walk` 23.89 dB moving, `np_popit` 27.56, `bk_a` 25.97, `bk_b` 27.64, `bk_c` 33.41). Output in the clone's `analysis\framegen\fgw_compare.txt`.

- **GPU time per pass** (`LBP3_FRAMEGEN_GPU_PROFILE=1`, `bk_c`, RTX 3090): HUD share 0.01, luma 0.03, game's vectors 0.04, coarse search 0.07, smoothing 0.03–0.07 each (8 passes), init 0.03/0.09, in-between field 0.06, compose 0.17 ms. **About 1.1 ms in all.**
- **Offline wall time per generation** is 1.4 ms avg (GPU, uploads included) when run back to back; the earlier 2–3 ms averages with 7–9 ms maxima came with the CPU warp and 1.9 GB of file reads between submissions (the GPU clocking down) [inferred].
- `framegentest`: **103 checks, 0 failed** (16 new: the GPU warp equals the CPU warp on four pans, with and without the game's vectors, with the HUD-less images and a screen HUD; it declines on saturated vectors; the camera-jump rule fires on a cut, not on a smooth swoop, and can be turned off). The six other test programs pass with 0 failed.

## In game [measured]

165 Hz display, runs with `D:\Recomps\lbp3-wt\_tools\fg_runs.sh` (exe swapped into the clone, restored by trap with an md5 check), on and off alternating, the same exe. Data in the clone's `analysis\framegen\fgw_*`.

**Needlepoint heavy route** (`m5_np_route.sh`; frame split over the last ~38 s, the walk and the standing at the basket):

| run | walk work | standing work | main thread | patch + copy | GPU submit + run | walk frames > 40 ms | periods > 34 ms |
|---|---|---|---|---|---|---|---|
| GPU warp on 1 / 2 | **19.61 / 19.64 ms** | 17.86 / 17.75 | 5.77 / 5.79 | 6.64 / 6.60 | 4.22 / 4.24 | 0 / 0 | 3.3 / 3.4% |
| off 1 / 2 | **18.98 / 19.19 ms** | 16.98 / 16.88 | 4.91 / 5.11 | 6.46 / 6.47 | 4.61 / 4.46 | 0 / 0 | 1.1 / 1.3% |
| `repeat` (no generator) | 19.48 | 17.27 | 5.49 | 6.67 | 4.23 | 0 | 3.3% |
| lane C's blend on 1 / 2 (for comparison) | 20.58 / 20.04 | 18.31 / 18.12 | 6.18 / 5.90 | 6.96 / 6.68 | | 3 / 0 | 3.4% |

- **The cost to the game: +0.4–0.7 ms of walk work and +0.9 ms standing**, against +1.4 ms (walk) for the CPU blend. The ranges don't overlap.
- **Main thread, split** (asked by the coordinator): off 4.91–5.11 ms; `repeat`, which makes no frame and only copies, queues and uploads at 60 Hz, 5.49 ms (+0.4–0.6); the GPU warp 5.77–5.79 ms (+0.7–0.85). So roughly **half of the main-thread cost is the 60 Hz present path** (the presenter's doubled uploads and the worker's copies, present in every generator), and the rest comes with the GPU warp's extra work: its three input copies on the GPU, the 8 MB readback slot and the worker's 8 MB fetch [inferred from the three modes, one run of `repeat`]. Not split further.
- **"Periods > 34 ms"** (frame split's slow frames) rise from 1.1–1.3% to 3.3–3.4% in every mode with frame generation on, `repeat` and blend included. None is over 40 ms, and in those frames no phase grew by more than ~0.4–0.7 ms over its median: it is slot jitter of the 60 Hz present path, not the generator [measured; cause inferred].

**The generator in game:**

| run | real frames | generated | GPU time per frame, avg / max | worker's wait for the GPU | fetch (wait + copy), avg / max |
|---|---|---|---|---|---|
| Needlepoint on 1 / 2 | 3,600 / 3,600 | 96.0 / 96.2% | 1.64 / 4.35 ms, 1.68 / 4.32 | 0 (max 1.18 ms) | 0.76 / 1.92 ms |
| Introduction on 1 / 2 | 3,030 / 3,030 | 94.9 / 96.4% | 1.68 / 4.16 ms | 0 | 0.72–0.78 ms |
| Thruster runs 1–3 | 2,490 each | 91.1–91.6% | 1.6 / 4.84 ms | 0 | max 4.51 ms |
| photo | 1,800 | 93.3% | 1.6 / 4.91 ms | 0 | max 2.55 ms |

- The GPU time includes the 8 MB copy to the host-visible readback slot. **The work is always done before the worker asks**: the wait averages 0.00 ms.
- **Inputs:** every fetched frame in levels and cutscenes used the HUD-less images and the game's vectors (e.g. Needlepoint 3,454 / 3,453 of 3,455).
- **Drops** (whole runs, boot included): the game's own 60 Hz at boot (20), late slots (6–16), 0- or 2-step frames (73–147), and cuts. The first batch ran the absolute camera rule, which counted the map swoop as cuts (23–48 per run); the relative rule (second Introduction run) dropped 1 there. Declined on saturated vectors: 0–5 per run. Gen-late: 0. Shown a refresh late: 0–1 per run.

**Latency and cadence** (`fg_score.py`; Introduction from 60 s):

| | real frame: slot → scan-out | generated frame: slot → scan-out | new frames on screen |
|---|---|---|---|
| Needlepoint on 1 / 2 | 28.64 / 28.61 ms | 12.09 / 12.19 ms | 2 refreshes 24.6%, 3: 73.7% (the 3/3/3/2 pattern) |
| Needlepoint off 1 / 2 | 9.25 / 9.12 ms | | 5 and 6 refreshes, 49.4% each |
| Introduction on 1 / 2 | 28.73 / 28.73 ms | 12.13 / 12.17 ms | 59.1/s |
| Introduction off 1 / 2 | 9.43 / 8.86 ms | | 30.0/s |

- **Real frames go out +19.3–19.9 ms later**, against +21.5 ms with lane C's blend: the 2 ms margin, P/2 = 16.7 ms and the refresh quantization.
- No frame went out before its due time.

**Game side and regression:**
- **Steps ≠ 1 in the Introduction:** on 0.92% / 0.00%, off 0.39% / 0.26%. The first on run's 0/2 pairs cluster at flips 931–956, the camera check's frame-dump window (the off run has a pair there too); outside it 6 against 4. Within noise.
- **Thruster recipe:** 3 of 3 OK with it on (`analysis\ab_fgw_th_{1,2,3}_stderr.txt`).
- **Photo** (`t_m2v_photo.sh`): encoder PASS, capture readback PASS, 1 JPEG in the save.
- **`[fills]` per frame identical on and off:** Needlepoint 3.1 refreshes (32.0 MB), 59.0 cleared (138.0 MB), 1.0 pre-syncs, 7.0 skipped; Introduction 3.0–3.1 refreshes, 57.0 cleared, 2.0 pre-syncs, 4.0 skipped.
- **GPU warnings 0; nvlddmkm events 0** (12:20–13:05, the stills runs included).

## Stills in game [looked at]

`fg_stills.sh np` and `fg_stills.sh bunkum` (the GPU build, from the story test save), PNG strips N−1 | generated | N in the clone's `analysis\fg_stills\png\`:

- **`pod_*`** (the Pod at boot, no game vectors: colour search only) and **`menu_*`** (the Play page): clean. The game's network dialog and the menu text are crisp.
- **`walk_*`** (Needlepoint, a camera pan): clean, with the flagpole and ropes intact; the score bubble fading in shows halfway.
- **`popit_*`:** the route reached the basket with the stick held, but the popit didn't open in this run. The hint text ("Press UP and DOWN…", screen HUD) is crisp over the moving scene and Sackboy is halfway. The popit itself is covered by the offline sets (`np_popit`: the GPU output equals the CPU's, and lane D looked at it).
- **`load_*`** (the Prologue map's camera swoop into the level node): the scene zooms correctly, but **the rotating "Loading…" text around the node is partly lost in the generated frames**. It is in-world UI with no vectors of its own, inside a fast zoom: lane D's known worst case, now seen in game too.
- **`bk_subs_*`, `bk_slow_*`** (Bunkum): subtitles and cinematic bars crisp, the scene interpolated.
- **Cuts (`bk_cut_*`):** the camera rule fired on 8 cuts in the cutscene, including lane A's 5,753-unit one, each time from a still camera ("0 the frame before"); the image difference caught one more; 4 frames were declined on saturated vectors. Each cut drops exactly one generated frame, and none of the 12 stills taken across that stretch spans a cut (the largest N−1 → N change in them is 1.7).

## Not done / follow-ups

- **The "Loading…" ring in the swoop.** Options: drop generation when the camera's move is very large even if smooth (the swoop is under a second long), or treat a zoom as such (lane D's zoom-aware candidate).
- **The main-thread cost of the 60 Hz present path** (+0.4–0.6 ms with no generator at all) is lane C's presenter: its uploads through SDL's D3D11 at 60 Hz. A Vulkan presenter on shadPS4's device would remove the uploads and the 8 MB fetch.
- **GPU time** could come down further (the passes are latency-bound small dispatches; the level-0 searches could use one workgroup per block as the coarse search does), but 1.1 ms of passes is already small next to the frame.
- **Not measured:** 240 Hz (where 60 is an even 4 refreshes), G-Sync, and a human looking at it.

## Handover checks (2026-11-12 13:10–13:24) [measured]

Build `D:\Recomps\lbp3-wt\fggpu\build\lbp3.exe` (exp/fg-gpu `83e36d3`). Scripts: `D:\Recomps\lbp3-wt\_tools\fgh_overlay.sh`, `fgh_popit.sh`, `fg_cov.py`.

**1. The human's launcher path (`play_fg.bat`, run once via `cmd /d /c`, `LBP3_DISPLAY=165hz`):**
- **Window:** on "DQI27165" (165.01 Hz), one of 2 displays; the process path is the fggpu exe.
- **The Pod:** reached about 26 s after launch (intro skipped); the run lasted about 57 s.
- **Generation:** `[framegen]` showed 30 generated per 30 real in every Pod window.
- **Whole session:** 1,440 real, 1,354 generated, 85 dropped (boot 60 Hz 19, `steps` 60, `late` 5, `first` 1); GPU 1.6 ms a frame.
- **Pod stretch** (`fg_score.py --from-s 30`): 59.7 new frames/s; intervals of 2 refreshes 24.9% and 3 refreshes 74.6%; real frames out 28.7 ms after their slot, none early.
- **Logs** landed in the clone: `analysis\play_fg_log.txt`, `play_fg_stdout.txt` and `framegen.csv` (2,803 rows).
- **Close:** WM_CLOSE (taskkill without /f). "[video] window closed", the process gone within 1 s, launcher exit 0.
- **Saves:** snapshot `analysis\savedata_backups\human_pre_fg_20261112_1310`, identical to `human_play_20261111_2104`. The game rotated its own profile save on entering the Pod: `LBPxSAVExLITTLE/00000001/littlefart213` (142,204 B) became `littlefart214` (142,148 B) at 13:10:23. No other file changed. This is the game's normal per-session write; the snapshot keeps the old one.

**2. The overlays with frame generation on** (exe swapped into the clone, restored by trap, md5 `0dfda1b9…` checked; saves via saves.sh):
- **`t_m2v_osk.sh fgh_osk`: PASS.**
  - Typing: confirmed "osk ok".
  - Circle cancel: the name was unchanged and the menu stayed on the field.
  - Generation: 30 generated per 30 real while the keyboard was up.
  - Still: `analysis\screenshots\m2_fgh_osk_1_keyboard.png`, the keyboard drawn crisp over the game.
- **`t_m2v_backup.sh fgh_backup`: all verdicts as expected.**
  - Front end: 6 dialogs on the overlay, 0 on a window.
  - The steps: (1) the backup folder was created, (2) the backup was unchanged after No, (3) no save file changed after the import was cancelled.
  - Stills: `m2_fgh_backup_{1_list,2_list,2_overwrite,3_list}_dialog.png`; the overwrite dialog is crisp over the game, with the subtitles intact.
  - nvlddmkm events: 0.

**3. The popit open, in game** (`fgh_popit.sh`, story test save; stills `hpopit2` taken 2.5 s after Square; the screenshot `m2_fgh_popit.png` confirms it open):
- **The generated frames:** the popit box ("Personal", icons), the "Close Popit" HUD and the score bubble are intact and crisp, with the scene halfway between.
- **Measured:** generated vs neighbours 2.2–2.7 against 3.9–4.7 neighbour to neighbour; in the popit box 0.8–4.1 against 1.7–7.7.
- **Files:** strips in `analysis\fg_stills\png\hpopit2_*.png`.
- **The PNG strips swap red and blue** (the converter's channel order). That's display only; the screenshots' colours are right.
- **Earlier stills don't show it:** `hpopit` was taken 1 s after Square, before the popit had opened (running left, clean apart from a faint doubled glyph on the left "L" sign).

**4. Coverage in steady play** (rescored from the existing runs; `fg_cov.py`: route walk/standing windows from each run's `_out.txt` counts; the Introduction from 5 windows after the level load):

| Run | walk | standing | Introduction play |
|---|---|---|---|
| GPU warp `fgw_r_on1` | 98.8% (6 dropped of 510: steps) | 100% of 660 | |
| GPU warp `fgw_r_on2` | 99.6% (2: steps) | 100% | |
| Blend `fgr_on1`, `fgr_on2` | 99.6% (2: steps) each | 100% each | |
| GPU warp `fgw_i_on1` | | | 98.2% (27 of 1,530: late 7, steps 14, cut 6; the camera check's frame-dump window) |
| GPU warp `fgw_i_on2` | | | 100% (1 cut) |
| Blend `fgi_on1`, `fgi_on2` | | | 99.7% (late 1, steps 4), 99.6% (steps 6) |

During play, drops stay at or under about 1 in 100 apart from the frame-dump window. Every walk drop is a 0- or 2-step flip, which the game itself shows as a camera snap anyway. Standing still drops nothing.

**State after:** no game, marker, kept copy or swapped exe left; the clone is on `port` with its own exe (md5 `0dfda1b92c30982b636b647e6d9f4ddf`). The human's saves equal the snapshot plus the game's own `littlefart` rotation from step 1.

## In-world text in the map swoop (2026-11-12 night, `fix/fg-overlay-text`)

The "Loading…" ring around a level node on the Prologue map lost letters in generated frames. Offline work on `np_load`, on branch `fix/fg-overlay-text` `aa2dc7b` (from `port` `2cd464a`, pushed to the fork, not merged).

**Cause [measured, decision map `LBP3_FRAMEGEN_WARP_KINDS=1`, single-pixel probes]:**
- The letters are white (255), like the map's white gingham stripes, and are readable on a stripe only through their soft shadow.
- During the swoop, many output pixels near the letters are white in both frames at the same spot: a stripe in one frame, a letter or a stripe in the other.
- The static-copy shortcut (a pixel within 2 in N−1 and N is copied unwarped) took those pixels as static, so the letters' outlines were replaced by stripe white.
- The per-pixel alternative search and averaging were not the main loss: a rule keeping the nearer frame's sample at thin bright strokes changed nothing measurable.

**Fix:** within `textD` (8) px of a thin bright stroke, a near-white pixel that would be copied as static is drawn from the nearer frame moved with the scene instead.
- **Stroke test:** min channel ≥ 236, and ≥ 48 brighter than the luma within 8 px on both sides along x or y.
- **Exception:** a pixel that is a stroke in both frames (static text) is still copied.
- **Switch:** `LBP3_FRAMEGEN_TEXT=off` restores the old behaviour. `LBP3_FRAMEGEN_WARP_TEXT_W/C/R/D` tune the thresholds.
- **GPU:** two new passes, the stroke masks of both frames and a separable square dilation, plus the compose rule.
- **Status line:** `[framegen] gpu` now counts frames and pixels drawn this way ("white near text").

**Scores (skip-one PSNR all / moving, before → after, measured):**

| Set | Before | After |
|---|---|---|
| np_walk | 27.37 / 23.89 | 27.38 / 23.89 |
| np_popit | 31.84 / 27.56 | 31.85 / 27.56 |
| np_load | 37.57 / 41.66 | 37.58 / 41.66 |
| bk_a | 32.96 / 25.97 | 32.96 / 25.97 |
| bk_b | 35.32 / 27.64 | 35.32 / 27.64 |
| bk_c | 38.09 / 33.41 | 38.09 / 33.41 |

- **No regression:** the text-only score (`fgeval --text`) is unchanged too, 17.36 dB on np_load. PSNR can't see this fix, because a crisp letter a few pixels off scores like a faded one, so the stills are the judge.
- **Variants rejected:**
  - Keeping the nearer frame's sample near every stroke, whether or not the samples agree: fuller words at k=8, but −0.1 to −0.2 dB on the walk and cutscene sets.
  - The same with a stricter white (252): the same cost, and a dark ghost artifact at k=10.

**Stills** (`D:\Recomps\lbp3-recompiled\analysis\fgtext\`, columns prev | before | after | cur):
- `zoomfinal_top.png` and `zoomfinal_bottom.png` (k = 4, 8, 10, 12);
- `sheet_k0NN.png` (whole node);
- `before\` and `final\` (fgeval crops).

What they show:
- **k=8:** "L ding…" becomes "Lo ding…".
- **Bottom of the node:** the zipper pulls that were smeared come out crisp.
- **k=10–12:** both versions still lose letters where the node's white disk expands under them; in the real frames those letters are near-invisible white on white anyway.
- **Not fixed:** the blocky artifacts at the node's top edge in k=10. They come from the motion field (zoom), not the text.

**Equivalence and cost:**
- `fgeval --compare`: all 322 triples of the six sets byte-identical, CPU against GPU.
- The text passes take about 0.2 ms of GPU per generated frame at 1080p (`LBP3_FRAMEGEN_GPU_PROFILE=1`).
- The CPU path costs about +10 ms on swoop frames. It reuses N−1's mask in play; the CPU path is the fallback, not the default.

**Tests:**
- `framegentest` 106/0, with 3 new checks: white pixels near text are drawn moved; `LBP3_FRAMEGEN_TEXT=off` keeps the old copy; GPU = CPU on the text scene.
- The eight other offline programs pass.

**For the in-game check:**
1. The Prologue map swoop into a level, with `fgstill` stills of the ring.
2. The `[framegen] gpu` line's "white near text" count.
3. A walk and a cutscene, to confirm no visible change elsewhere.

### In game (2026-11-13 ~01:00)

**Setup:**
- `fix/fg-overlay-text` with current `port` merged: `6c03014` = `aa2dc7b` + `port` `9625c5a`, submodule `553be3ba`, pushed to the fork.
- Rebuilt; the offline programs pass (framegentest 106/0, rescachetest 68/0, the other eight 0 failed; padtest needs a controller).
- `play.bat`'s settings: 1440p with its shaders, frame-generation warp, DLC (256 packs listed).
- 165 Hz display, story test save via `saves.sh`.
- The game ran on a copy of the shared shader cache (`LBP3_SHADER_CACHE_DIR`), so the shared cache is unchanged: same fingerprint before and after.

**Runs:** `D:\Recomps\lbp3-wt\_tools\fgt_ingame.sh <np|bunkum> <on|off>`, A/B with `LBP3_FRAMEGEN_TEXT=on/off`. Stills are in the clone's `analysis\fg_stills\t{on,off}_*`; stderr in `fgt_*_stderr.txt`.

- **The Prologue map swoop (measured, stills):**
  - The ring's text keeps its letters in the generated frames with the rule on: left "…gnibaoL" and right "Loadin…" read in full.
  - With it off, letters break up and the zipper pulls smear.
  - Sheets (columns: generated with it off | generated with it on | the real N): `analysis\fgtext\ingame_load_06/09/12/15.png`.
- **Walk and cutscene:** no visible difference between on and off: Needlepoint's start, the hint banner, the Bunkum cut and subtitles. Sheets `ingame_walk.png`, `ingame_banner.png`, `ingame_detail.png` (full-resolution crops).
- **Counter:** the `[framegen] gpu` line's "white near text":
  - 30 of 30 frames, ~7,600 px a frame, at the end of the swoop-and-walk run;
  - 27 of 30 frames, ~800 px a frame, in Bunkum;
  - 0 with it off.
  At ~0 camera motion such pixels come out as before (the nearer frame at p ≈ the copy).
- **GPU:** 1.96–1.97 ms per generated frame with it on, against 1.52–1.62 off: +0.35–0.45 ms at 1440p.
- **Clean run:** no GPU warnings, no device loss.
- **State left:** no game, marker, kept copy or swapped exe (the runs used the worktree's exe). The human's saves are identical to `human_play_20261113_0046`.

**User launcher:** `D:\Recomps\lbp3-wt\fgtext\play_fgtext.bat`. It uses `play.bat`'s settings with this build; `FG_TEXT=0` in it gives port's behaviour, for comparing.
