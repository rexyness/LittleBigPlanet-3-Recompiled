# Milestone 5, performance: heavy levels over budget, and stalls

> **Moved to GitHub:** https://github.com/rexyness/LittleBigPlanet-3-Recompiled/issues/18 (#18) on 2026-11-19. This file is the archive as of the move; work on the issue.

Type: task
Status: moved to #18 (2026-11-19); was: in progress (measuring; started 2026-11-11 evening while the human was away: "do amazing things my boy and don't stop")
Blocked by: none
Map: [LBP3 native port](../map.md) (done; build ticket from the [spec](../spec.md), gap 10)

## Question

Why do the heaviest levels run over the 33.3 ms budget, and what are the 0.5–2.5 s stalls? Fix what the port causes, and say plainly what is the game's own work.

## Done when

- Each symptom has a measured answer: which thread holds the frame, and in what.
- Port-side causes are fixed on branches, each judged by alternating A/B runs on the same fixed route, with no new GPU warnings, milestone 1's checks still passing and no change in the game's behaviour (sim steps per frame, saves).
- The heaviest measured routes (Needlepoint Peaks; the Introduction's transitions) are under budget, or what remains over it is shown to be the game's own CPU or GPU work.

## Comments

- 2026-11-11, **the evidence** ([14](14-milestone-3-prologue-first-world.md) has the per-level table): Needlepoint Peaks averages 37.7 ms with "work" 33.9 ms and about 23% of frames over 40 ms; Stitchem Gate 38.2 / 34.4 / 30%; Stitchem Manor 34.5 / 31.8 / 20%; lighter levels sit at work 22–26 ms. Stalls of 0.5–1.4 s at level entry and mid-level.
- 2026-11-11, **read-only audit** ([perf/audit.md](../perf/audit.md)):
  - **"work" is not the game's CPU time.** It is everything in the frame period but the slot wait: the game's main thread until its submit, the command processor recording the frame, blocked queue waits, the GPU running the whole frame (the first Vulkan submit is the readback's `Finish`, since `sceGnmSubmitDone` is a no-op), and an 8.3 MB readback of the display image. The game's main thread waits in `sceGnmSubmitAndFlipCommandBuffers` for all of it. So the frame is fully serial: the CPU and the GPU never overlap.
  - **At 165 Hz the pacer uses a 60 Hz timer grid** (165/30 isn't whole), so a frame that misses 33.3 ms by a little shows at 50 ms; at 240 Hz the step is 4.2 ms.
  - **The stalls match first-time shader and pipeline compiles**: 28 of 31 stalls of 150 ms or more after boot fall within 4 s of shader cache writes (base rate 9–19%); all 9 of 300 ms or more do. A second visit to a level should be smooth.
  - Texture garbage collection never runs (the same `SubmitDone` reason); unmeasured.
- 2026-11-11, **plan** (the agent's, with the advisor's review): measure first, then one fix lane per measured cause.
  1. Instrumentation (`perf/instrument`): a per-frame split of "work" into its phases (CSV), pipeline compiles and compile time and early releases by call site on the `[video]` line, a log line for any read over 20 ms; `tools/frame_split.py` to summarise.
  2. Baselines on fixed routes from the test copy of the human's story save (`analysis/savedata_story_20261111`, never the live saves): Needlepoint Peaks (heavy) and the Introduction (transitions), plus F10 reports at a heavy spot.
  3. Fix lanes only for measured causes. Candidates from the audit: overlap the GPU with the next frame's CPU work (submit as the frame is recorded instead of at the readback; drop or defer the readback), the 165 Hz pacing grid (let a late frame flip at the next real vblank), pipeline compiles off the critical path or warmed ahead.
- 2026-11-11 22:10, **the heavy route and its baseline** (`tools/m5_np_route.sh`, from the story test save; on `port` `b5c42c4`, 165 Hz display, one display attached): Pod → Play → Adventure → Prologue map (down, right, up) → Needlepoint Peaks; 30 s at the start, then five 3 s walks right with a jump between, to the picnic basket. Standing at the start or at the basket is light (game work 22 and 24 ms); **the walk is the measure**: run 1 avg 33.64 ms, work 27.88 ms, 9 frames over 40 ms, worst 58.6 ms; run 2 avg 33.60, work 27.68, 9, worst 54.9 (17 windows each). The human's whole-level average was heavier (work 33.9 ms), from parts deeper in the level this route doesn't reach; it serves for A/B.
- 2026-11-11 22:25, **the frame split of the heavy walk** (instrumentation `perf/instrument` `d41c4c6` + shadPS4 `bacc9fba`, `LBP3_FRAME_SPLIT=1`, `tools/frame_split.py`; the instrumented build measures the same as `port`: walk work 27.90 ms vs 27.88). Per frame, mean (p95), all in series on the critical path:
  - the game's main thread: 6.32 ms (7.91)
  - "patch + copy", main's release from `WaitIdle` to the last buffer's hand-over: 6.52 ms (7.65). The overrun scan and the copy are cheap loops on ordinary memory, so this is almost certainly the command processor recording the frame's **earlier** command buffers: each `SubmitGfx` waits for the graphics queue, and the split's hand-over mark is the last buffer's.
  - command processor to the flip: 2.72 ms (6.14), of which the graphics queue blocked on waits 2.22 ms (5.50); recording proper p50 0.5 ms
  - the GPU running the frame (`Finish`, its only Vulkan submit): 10.83 ms (11.86)
  - readback copy 0.72 ms; slot wait 6.19 ms.
  - Slow frames (12 of 505 over 34 ms): 8 by "wait for previous frame" (+13.7 ms), 3 by blocked waits (+5.5 ms), 1 GPU. No pipeline or shader compiled on the route.
  - Standing at the basket: the same except no blocked waits (0.4 ms command processor) and the GPU at 10.5 ms.
  - **Reading:** recording (~7 ms) and the GPU (~11 ms) never overlap, because the recorded work is submitted only at the frame's end. Flushing recorded work to the GPU as the frame is recorded would let them overlap without letting the game run ahead (ticket 10's constraint stays: the game is still released only when the graphics queue is idle). Expected gain up to the recording time, ~6 ms per frame. That's the first fix lane, `perf/gpu-flush`.
- 2026-11-11 23:30, **fix lane `perf/gpu-flush`: the GPU starts on recorded work as each command buffer finishes** (runtime `2bb596f` on `perf/gpu-flush` from `d41c4c6`; shadPS4 `31071685` on `perf/gpu-flush` from `bacc9fba`; worktrees `D:\Recomps\lbp3-wt\flush` and `flush-shadps4`; not pushed, not merged).
  - **Hypothesis confirmed** (new split columns `flip_buffers`, `flip_dwords`, `first_to_last_enq_us`, `pre_*`): every frame is one `sceGnmSubmitAndFlipCommandBuffers` of **14 command buffers** (~63k dwords), no other submits. "prep" equals the time from the first buffer's hand-over to the 14th's (5.96 vs 6.00 ms): the command processor recording buffers 1–13, each submit waiting for the graphics queue. The GPU only started at the readback's `Finish`.
  - **Change:** in shadPS4's command-processor loop (`liverpool.cpp`), after a graphics command buffer's task finishes and the waiting submit has been notified, `rasterizer->Flush()` submits the recorded work without waiting. Same thread as all recording; the game is still released only when the graphics queue is idle (ticket 10). `LBP3_GPU_FLUSH=off` turns it off, `LBP3_GPU_FLUSH=<n>` flushes every n-th buffer.
  - **A/B on the Needlepoint walk**, same binary, alternating off/on, 3 runs each (`m5_ab_{off,on}{1,2,3}`): walk work off 27.14 / 27.69 / 27.28 ms, on 23.41 / 22.79 / 22.32 ms (**−4.5 ms**); standing at the basket off 24.17 / 24.15 / 24.90, on 19.96 / 18.90 / 18.99 (**−5.1 ms**). Per phase over the 1516 walk frames: `Finish` (the GPU wait) 10.98 → 5.92 ms; main thread 6.40 → 6.46, recording ("prep" 6.00 → 6.33, record 2.93 → 2.80), blocked waits 2.42 → 2.28, copy 0.75 → 0.76 unchanged; the slot wait (headroom) 6.23 → 11.00 ms. Frames over 40 ms on the walk unchanged (9 per run: they come from the previous-frame and blocked waits); worst frame off 53.8–60.1 ms, on 50.0–50.2. No GPU warning in any run.
  - **Safety, with the flush build in the clone's `build\lbp3.exe`** (backed up and restored, checksum verified): milestone 1: Introduction 0.00% and 1.18% steps ≠ 1, then an alternating check on the same build gave flush on 0.00% / 0.00%, off 0.13% / 0.00% (the 1.18% run's extra 0/2s were the known cluster at the scripted walk and frame dump, flips 992–1021, plus three lone pairs, as in earlier pre-flush runs); Thruster save recipe 3 of 3; Bunkum 16 of 16 with Needlepoint loading; sky (121, 177, 182). Photo: encoder, capture readback and photo PASS. Backup declining: backup unchanged, import Cancel loads nothing, 6 dialogs on the overlay. No `Rejecting invalid S#`, stuck wait or device loss in any log. The human's saves equal their 21:04 snapshot; no marker or kept copy left.
  - **Recommendation: merge** (with `perf/instrument`, which it builds on; the clone's submodule must move to `31071685`). Not covered: the human playing with it, and the heavier, deeper parts of Needlepoint Peaks and Stitchem Gate (work was 33.9 / 34.4 ms in their session; a ~4.5–5 ms cut would bring them under budget if the cut holds there).
- 2026-11-11 23:30, **slow-frame analysis** ([perf/slow_frames.md](../perf/slow_frames.md), read-only): of the walk's 12 slow frames, 8 are a once-per-session burst: the pre-submit `gpu::WaitIdle` waited on every queue, and the async compute queue, parked on a label wait the coming frame's graphics work satisfies, held the game for the full 20 ms grace eight frames in a row while those addresses were learned (the human's play logs show the same burst once per session). The graphics queue's blocked waits (one per frame in the last of 14 buffers, median 4.2 ms, in ~55% of walk frames) happen exactly in frames where the compute queue is parked; their address and writer aren't known yet, and letting the game run on through them is ticket 10's risk class, so they stay for later.
- 2026-11-11 23:35, **fix: the pre-submit wait waits for graphics only** (`perf/waitidle-gfx` `735e6f6`, on top of `perf/gpu-flush`; `LBP3_SUBMIT_WAIT=all` restores the old wait at both sites). The graphics queue is idle there, so ticket 10's race can't reopen. A/B against the flush-only build, alternating, two runs each (`analysis/m5_wgfx_ab.txt`):

  | | flush only | + graphics-only wait |
  |---|---|---|
  | pre-submit grace releases per run | 8, 8 | **0, 0** |
  | walk: frames over 40 ms | 9, 9 | **1, 1** |
  | walk: avg / work | 33.60 / 22.52, 33.60 / 23.16 ms | **33.34** / 22.15, **33.34** / 22.54 ms |
  | standing: work | 18.94, 19.42 ms | 19.07, 19.05 ms |

  No GPU warnings; the human's saves untouched.
- 2026-11-11 23:40, both fixes merged on the clone's `m5/verify` (`2d92d65`, submodule `31071685`, which fast-forwards the submodule's `port` from `50b4720a`); offline tests pass; the full regression (milestone 1, every milestone 2 check, `t13_coop.sh`, the heavy route) is running.
- 2026-11-12 00:40, **merged and pushed**. Full regression on `m5/verify` (`2d92d65`, submodule `31071685`): milestone 1 (Thruster 3 of 3, Bunkum 16 of 16, sky cyan), `t13_coop.sh`, the keyboard, backup declining and accepting, photo, story progress, the saved edit, level name, Text Chat and boot all pass; the heavy route: walk avg 33.34 ms, work 22.52 ms, 1 frame over 40 ms. The Introduction read 1.05% once; an alternating A/B against the pre-performance build (`analysis/m5_intro_ab.txt`) gave pre 0.00 / 1.18 / 0.00% and merged 0.13 / 1.05 / 0.00%, the over-1% runs both being the same 0/2 cluster at level flips ~975–1005, where `camera_check.py` walks and dumps frames: the check's own instrumentation, on either build. (To do: keep that window out of the count.) **`port` = `2d92d65`** (submodule `port` = `31071685`), pushed to both forks.
- 2026-11-12 00:10, **shader-compile freezes** ([perf/compile_stalls.md](../perf/compile_stalls.md), read-only): the 0.5–1.4 s level-entry freezes are the NVIDIA driver building Vulkan pipelines for shader code it hasn't seen (~46 ms per new pipeline, 10–50 in a burst); our GCN → SPIR-V translation is 2–3 ms a shader. A second visit to a level compiles nothing (13 of 13 warm runs). Only asynchronous pipeline creation with draws skipped until ready removes first-visit freezes; it trades a freeze for objects missing for a few frames, and photo captures and full-screen passes must never be skipped: a taste call for the human, not started.
- 2026-11-12 00:30, **gap 15 drops whole draws** ([perf/dropped_draws.md](../perf/dropped_draws.md), read-only): one object-drawing path (`eboot+0x465cf0`, called from `+0x470200`) turns on a geometry-shader stage (ES+GS); `sceGnmSetEs/GsShader` are NOP fills in the runtime, so shadPS4 finds no ES program and drops the draw, every frame, without a warning. Seen in the Pod, the Introduction, the Legend of Bunkum, Needlepoint Peaks, the Manglewood hub and the Diner. What goes missing isn't known yet; a fix lane (`fix/gs-stages`, behind `LBP3_GS_STAGES=1`) is on it.
- 2026-11-12 01:05, **gap 15, the dropped geometry-shader draws** (lane `fix/gs-stages`: runtime `23d3070` on top of `m5/verify`, shadPS4 `e56074fe` on top of `31071685`; worktrees `D:\Recomps\lbp3-wt\gs`, `gs-shadps4`; script `D:\Recomps\lbp3-wt\_tools\gs_ab.sh`):
  - **Instrumentation (always on):** shadPS4 counts every draw dropped for want of a pipeline and logs each distinct case once (`[gpu] dropped draw: <reason>; stages, ES/GS/VS/PS addresses, PS hash, primitive, GS mode, streamout`); the `[perf]` line ends with `dropped N draws`. Every NOP-filled shader setup logs its first call.
  - **What is dropped:** in all sessions only one reason: "ES (export) program not set; stages 0xb0" (ES+GS), primitive 10 (line list with adjacency), GS mode 0x180013, no streamout, 5–7 distinct pixel shaders. Rates: ~18 draws per frame in the Pod and the Introduction with one player (1,800 in some 30-frame windows), ~60 per frame in the Pod with two players, ~13 per frame in the Legend of Bunkum. They scale with the characters on screen, which with adjacency lines points at geometry-shader fins on characters (fabric fuzz/silhouettes): an inference, not seen. Only `sceGnmSetEsShader` (20 dwords) and `sceGnmSetGsShader` (29 dwords) are ever called; `UpdatePs/UpdateVs/UpdateGs/Hs/Ls` never were in these sessions.
  - **The fix behind `LBP3_GS_STAGES=1` is not safe:** with the ES/GS registers written as shadPS4's gnmdriver does, no draw is dropped, but the GPU hangs at the first such draw in the Pod and the driver resets it (System log nvlddmkm 153 / Display 4101 at 00:58:46, 01:00:19 and 01:00:59, one per "on" session; none in the "off" sessions). The game then makes no new frames (the presenter keeps presenting the last one); the runtime logs no device loss. So there is no "on" image to compare with, and the switch stays **off by default**. What hangs is shadPS4's GS path for this case (adjacency line input, GS mode 0x180013): a separate ticket for later (needs the checkpoint build's device-lost report, or RenderDoc).
  - With the switch off (the default) the build behaves as `m5/verify`: no GPU warnings in 3 sessions. The human's saves stayed identical to the 21:04 snapshot; no marker, kept copy or swapped exe left behind.
- 2026-11-12 02:40, **Introduction transition stalls** ([perf/intro_stalls.md](../perf/intro_stalls.md); on `port` `2d92d65`, 2 timing runs and 7 walks with a new route that reaches the tutorial video and Newton, `tools/intro_walk.sh` + `intro_drive.py`):
  - **The human's 1 s (Newton's entrance) and 2.5 s (after his story save) stalls didn't reproduce** in 5 walks through the same spots, from the test baseline and from the human's own pre-walk save; worst frame there 50 ms. Their stalls line up with the game streaming that area (and a level load after the save); our build, 165 Hz and a scripted player all differ from their session, so this is "not reproduced", not "fixed".
  - **Level loads** (Introduction 0.70–0.85 s, leaving for Bunkum 0.91–1.11 s of skipped game steps while frames keep flipping) are the game's own loader: ~145 MB read, no read over 20 ms in 9 runs, ~16 µs per read in our path (~50 ms per load), the async I/O thread idle.
  - **The one stall on our side: the first frame of a new scene** (Pod at boot 180–233 ms, Prologue map 191–204 ms, once 326 ms): command-processor recording of the first buffers 69–122 ms, then GPU 26–89 ms, no compiles. Uploads, driver allocations or the lighting-volume expansions; to instrument next.
  - Also: the main thread spends ~1 ms a frame in write-tracking faults, loading or walking.
- 2026-11-12 02:00–03:00, **gap 15 fixed behind the switch, and safe** (shadPS4 `fix/gs-stages` `5b57642d`, runtime `fix/gs-stages` `ae21860`; analysis [perf/gs_hang.md](../perf/gs_hang.md)):
  - **The cause, confirmed:** this RTX 3090 reports `maxGeometryTotalOutputComponents` **1024** (and 1024 output vertices, 128 components per vertex, 128 input components; logged at startup as `[gpu] geometry limits: ...`). shadPS4 declared `OutputVertices 64` (the game's GSVS ring budget, `VGT_GS_MAX_VERT_OUT`) for geometry shaders that write 4 vertices of 24 or 40 components: 1,536 and 2,560 components, over the limit, hence the GPU's "Shader Program Header" exception.
  - **The fix:** the GS declares the copy shader's real vertex count (4), set in the runtime info so no cached 64-vertex module is reused; and a permanent guard checks every ES+GS draw against the device limits before compiling and for the permutation actually picked, dropping and logging the draw instead of faulting. With the switch off nothing is parsed and the path is unchanged.
  - **Runs** (165 Hz display; the System log read for nvlddmkm and Display events after every run; tool `D:\Recomps\lbp3-wt\_tools\gs2.sh`):

    | Scene | switch off: dropped draws | switch on (`LBP3_GS_STAGES=1`): dropped draws | GPU faults |
    |---|---|---|---|
    | Pod, one player | 14,490 | 0 (cache bypassed), 0 (cache on: the 64-vertex pipelines preloaded at boot are not picked) | none |
    | Pod, two players | 55,170 | 0 | none |
    | the Introduction | 50,343 | 0 | none |
    | the Legend of Bunkum, heroes' entry | 20,214 | 0 | none |

  - **Milestone 1 with the switch on** (the clone's exe swapped and restored by trap): Introduction 0.07% and 0.00% steps ≠ 1, Thruster recipe 3 of 3, Bunkum 16 of 16 with Needlepoint loading, sky (121, 177, 182); 0 dropped draws; no System log event over the whole run. One Introduction window averaged 38.5 ms: its `[perf]` line shows the first-sight compile of 2 new GS pipelines (158 ms), once.
  - **What the restored draws are:** no visible change in still screenshots (off vs on differ by 2.2–3.8 mean, within each run's own animation noise of 4.0–4.1; mean absolute difference over Sackboy's area, `D:\Recomps\lbp3-wt\_tools\gs_noise.py`). That fits what they write: a depth-only pass and a motion-vector pass for character fins (pixel shaders `23603ec6e8b6e39e` depth, `dd549569…` velocity) plus one colour permutation; they feed motion blur and depth effects, so any difference shows in motion. Screenshots: `analysis\screenshots\m2_gs2_{off,on}_*` and the close-up `m2_gs2_cmp_pod_sackboy.png`. No garbage or stray geometry anywhere with the switch on.
  - **Recommendation:** turn `LBP3_GS_STAGES` on by default after a merge and regression on `port`: the game asks for these draws, they render without faults in every scene tried, milestone 1 passes, and the guard turns any future over-limit GS into a logged drop instead of a GPU reset. Cost: a one-time compile of each new GS pipeline (~80 ms each) on first sight. The human seeing it in motion (fabric/motion blur) would confirm the visual side.
- 2026-11-12 03:40, **gap 15 fixed on `port`** (`d84eae8`, submodule `5b57642d`, both pushed): shadPS4 declares a geometry shader's real output vertex count (from the copy shader, 4) instead of the game's ring budget (64), which put the GS over the RTX 3090's 1,024 total output components and made the GPU fault; a permanent guard drops (and logs) any GS draw over the device limits instead of faulting; the runtime writes the ES/GS stage registers by default (`LBP3_GS_STAGES=0` restores the NOP fills). Full regression with it on (`analysis/gs_regression_summary.txt`): milestone 1 (Introduction 0.13% and 0.13%, Thruster 3 of 3, Bunkum 16 of 16, sky cyan), the keyboard, backup both ways, photo, story progress, saved edit (B saved vs C play 2.1; the A–B level shots differed by a camera still settling), level name, Text Chat, boot and the heavy route (walk work 22.44 ms, 1 frame over 40 ms); **0 dropped draws and 0 nvlddmkm events** across the whole run. `t13_coop.sh` failed once ("pads 2" never applied: the test helper wrote the command file in place and the game read it half-written; `m2_lib.sh`'s `cmdq` now writes a temp file and renames it) and passed on the rerun. The restored draws (likely character fabric fins: depth/motion-vector passes and a colour pass) aren't visible in still screenshots; the human should look in motion.
- 2026-11-12 04:15, **what the first frame of a new scene spends its time on** ([perf/first_frame.md](../perf/first_frame.md); measurement only, branch `perf/first-frame` in `D:\Recomps\lbp3-wt\ffs` / `ffs-shadps4`, local; 10 runs of `m5_np_route.sh`, data in the clone's `analysis\first_frame\`):
  - **Prologue map** (recording 91–95 ms, Finish 81–86 ms):
    - CPU: **144 image expansions, ~47 ms.** The three lighting volumes (512×256 R8 ×64, 256×128 RGBA8 ×64, 64×32 RGBA8 ×16) are render-target arrays filled one slice at a time. `ExpandImage` grows each by one layer per slice, re-uploading the whole grown image (23.5 ms) and then copying the old layers over it: 521 of 537 MB uploaded are overwritten at once. Then other image uploads ~16 ms, a 52 MB buffer sync 14.5 ms.
    - GPU: **our transfers, 80–85 ms** (image uploads 35–38, detile 34–35, the expansions' copies 7–8, buffer copies 2–3), against ~6 ms of the game's draws. About 60 ms of it is the expansions'.
  - **Pod** (two flips, 69–73 + 118–178 ms):
    - CPU: **guest → staging memcpy, 72 of ~97 ms** of recording (264 MB; inside buffer sync 41 ms and image refresh 35 ms), with 110,000 page faults in the window.
    - **82% of those bytes are all-zero,** memory the game never wrote. That is likely ~55,000 demand-zero faults on first read. Needlepoint's first frame, by contrast, is 14% zero.
    - GPU: our transfers again, 100–127 ms (detile 49–56), run while the GPU is still at P5 (~460 MHz, PCIe gen 2; `nvidia-smi`). One run, likely caught higher, did them in 33 ms.
  - **The Pod's flip interrupt fires after buffer 1 of 8:**
    - in every run on this route, and in 3 of 9 of the intro_stalls runs;
    - the frame is presented, and the game told, before 7 of 8 buffers ran;
    - same-build runs without it are not faster (two-flip totals 226–262 ms against 183–233 with it), so it is a correctness issue with no time cost shown.
  - **Fix candidates, ranked:**
    1. `ExpandImage`: upload only the new layers, and grow layer arrays by powers of two. At the map, ~23 ms CPU and ~50 ms GPU from the first part; ~43 ms and ~58 ms with both. Small and local.
    2. Don't upload never-written guest memory: a per-page "never written" bit from mapping and the page tracker turns those uploads into GPU fills or clears. Mainly the Pod: most of its 72 ms memcpy and the zero share of 100–127 ms of GPU work. At the map after fix 1, only ~10 ms CPU and ~7 ms GPU are left. Risk: every guest write path must clear the bit.
    3. Honour only the flip NOP this submit patched (root-cause the stray one first). Correctness.
    4. Smaller: GPU power state (a test with "prefer maximum performance", not a code fix), detile throughput, and the frame after the map (40–43 ms unattributed in 2 of 6 runs).
  - **Also:** Needlepoint re-uploads 26 GPU-modified images (106 MB) every frame: 4.1 ms of timestamped GPU transfers against a 5.5 ms Finish. That is worth its own look for the walk.
  - No fix implemented. The counters show no walk cost (22.52–23.34 ms work across the runs). The human's saves stayed identical to the 21:04 snapshot; no marker, kept copy, swapped exe or game left behind.
- 2026-11-12 05:30, **fix candidate 1 done: lighting-volume expansions** (shadPS4 `fix/expand-layers` `d2fdcafe` on `7eab2b0d`, runtime `fix/expand-layers` `d799245` on `9f9b937`, both on `port` `d84eae8`; local, not pushed; worktrees `D:\Recomps\lbp3-wt\expand` / `expand-shadps4`):
  - **The change:** when `ExpandImage` grows an array by layers only (same size, format, tiling, one mip, 2D), the new array is rounded up to a power of two (at most 256, and only if no other cached image is in the extra guest memory), and only the new layers are uploaded: whole groups of the tiling's slices, with the detiler told the absolute first slice for its pipe and bank rotations. The old layers come from the copy, as before. `LBP3_EXPAND=old` restores the old path.
  - **The first A/B was void:** the volumes are thick-tiled (4 slices per micro tile), and the first build only took thin tiling, so both modes did 144 expansions. With thick tiling accepted: **144 → 19 expansions** on the route (the growths are 1→2→4 as whole small images, then 8, 16, 32, 64 by new layers only).
  - **Prologue map's first frame**, 3 alternating runs each (`m5_np_route.sh` with `LBP3_FRAME_SPLIT=1`, tool `D:\Recomps\lbp3-wt\_tools\expand_ab.sh`, data `analysis\m5_expand_ab2.txt` and `m5_xab_*_split.csv`):

    | | recording (prep) | Finish | period |
    |---|---|---|---|
    | old | 107.2 / 94.2 / 95.3 ms | 65.6 / 75.4 / 82.0 ms | 195.0 / 183.6 / 191.0 ms |
    | new | 58.6 / 58.5 / 59.3 ms | 16.4 / 17.1 / 15.9 ms | 101.5 / 100.2 / 89.2 ms |

    `record_us` is 0.5–1.0 ms in both. That is about 40 ms of CPU and 50–65 ms of GPU saved, as first_frame.md predicted (~43 and ~58). The walk is unchanged (work 22.86–22.94 new against 22.92–23.78 old, avg 33.34 ms). The longest frame in the window is now the level's load frame (169–181 ms, no recording, same in both modes).
  - **Looks the same:** mean of 3 shots per mode, compared per cell of a 16×9 grid (`D:\Recomps\lbp3-wt\_tools\shot_cells.py`, `shot_compare.py`). Map: no cell differs by more than 3.1 luminance, where old-vs-old noise in that cell is 5.9; flat areas differ by 0.0–0.4, and mean luminance is 93.26–93.33 old against 93.35–93.38 new. In the level, the large cells are the swinging bunting and Sackboy (noise up to 24), and the rest is edge-only, a sub-pixel camera offset. This shows the same image for LBP3's use: the new layers are render targets the game overwrites, so it is not a standalone check of the thick partial detile.
  - **Not grown past the real size:** in all six regression scenes, each array's final size (64, 64, 16 layers) equals the depth of its later 3D use at the same address (`512x256x64`, `256x128x64`, `64x32x16`). There was never "another image in the way"; the only whole-image fallbacks are the 2- and 4-layer images.
  - **Regression on this build** (the clone's exe swapped and restored by trap, hash checked; `D:\Recomps\lbp3-wt\_tools\expand_regress.sh`, log `analysis\m5_expand_regress.txt`): Introduction 0.13% and 0.13% steps ≠ 1; Thruster recipe 3 of 3 OK; Bunkum 16 of 16 voice lines, Needlepoint loads; sky (121, 177, 182) cyan; photo PASS (encoder, capture readback, 1 JPEG); backup/import OK (6 dialogs on the overlay, overwrite-cancel and import-cancel left the saves unchanged); t13 co-op PASS; Needlepoint walk work 22.45 ms. Every run that enters a level did 19 expansions; no GPU warnings, and no nvlddmkm or Display event over the whole run. Offline tests: cooptest 81, dlgtest 154, osktest 263, dialogtest 368, jpegtest 157, imetest 403 checks, 0 failed.
  - **Recommendation: merge** both branches into `port`. Left after this: the map frame's ~59 ms recording and ~16 ms Finish (other image uploads; fix candidate 2, never-written memory), and the Pod's first frame, which this doesn't touch.
  - The human's saves stayed identical to the 21:04 snapshot. No marker, kept copy, swapped exe or game was left behind.
- 2026-11-12 06:25, **early flip fixed and proven** ([perf/early_flip.md](../perf/early_flip.md)). Branches (local): shadPS4 `fix/flip-marker` `3e31a920` + merge of `port` `d2fdcafe` = `020cf1ec`; runtime `fix/flip-marker` `4c53313` + merge of `port` `d799245` = `b76ac5f`, and the candidate `fix/fillnop-zero` = `cdeb4aa` + merge = `64d6252` (submodule `020cf1ec`).
  - **The gate** (shadPS4): the command processor raises the flip only for the PatchedFlip NOP 59 dwords before the end of a top-level buffer and logs any other ("[gnm] ignored a stale flip marker ..."). **The trace** (runtime, `LBP3_FLIP_TRACE=1`, off normally): one line per flip submit with every buffer's game address and size and the patched NOP's address. **The FillNop fix** (runtime): the NOP fill writes its first body dword 0, as upstream's `WriteTrailingNop` does.
  - **Proof run** (`m5_np_route.sh flip1`, gate + trace): 11 stale markers ignored, and every one sits at the exact game address of an earlier frame's patched flip NOP (2 to 786 submits earlier; `D:/Recomps/lbp3-wt/_tools/flip_trace.py`). The NOPs carrying them have 5 body dwords (9 of 11), 32 or 16 body dwords, as the command processor counts them; the FillNop fix below removes all of them, so all are runtime NOP fills. Besides the Pod's first frame (buffer 0 of 8), **they sit mid-level in the last buffer of their frame** (buffer 13 of 14, 4 of 5): on `port` those frames flipped part-way through their last buffer and were shown before its rest ran, which the earlier scan (`record_us < 0`) couldn't see; about nine such frames in this route's 2.5 minutes. Without the trace (`flip2`) the same 9 positions: deterministic, not caused by the trace.
  - **With the FillNop fix** (`flip3`, trace on): **0 stale markers**, so all of them came through the runtime's NOP fills.
  - **The Pod's first frame:** on `port` (`m5_xab_old3`) it flipped early (record -91.1 ms, the game released 73.6 ms after the flip, periods 79 + 133 ms); with the fix one flip at the end (record 23-30 ms, release 0.01 ms after the flip, one period of 203-211 ms). No early flip in any fixed run.
  - **Regression** on the candidate swapped into the clone (restored by trap, checksum-checked): milestone 1 (Introduction 0.00% and 0.78%, Thruster 3 of 3, Bunkum 16 of 16 with Needlepoint loading, sky cyan), backup declining, `t13_coop.sh` PASS, the heavy route (walk work 22.62 ms, 1 frame over 40 ms), photo (the first run's route stayed in the Pod and never took a photo; the rerun passes: encoder, readback, 1 JPEG in the save). 0 stale markers, 0 dropped draws, 0 nvlddmkm events. The human's saves identical to `human_play_20261111_2104`; no marker, kept copy or swapped exe left.
  - **Verdict: merge `fix/fillnop-zero`** (it contains the gate, the trace and the FillNop fix): it removes a frame shown half-rendered at the Pod's first frame and about nine mid-level frames shown before their last buffer finished, with no measured cost.
- 2026-11-12 07:40, **per-frame render-target re-uploads replaced by clears** (details: [perf/reuploads.md](../perf/reuploads.md), "Outcome").
  - **Branches (local, not pushed):**
    - shadPS4 `fix/rt-fills` `d5af2ae7`, on `020cf1ec`;
    - runtime `fix/rt-fills` `bac46c2`, on `64d6252`: the `[fills]` status line, then the submodule pointer at `d5af2ae7`.
    - Worktrees: `D:\Recomps\lbp3-wt\fills` and `fills-shadps4`.
  - **Measured first** (`[fills]` counters, plus `LBP3_FILL_LOG=1` naming each refreshed image and its writer):
    - every steady-state refresh is the game's masked fill `5c5e1be6`, and none is CPU-dirty;
    - Needlepoint does 26 refreshes a frame (104.8 MB), plus **7 image→buffer pre-sync copies (56 MB)** that had never been counted;
    - the depth target's 9,216 KB was confirmed.
  - **The change:** a fill with mask `ffffffff` and `cb[1] == 0` writes one word and reads nothing. So:
    - the images wholly inside its range are cleared to that word before the dispatch, when the clear reproduces it exactly (zero in any colour format and as depth 0.0; RGBA8/BGRA8 words; sRGB with `00`/`ff` colour bytes);
    - the pre-sync copy is skipped;
    - everything else keeps the old path.
    - `LBP3_RT_FILLS=old` restores it; `=presync` skips only the copy.
  - **Effect:** in a level, 26 → 3 refreshes (32 MB) and 7 → 1 pre-syncs. In the Pod, 7 → 2 and 5 → 2.
  - **A/B,** 3 + 3 alternating runs on the same build (`fills_ab.sh`):

    | | walk work | standing work | GPU wait at frame end |
    |---|---|---|---|
    | old | 22.29 / 22.28 / 22.85 ms | 18.95 / 18.98 / 19.63 ms | 5.55 / 5.56 / 5.73 ms |
    | new | 21.10 / 21.46 / 20.84 ms | 18.32 / 18.59 / 17.88 ms | 4.38 / 4.32 / 4.38 ms |

    - So −1.3 ms walk, −0.9 ms standing and −1.25 ms GPU, with no overlap.
    - How the gain splits between skipping the copies and the clears is not measured: `TileImage` has counts only, and the one copy-only run is just indicative.
  - **Screenshots:**
    - The Prologue map, Needlepoint's start, the Pod Home and the Introduction fog are within run-to-run noise.
    - **At the basket, old and new did not match within noise** (3.4–4.5 against 0.5–2.2).
      - The difference is on edges only, with no change in per-cell luminance.
      - Across all 15 basket shots from this build and the expansion build, the end picture falls into at least three position states, and both modes land in both main ones. The old-mode `rtf_meas_old` matches the new-mode runs to 0.78; the new-mode `rtf_meas_new` matches the old-mode runs to 0.32.
      - So this is where the walk ends, not rendering.
  - **Scope:** the writer log and the screenshot A/B covered the Pod, the Prologue map, Needlepoint and the Introduction. Bunkum, Create and the photo scene were covered only by the regression's pass/fail checks.
  - **Regression** (clone's exe swapped and restored by trap, md5-checked):
    - Introduction 0.78% / 0.00%; Thruster 3 of 3; Bunkum 16 of 16 with Needlepoint loading; sky cyan.
    - Photo PASS, and the photo looks right. Backup PASS (6 overlay dialogs). `t13_coop` PASS.
    - Route: walk 20.91 ms, standing 17.78 ms.
    - 0 dropped draws, 0 stale markers, 0 GPU warnings, no nvlddmkm events. Offline tests all pass.
    - The human's saves are identical to `human_play_20261111_2104`; nothing was left behind.
  - **Verdict: merge both.**
  - **Left:** the alpha fill (mask `ff000000`; F3) and two partial-range targets, ~32 MB a frame.
- 2026-11-12 08:05, **render-target fills merged into `port`** (`bac46c2`, submodule `d5af2ae7`), pushed to both forks; built and the offline tests pass on the merged clone. `LBP3_RT_FILLS=old` restores the old path.
- 2026-11-12 08:30, **the graphics queue's blocked waits, named and timed** (`perf/gfx_wait.md`). Measurement only, on `perf/gfx-wait` (runtime `9fff263`, shadPS4 `24f2276a`; local, not merged). `LBP3_GFX_WAIT_TRACE=1` is the trace and `LBP3_GFX_WAIT_SAMPLE=1` the job-thread sampler. Seven runs of the heavy route: one with the trace off (n=1, so the trace's cost is within run-to-run spread) and six with growing trace layers.
  - **What it waits for:** ticket 10's **fluid flag** (`0x1e1303c70`/`c78`/`c80`, rotating; `WAIT_REG_MEM == 1`).
    - It sits in the frame's last buffer and is reset by `WRITE_DATA 0` in buffer 0.
    - It is only present in fluid frames: 291 of the walk's 521.
    - No other graphics wait blocks.
    - The first-wait flag never blocked, so `slow_frames.md`'s fix 2 is not ruled out.
  - **Who ends it:** a guest CPU thread every time, never a command stream. It is a `Spu` job (inferred: from the sampler, and from ticket 10's hardware watch at the same addresses).
    - The processor notices in 0–1 µs.
    - 0 learn/forget events: no early release is involved.
  - **Why ~4 ms** (4.03 ms median with the trace off): the wait is the tail of a **~12 ms serial chain**.
    - The chain starts (inferred) at buffer 0's EOP interrupt and runs ~15 job ↔ compute round trips through the 8-slot ring.
    - The processor reaches the wait 7.6 ms into the chain.
    - The chain's end moves with that time (slope ~1), while the wait doesn't. So the chain isn't fixed-length: either the processor holds it back, or frame weight drives both. Open.
    - **Within the wait:** 55% compute parked on a guest job, 20% the processor recording compute, 25% the job tail, 0% noticing.
    - **Before the wait:** armed compute work waits behind graphics buffers for ~1.3 ms per fluid frame (median 0.18 ms per round trip, max 2.2 ms).
    - **The jobs:** untimed event-flag stages on `Spu 0–4`, with no timers involved. They take **~1,380 write-tracking faults per fluid frame** (862 before the block, ~520 during it; ~8.6 ms of handler thread time; `VirtualQuery` ~1.15 ms per wait window).
    - **The spike** (14.7 ms trace-off) is a burst of job work with 1,739 faults.
  - **Fixes ranked against ticket 10's risk:**
    1. `Protect` without `VirtualQuery`: no ordering change, small gain.
    2. Fewer faults on the fluid buffers: no ordering change, the largest lever on the game's side, coherence risk.
    - Like any chain speed-up, 1 and 2 shift the timing of ticket 10's race, so rerun its recipe.
    3. Compute turns inside graphics buffers: ≤1.3 ms, low–medium.
    4. Cheaper compute recording.
    5. Last: release the game at the fluid-flag block. Largest gain, ticket 10's class.
  - The human's saves are identical to `human_play_20261111_2104`; nothing was left behind.
- 2026-11-12 09:25, **fix 1 from [perf/gfx_wait.md](../perf/gfx_wait.md): `Protect` with one `VirtualProtect`** (runtime branch `fix/protect-fast` `0570df8`, from `port` `bac46c2`; worktree `D:\Recomps\lbp3-wt\protect`, which uses the clone's shadPS4; local, not merged). Recommendation: **merge**.
  - **The change:** `AddressSpace::Protect` (`runtime/src/gpu/video_core_glue.cpp`) first calls `VirtualProtect` once over the whole range. Only if that fails (the range spans allocations or reaches uncommitted pages) does it walk the regions with `VirtualQuery` as before. The walk sets the same protection on every committed page, so the pages end up the same either way.
    - `LBP3_PROTECT=old` always walks.
    - A new `[protect]` status line counts calls per window, "fast" and "walked". It is a separate line so the `[perf]` line's format, which scripts parse, stays the same.
  - **A/B:** 3 + 3 alternating runs of `m5_np_route.sh` with `HOLD=20` and `LBP3_FRAME_SPLIT=1`. Both arms used the same exe and differed only in `LBP3_PROTECT`. All on the 165 Hz display, 0 GPU warnings.
    - Script `D:\Recomps\lbp3-wt\_tools\protect_ab.sh`; scorer `protect_score.py` (the fluid frames are those with `gfx_blocked_us` > 0). The scorer reproduces `gw0`'s 291 fluid frames and 4.03 ms median.
    - Files: `analysis\m5_pf_{old,new}{1,2,3}_*` in the clone.

    | arm | walk work | frames > 40 ms (worst) | standing work | graphics blocked, walk's fluid frames: median / p95 / max | recording (`record_us`) | main thread |
    |---|---|---|---|---|---|---|
    | old ×3 | 20.88 / 21.19 / 20.95 ms | 1 / 1 / 1 (41.3 / 50.0 / 50.0 ms) | 17.85 / 18.20 / 18.52 ms | 4.02–4.25 / 5.28–5.66 / 16.7–18.8 ms | 2.69–2.78 ms | 6.35–6.46 ms |
    | new ×3 | **18.30 / 17.99 / 18.25 ms** | **0 / 0 / 0** (33.5 / 33.7 / 33.6 ms) | 16.28 / 16.16 / 16.52 ms | **2.54–2.63 / 3.31–3.34 / 3.8–4.2 ms** | 1.88–1.90 ms | 4.53–4.64 ms |

    - **Results:**
      - Walk work: −2.8 ms (21.0 → 18.2).
      - Blocked median in fluid frames: −1.5 ms (−37%).
      - **The spike is gone:** 17–19 ms → ≤ 4.2 ms. It had come within 0.2 ms of the 20 ms grace in `gw5`.
      - The walk's single frame over 40 ms is gone too.
      - The old and new ranges don't overlap in any column.
    - **Why it is ~5× the prediction (~0.3–0.5 ms):** the prediction counted only the job threads' `VirtualQuery` inside the wait. In fact `Protect` runs about 1,100 times per frame, ~1.2–1.3 M calls over the level part of a run. Inferred from the columns that dropped: the calls include the main thread's own write faults (main −1.8 ms) and the processor's protects while it records (−0.85 ms). No per-thread count was taken.
    - **Fast path:** used for 100% of calls. 0 fell back to the walk, out of ~6.5 M calls in the new arm.
  - **Regression:** the build was swapped into the clone with a trap restore (`_tools\protect_regression.sh`).
    - `m1_checks`:
      - Thruster Save Changes recipe **3 of 3 OK** (20 savedata mounts each);
      - Introduction steps ≠ 1: 0.00% and 0.13%;
      - Bunkum 16 of 16 voice lines, and Needlepoint loads;
      - the Create sky is cyan.
    - `t_m2v_photo`: PASS.
    - `t_m2v_backup`: the backup and saves are unchanged; 6 dialogs on the overlay.
    - `t13_coop`: PASS.
    - 0 GPU warnings. **0 `nvlddmkm` events from 08:40**, which covers all six A/B runs and the regression.
    - Every one of the 11 game logs has `[protect]` lines: ~21 M calls, **0 fell back**.
  - **Ticket 10's risk:** no wait, label or release moved. The chain is faster, which shifts its race timing, and the Thruster recipe still passes 3 of 3. With the spike at ≤ 4.2 ms on this machine, the margin to the 20 ms grace is much wider. A slower CPU could still cross it, which is gfx_wait.md's warning, but it is less likely.
  - **What's left:** the fluid block is still ~2.5 ms median in 54% of walk frames. Fixes 2–5 of gfx_wait.md still apply, and fix 2 (fewer faults) is the next lever on the game's side.
  - **Offline tests** (the build's own test programs): `osktest` 263, `dlgtest` 154, `cooptest` 81, `imetest` 403, `jpegtest` 157, `dialogtest` 368 checks, 0 failed. `padtest` waits for a pad, so it wasn't run.
  - **Not done:** the note's trace-on confirmation (`vq_us` → ~0); the `[protect]` counters stood in for it. Skipping the handler list's copy in `Dispatch` was also left out.
  - The human's saves are identical to `human_play_20261111_2104`. No marker, kept copy or swapped exe is left, and no game is running.
- 2026-11-12, **`fix/protect-fast` merged into `port`** (`0570df8`, runtime only; submodule unchanged at `d5af2ae7`), pushed; built and the offline tests pass on the merged clone. Heavy walk work ~21 → ~18 ms, fluid spike ~17 → ~4 ms, frames over 40 ms 1 → 0. `LBP3_PROTECT=old` restores the old path.
- 2026-11-12 afternoon, **frame generation merged into `port`** at the human's request ("merge it into port"), after their 37-minute session on the 240 Hz display ("it feels smooth"): `port` `7d04e28` (runtime; `exp/fg-gpu` `83e36d3` + `play.bat` with `FRAMEGEN=1` by default), submodule `port` `55d38d87` (capture hooks), both pushed. The runtime default stays off (`LBP3_FRAMEGEN` unset), so the test scripts are unchanged; `play.bat` turns it on (`FRAMEGEN=0` there turns it off). The human's session (`analysis\play_fg_log.txt`, `framegen.csv`): new frames every 16.67 ms (p95 16.71) at 240 Hz; 92.5% of real frames generated (4,579 drops in 24 long stretches where the game didn't step, 294 single drops on 0/2-step flips, 0 too late); generation ~0.8 ms worker + 1.6–1.8 ms GPU; real frames 25.0 ms from slot to scan-out; 84 frames over 40 ms, all at first entries into Manglewood levels and boot (pipeline builds). Not rerun on the final commit with it off: `m1_checks.sh` (Bunkum, sky, Thruster); the off path is flag checks plus null hooks, and lane E's off runs used the same code. Details: perf/fps60.md, perf/fg_*.md.
- 2026-11-12 evening, **night checks** (stopped partway at the human's request), in perf/res1440_experiment.md:
  - `port` `7d04e28` with frame generation off passes milestone 1's checks, the photo, backup and keyboard tests, and `t13_coop`.
  - The 1440p build with frame generation passes milestone 1's checks: Introduction 0.00%/0.00%, Thruster 3/3, Bunkum 16/16, sky cyan.
  - At 1440p, the photo, backup, keyboard, co-op, heavy-route and stills checks are still to run.
- 2026-11-12 evening, **1440p and the audio fix merged into `port`** at the human's request ("go ahead and merge 1440p and audio fix"): `port` `6d212bc` (`exp/res1440-audio` `c1d74ff` plus `play.bat` with `RES=1440` by default), submodule `port` `56120048` (the 1440p shader literals), both pushed. The runtime defaults are unchanged (`LBP3_RES` unset means 1080p), so the test scripts still run at 1080p. `play.bat` turns on 1440p and frame generation; `RES=1080` and `FRAMEGEN=0` there turn them off. The audio downmix is on by default for stereo devices (`LBP3_DOWNMIX=sdl` restores SDL's). Checks before merging:
  - the 1440p build passed milestone 1's checks (night checks);
  - the human played it and checked the save and load dialogs;
  - the human listened to the audio fix, and their session's levels are in ticket 16;
  - built, and all 8 offline test programs pass on the merged clone.
  Not yet run at 1440p: the photo test, the scripted keyboard test, co-op, and the heavy route. The brightness investigation (ticket 17) continues separately.
- 2026-11-12 night, **first-visit freezes: compiles don't repeat** (perf/first_visit.md). No pipeline key in either cache folder was ever rewritten (0 of 5,587), and no pipeline-cache warnings appeared: shadPS4's key cache with its WarmUp at boot, plus NVIDIA's driver cache, already make each pipeline a once-ever compile. So there's no persistence fix to build. 30 of the 46 story level archives are still unseen (Ziggurat, Bunkum, and Manglewood's Titan level and cutscenes); each freezes 0.3–2 s once. The 1440p cache folder is separate, so the 8 levels first seen at 1080p would compile once more at 1440p; seeding it from the 1080p cache is an option (not built). Draw-skipping stays the human's call; a sketch with its constraints is in the doc.
- 2026-11-12 night, **full verification of `port` `2cd464a`** (perf/verify_2cd464a.md):
  - **Runtime defaults** (DLC on in every run): milestone 1's checks, photo, backup, keyboard, co-op and the heavy route all pass. Walk work 19.94 ms, 0 frames over 40 ms.
  - **`play.bat`'s settings** (1440p + frame generation): all of the same pass. The 1440p photo is correct by eye; walk work 22.70 ms with 0 frames over 40 ms; 99.1% generated in the level. The night's 38.3 ms Introduction window was noise at a load; the steady 1440p cost is about +3 ms of work.
  - 0 nvlddmkm events.
  - **Options:** no crash in 4 sessions and about 45 presses, with or without `LBP3_MY_UNLOCKS`. But the import failed ("Failed to import objects") on two fresh story saves, each time with a failing `LBPxSAVEx12x350` mount right after it started. The human's crashing session mounted 12x350 just before the crash. Lead for ticket 19, not traced.
  - **`play.bat` itself:** boots with every setting applied and closes cleanly; the only save change is the profile rewrite (snapshot `human_play_20261112_2318`).
- 2026-11-13 about 00:45, **the shared shader cache merged into `port`** (`9625c5a`, submodule `553be3ba`), pushed, and the merged cache installed (previous cache kept as `CUSA00473_pre_shared_20261113_0043`). 1440p sessions now reuse everything compiled at 1080p and the other way round; only the 17 rescaled shaders compile separately, under salted keys. Booting to the Pod costs about 0.65 s more at 1440p (about 4,000 keys warmed up against 3,262). The full regression in both configurations passed beforehand (perf/shared_cache.md). The 27 extra shaders from the full scan are held until the x-ray/outline effects are seen in game.
- 2026-11-13 about 03:40, **fix/pipeline-warmup merge-ready** (perf/pipeline_warmup.md). It ports from LBPPC (emurphy04) aad6a91, GPL-2.0-or-later, the parallel pipeline preload and a persistent driver `VkPipelineCache` (`driver.vkcache`, with our own header checking the GPU, driver version, UUID and size). Branches: shadPS4 `e1d0d978`, runtime `a30fb18`, both pushed.
  - Boot to the Pod at 1440p: about 5.0 s against 5.7 s.
  - First boot with a new driver cache: 6–12 s of preload, once.
  - Screenshots within noise; m1, photo and co-op pass at play.bat's settings.
  - Switches: `LBP3_PIPELINE_THREADS=1`, `LBP3_VK_PIPELINE_CACHE=off`.
  - The serial read phase (about 3 s) is the next lever.
- 2026-11-13 about 08:35, **the LineLoop crash fix merged into `port`**: runtime `5fbc009`, submodule `fd3f9cbb` (primitive type 18 drawn as a line strip with a warning, instead of shadPS4's UNREACHABLE exiting the game). Found by the warm-up tour when it entered c01_intro straight from the Pod. No user log ever drew it, but `port` would have crashed on any LineLoop draw. A crash-only change: it builds, the offline tests pass, and it's pushed. The same fix is in `bestofboth` (`24e4e494`), where it's required because the tour put a LineLoop pipeline into the best cache.
