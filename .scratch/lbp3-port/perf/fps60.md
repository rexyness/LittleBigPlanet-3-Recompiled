# Can LBP3 run at 60 fps? (2026-11-12)

The human asked "do you think 60fps is attainable?" and then "do proper analyses on the codebase / via RE". Five lanes answered: one game experiment, three reverse-engineering lanes and one prior-art search. Each claim below is labelled **measured** (game runs), **decompiled** (Ghidra on the eboot or `spu.prx`), **prior art** (other projects' published work) or **inferred**. The details are in the lane files:

- [fps60_experiment.md](fps60_experiment.md): forced 20/30/60 Hz flips, branch `exp/flip60` `3ddb0a1`, local only
- [fps60_timing_re.md](fps60_timing_re.md): the eboot's tick, step and flip rate
- [fps60_spu_re.md](fps60_spu_re.md): the `spu.prx` job module (physics, fluid)
- [fps60_render_re.md](fps60_render_re.md): the renderer, motion vectors, UI order and the present path
- [fps60_prior_art.md](fps60_prior_art.md): GoldHEN, illusion, RPCS3, PSP, Vita, LBPPC

## Verdict

- **A true 60 fps game (60 simulation steps a second): no.** It is not practical and not worth it.
- **60 fps on screen with the game still stepping 30 times a second (frame generation in the port): yes.** About 2–3 weeks of work, medium risk (visual).

Correction to the first answer in chat: at forced 60 Hz the game does **not** show each frame twice. It runs at double speed (measured below).

## Why a true 60 doesn't work

- **One simulation step per frame shown, at any rate (measured).** Steps per second in the Pod were 20.0 at 20 Hz, 30.0 at 30 Hz and 59.6 at 60 Hz. The Legend of Bunkum cutscene takes the same ~4,300 steps at 30 and 60 Hz, so at 60 it lasts 55 s instead of 108 s. Speech plays at normal speed over it, so 12 of 15 voice lines overlap. The Introduction's scripted walk reaches Newton at 23 s instead of 52 s. The camera moves ~2 px on every frame at both rates: no duplicates and no in-between frames.
- **No clock drives the step: the behaviour is measured above; the mechanism is partly decompiled.** The host thread wakes on a hard-coded 33,333 µs `select()` timeout and builds input frames without reading a clock (decompiled). Who queues the per-frame input wasn't found, so "steps follow the loop's rate" is inferred from the code and confirmed by the measurements. The main loop runs up to `MaxCatchupFrames` (3) steps per pass, and each step is a call with no time argument. The flip rate is requested only at boot (60 Hz on the loading screen, then 30 Hz for good). No setting, PS4 Pro check or hidden option asks for 60 in play.
- **The step length isn't one number (decompiled).**
  - **Physics:** rigid bodies get a step of exactly 1.0; per-step constants sit in `spu.prx` (8 constants, ~10 addresses).
  - **Frame↔second conversions and thresholds:** 30.0f is read at 150 sites in 107 functions and 60.0f at 73 sites; about half of a sampled 30 functions are these conversions, the rest spatial, angle or UI values.
  - **Game logic counts frames:** `ModDeltaFrames`, `AliveFrames`, `JumpFrame`.
- **Level data stores timing in steps, user levels included (prior art, LBP1 format; inferred for LBP3).** It holds a world `frame` counter, `simFrame`, and emitter frequency, phase and lifetime as integers.
- **Prior art agrees.**
  - No working 60 fps patch was found for the main-series engine (PS3 LBP1–3, PS4 LBP3) in the places checked: GoldHEN, illusion's repos, RPCS3's patch file, VitaGrafix and shadPS4's patches. The RPCS3 wiki pages and the Orbis Unjailed list couldn't be read (403/404).
  - An LBP1 PS3 project got 2.1× speed from uncapping. Its partial fix took gravity and jump retunes, an animation-clock patch and 213 resampled animation clips.
  - PSP LBP (a different studio's engine variant): a 2015 cheat broke loading; a static-recompiler project reportedly reached 60 Hz logic at stock speed, but only after patching 68 code sites (its package is private).
  - LBPPC (github.com/emurphy04/LBPPC), another native PC port of this same dump, by its own README ships "60 FPS" as DLSS or FSR 3.1 frame generation and "the game still steps 30 times a second, as on PS4". Its website calls native 60 "the hardest one on the list". These are the project's claims; it has published no timestep analysis.
- **Render-side interpolation in the game is also out (decompiled).** The main loop renders once after its steps with no blend factor. A dormant PAL mode (byte `0x120ac62`, never written) draws rigid bodies at k/5 of a step for a 25 fps output. It has no path for soft bodies (Sackboy), animation, particles or the camera, so the in-between frames would judder. Adding all of those is months of work at very high risk.
- **The budget at 60 would be tight anyway (measured).** Introduction work was 15.8–16.3 ms with 12–19% of frames late; the heavy Needlepoint walk would average ~15.6 ms (inferred), and an average that close to 16.7 ms means a share of frames miss, as in the Introduction. The 0/2-step rate rose to 10–12% (camera snaps would return).

## Why frame generation works

- **The game already renders what a frame generator needs (decompiled; one traced Create-mode frame, address matched in Needlepoint).**
  - **Motion vectors:** a 1920×1080 RGBA8 velocity target (`17a818000`), written every frame for motion blur, with per-vertex previous positions (object and skinning motion, not just the camera).
  - **Depth:** the D32S8 at `171478000`.
  - **Native 1080p:** no dynamic resolution.
  - **No jitter to undo:** SMAA 1x, not TAA.
- **There is a clean point before the HUD (decompiled, one frame).** The screen HUD is drawn onto the display buffer after the final composite. In-world UI (the popit, the cursor) is in the scene and has no motion vectors.
- **The 30 Hz budget has room (measured).** The heavy walk takes ~18–19 ms of work against 33.3 ms. Generation costs ~1 ms of GPU per frame on the RTX 3090 (estimate).
- **Input lag: about +17 ms plus the generation time (inferred).** A real frame waits half a 30 fps frame for the in-between one. Responsiveness stays that of 30 fps; motion looks like 60.
- **Plan (from fps60_render_re.md):**
  - Run FSR 3.1 frame interpolation (open source, Vulkan) on shadPS4's device, using the game's own vectors and depth.
  - Read back two frames per 33 ms (+0.7 ms). The ImGui overlays (keyboard, dialogs) stay sharp on top.
  - Pace at 60 on the existing slot grid. 240 Hz is even (4 refreshes a frame); 165 Hz alternates 3/3/3/2 unless G-Sync/VRR is on or the display runs at 120 Hz.
  - Keep it opt-in at first, with an off switch.
- **Risks:** FSR 3 is tuned for a ~60 fps base, and 30 is its hardest case. The vectors are 8-bit (~0.94 × 0.53 px steps, ±120 × ±67 px range). The popit has no vectors. The pass order and the vector encoding come from one Create-mode frame on the 2026-11-10 build, before the GS stages were turned on (ticket 15: those draws include motion-vector passes), so the checks below must run on current `port`. The "2–3 weeks" is a static-analysis estimate that rests on two unconfirmed points: that FSR 3.1 interpolation can run outside its swapchain proxy with this readback presenter, and how 8-bit vectors look from a 30 fps base.

## Before building it

0. The human tries generated 60 from a 30 base with an external tool (Lossless Scaling, 2×) on a heavy level. Whether it feels better than real 30 is the one thing no lane measured. The build is the human's call.
1. One draw trace in a story level, to confirm the pass order and what draws velocity there.
2. One velocity dump while walking, to confirm the encoding and the sign with real values.
3. Pick and pin the FidelityFX SDK version (FSR 3.1, Vulkan backend) and check its licence (MIT).
4. Optional and cheap: set the dormant PAL byte `0x120ac62` once to see whether that path still runs on PS4. It is a curiosity, not a route to 60.

## Prototype (2026-11-12, the human: "give it a try with frame gen")

Built on branch `exp/fg-gpu` (runtime `83e36d3`, shadPS4 `55d38d87`); **merged into `port` `7d04e28` on 2026-11-12** after the human played it ("it feels smooth"), with `play.bat` turning it on by default. Lane write-ups:
- [fg_inputs.md](fg_inputs.md): hook points, the vector encoding (p in frame k was at p − m in k−1; m.x = (B−127)×0.941, m.y = (G−127)×0.529 px), dump sets
- [fg_present.md](fg_present.md): the 60 Hz present path, the game side unchanged
- [fg_warp.md](fg_warp.md): our warp generator, skip-one PSNR 23.9–33.4 dB against blend's 17.9–23.0
- [fg_gpu.md](fg_gpu.md): the warp on Vulkan compute, byte-identical to the CPU version, ~1.1 ms of passes
- [fg_fsr31.md](fg_fsr31.md): FSR 3.1 study; the build is blocked pending the human's choice

In game (165 Hz display): Needlepoint walk work 19.6 ms on against 19.0–19.2 off; real frames reach the screen +19.3–19.9 ms later; generated frames on 91–96% of real ones. Steps ≠ 1, the Thruster recipe, the photo test and `[fills]` are unchanged. One visible artifact: the rotating "Loading…" text in the Prologue map's swoop.

To try it, double-click `D:\Recomps\lbp3-wt\fggpu\play_fg.bat`. It uses the same game folder and saves as `play.bat`, with logs in `analysis\play_fg_log.txt`.
