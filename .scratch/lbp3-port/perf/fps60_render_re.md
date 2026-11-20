# 60 fps: the renderer side (static RE, 2026-11-12)

This note asks whether 60 fps output is feasible, judged from the renderer. It covers two routes:

- **(a)** the game draws in-between states itself;
- **(b)** the port generates in-between frames from what the game already renders.

The sim clock is covered elsewhere (`fps60_spu_re.md` and the sim lane).

The game was not run for this note. The sources are:

- Ghidra decompiles of the eboot (`tools/ghidra_decomp.sh`);
- Python scans of `analysis\eboot.elf`;
- the game's file list (`tools/farc.py list`);
- the one fully traced frame in `analysis\t07_run1_stderr.txt`, lines 1597–2751. It is from Create mode on the 2026-11-10 build: 412 draws and 10 dispatches;
- its render-target dump `analysis\targets\dump_010247_1`, plus the other two dumps;
- the shader dumps in `analysis\shaders\dumps`;
- the frame-split CSVs;
- the runtime and shadPS4 code.

Every claim is labelled:

- **[dec]**: decompiled code or shader IR;
- **[log]**: log, trace, dump or string evidence;
- **[inf]**: inferred.

## Answer in short

- **(a) Drawing in-between states in the game: not feasible at reasonable cost.**
  - **One render per loop, no blend factor:** the main loop runs 0..N sim steps and then renders the current state once. The render entry takes no argument, so no blend factor exists [dec].
  - **What already exists:** the game keeps a previous camera, and for the velocity pass a previous-frame vertex position per mesh [dec]. That covers part of what interpolation needs.
  - **What is missing:** using those in every pass (main, shadow, reflection, particles, fluid, ropes, decals, UI) through an engine with no source code. It would also double the render cost of the main thread and the GPU.
  - **Effort:** months. The risk is very high.
- **(b) Frame generation in the port: feasible, and the inputs exist.** The game renders natively at 1920×1080:
  - a depth buffer;
  - a per-pixel motion-vector target every frame, for its motion blur;
  - a point in the frame where the 3D scene is finished and colour-graded but the screen HUD is not yet drawn.

  What is missing:
  - capturing those three images at the right moments in shadPS4;
  - an FSR 3 frame-interpolation pass on the Vulkan device;
  - 60 Hz presentation.

  Effort: about 2–3 weeks of sessions. Risk is medium, and it is mostly about quality:
  - a 30 fps base is below what FSR 3 is tuned for;
  - the motion vectors are 8-bit;
  - the in-world UI (the popit) has no motion vectors.

## 1. Does the renderer ever draw a state between two sim steps?

**No, as far as the code shows.**

- **Main loop** `FUN_00009d70` [dec] (profiler label `BeginFrame`):
  - It zeroes the frame's step counter (`world+0x3e0`).
  - It loops: take the next tick or input frame (`FUN_003ae640(DAT_0130d5c0, …)`, which ends the loop when none is queued), run one world step (`FUN_002651d0(DAT_012ecc80)`) and increment the counter. A step cap (`local_1508`) and network conditions also end the loop.
  - It then runs per-frame updates: `FUN_00256f90`, `FUN_00877810`, `FUN_001a8c70`, `FUN_00257070`/`FUN_002570f0`, `FUN_00257000`, `FUN_00266300` and `FUN_003f40e0`.
  - Then it renders once: `FUN_000043a0()` at `0xba2f`, with no parameters.
  - After that comes `FUN_0005caf0(1)`, the submit side [inf].
  - So a frame with 0 steps renders the same world state again. That matches upstream's camera check, where a 0-step flip showed "near-zero" scroll (NOTES.md, camera snaps) [log].
- **Lockstep:** the step count comes from a lockstep network layer. It takes the minimum tick across players (`DAT_0130d5c0`/`DAT_0130d5c8`, the "game host") [dec]. The strings `Game.SimFrame`, `Predicted` and `DeterministicRandom…` fit a deterministic fixed-step sim [log].
- **Render path:**
  - `FUN_000043a0` prepares the things' render data on the main thread [dec].
  - It then calls `FUN_0042d6e0`, the frame render. That function is bracketed by `RendererPS3.cpp` lines 0x1115–0x120d [dec].
  - `FUN_0042d6e0` calls `FUN_0042deb0`, the scene render. Its labels in order are `ESceneView_Main`, `AnnoyingCopyForRefraction`, `DrawTethers`, `EarlyClears`, `DrawPoppet`, `DrawCreatorHuds`, `RenderPlanets`, `RenderPod`, `RenderExtras` and `RenderExtrasPostComp` [dec].
  - No render thread is named. The threads are `main`, `SceneThreadWorker0–4`, `JobManagerWorker0–3` and `Orbis::Gfx::Callback` [log]. The command buffers are built during the main thread's render, using job workers [inf].
- **No interpolated transforms:**
  - `GetRenderPos__Q5Thing` (native `0x835aa0`, which calls `0x3caf80`) returns the single matrix stored at render-mesh `+0xe0`, with no blend [dec].
  - The only other per-frame fractions found are frame-counted fades: an appearing object's alpha is `n/10.0` for n < 10 [dec, `FUN_000043a0`; the divisor is the float 10.0 at `0xe49c48`]. Where n is incremented wasn't checked. If it is counted per render rather than per step, such effects would run twice as fast at 60 renders a second [inf].
- **Previous-frame data exists, for motion blur only:**
  - **The camera:** the main view (`DAT_013a2a60`) holds a current camera block at `+0x20..+0x58` and a previous one at `+0x160..+0x198`. The native `ClearMotionBlur__` (`0x8545a0` → `FUN_003fffe0`) copies current into previous and rebuilds the matrices, so a cut has no blur [dec].
  - **The velocity pass's matrices:** it uploads `modelViewProj_old` = the transposed previous camera view-projection (`FUN_00465fb0`, view `+0x160`) [dec].
  - **Per vertex:** the velocity vertex shader `vs_0xed24e10192a32704` transforms vertex attribute 0 with `cb0[0..15]` (current) and a **separate vertex attribute 2** with `cb0[16..31]` (old). Both get `positionOffset` (`cb0[32..35]`) added. So the game keeps last frame's world-space positions per mesh, which carries object and skinning motion and not just camera motion [dec]. That the vertex streams are world space follows from the matrices being view-projection only [inf].
- **The flip rate:**
  - `FUN_0005d2c0(mode)` stores the mode in `DAT_01060a10` (initially -1). Mode 2 asks for 30 Hz (`sceVideoOutSetFlipRate(h, 1)`); any other mode asks for 60 Hz [dec].
  - Boot initialisation (`FUN_000aaee0`, with the labels `InitTimingLog`, `Initialisation` and `PostResourceInitialise`) calls it with 0 (60 Hz) at its start and 2 at its end. The game's start, `FUN_00005b90`, calls it with 2 [dec].
  - So the 60 Hz the log shows twice at boot is the loading phase, not a hidden 60 fps gameplay mode [dec + log: `flip rate 60 Hz` ×2 then `30 Hz`].
  - `FUN_0005caf0` reads the mode for its flip call (argument 2 or 3) [dec].
  - No gameplay caller passes 0 or 1. Which of the four callers runs when was not traced.

## 2. Motion vectors and depth

- **The velocity target:** a 1920×1080 RGBA8 UNORM at `17a818000`. It is the same address in the Create trace and in the Needlepoint target list (`reuploads.md`) [log].
  - **Clear:** each frame the masked-fill shader clears it to bytes `00 7f 7f 00` (`007f7f00`): R=0, G=B=0.498 [log].
  - **Writes:** pixel shader `fs_0xa9a72c3f` [dec IR] writes R=1, G=0.498+cb0[1]·Δy and B=0.498+cb0[0]·Δx, with A=1. Δ is the difference between the two clip positions from the vertex shader, as xy/w: the NDC delta, interpolated perspective-correctly. Which of the two is subtracted, and so the sign, wasn't checked; the encoding below assumes current − old.
  - **Scale:** the scene render sets `uvescale` to the constant at `0xe817b0`, which is (4, −4, 0, 0) [dec]. That this is the velocity shader's cb0 is [inf].
  - **Encoding:** G = 0.498 − 4·Δy_ndc and B = 0.498 + 4·Δx_ndc.
  - **Range per base frame:** ±0.125 NDC, which is about **±120 px horizontally and ±67 px vertically**. Faster motion clamps.
  - **Step size:** about **0.94 px horizontally and 0.53 px vertically** [inf, from the IR and the constant].
- **The pass:**
  - In the traced frame it is draws #295–324: 30 draws, VS `ed24e101`, PS `a9a72c3f`. It has its own 1920×1080 D32S8 depth at `178938000` (test and write) [log]. The main colour pass draws about 32 objects (#326–357), so most objects also draw velocity [log].
  - In the dump (`t19_…17a818000`), R=255 covers the floor, the walls and Sackboy, and R=0 the sky. G and B stay at 124–132 because the scene is still [log].
  - The ES+GS fins and fuzz path writes velocity too, through `uvewrite.epo/vpo/fpo` and the alpha-test and fuzz variants (PS `4299ba39`) [log + inf: the file names `uvewrite*` next to the `uvescale` uniform].
- **Who reads it:**
  - **Motion blur:** PS `c2e07acc` ×3 (#365–367) and `8d6de026` (#368) sample it with 5 taps along the motion [dec IR].
  - **The final composite:** `dof_comp_moblur.fpo` [log: file names].
  - The script natives `SetFrameBlur__ff`, `SetFrameUseBlurForTransitions__b` and `ClearMotionBlur__` control the blur [log].
- **Depth:** the main scene depth is the 1920×1080 D32S8 at `171478000` [log].
- **Anti-aliasing:** SMAA 1x, not TAA (#375–380: edge detection `8b720f44`, blend weights `3a0615bd` with the 160×560 area and 64×16 search textures, neighbourhood blend `8e1db4f9`, copy back `6d4d5238`) [log + the `smaa_*.fpo` names]. No temporal accumulation and no jitter were seen [inf]. That is good news for frame generation: there is no jitter to undo.

## 3. The UI and where a frame generator could hook in

The order in the traced frame [log]:

| # | What | Target |
|---|---|---|
| 326–357 | scene colour (14 PS) | `171fe8000` 1920×1080 RGBA8 (scene buffer), depth `171478000` |
| 359–371 | DoF and motion blur, composited back | scene buffer |
| 375–380 | SMAA, result copied back | scene buffer |
| 381–383 | post: a 1920×1080 at `17a018000` and two 480×270 (bloom chain) | |
| 392–416 | **in-world UI** (popit, cursor, font quads with R8 atlases), depth-tested against the scene | **scene buffer** |
| 418 | **final composite** `b0bb8b37`: scene buffer + bloom + LUT/grading textures | display buffer `160800000` (sRGB) |
| 419–422 | **screen HUD** ("Close Popit", gauges) | **display buffer, after the composite** |

- **The labels match:** `@@RenderPostCompUI`, `RenderAdditionalUIItems` and `RenderExtrasPostComp` [log].
- **Hook point:** the display buffer right after the final composite (#418) is the "HUD-less" image in final colour space. It still contains the in-world UI (popit and bubbles), which carries no motion vectors. The scene buffer after #380 has no UI at all, but it is not yet colour-graded, so it doesn't match the final image.
- **Identifying the composite:** 10 `finalcomp*.fpo` variants exist (water, planets crossfade, VCR, lowres and others) [log]. Hooking "the first full-screen draw into a display buffer" is more robust than matching one shader hash [inf].
- **Resolution:** native 1920×1080 for the scene, depth, velocity and display buffers in all three target dumps. No dynamic resolution was seen [log]. The 960×540, 480×270 and 1280×720–320×180 targets are half-res post-effects, bloom and the share or thumbnail path [inf].

## 4. The present path, and where frame generation fits

**Today:**
- At the flip interrupt, `videoout.cpp:249-268` calls `ReadbackDisplayBuffer`, which copies the display image to a host buffer and runs `Finish` (`renderer.cpp:193-232`).
- `WaitFlipSlot` waits for the frame's slot (30 Hz grid).
- `video::PresentPixels` hands the frame to the presenter thread.
- The presenter is SDL_Renderer on **direct3d11** with vsync [log: `window renderer: direct3d11`]. It uploads a streaming texture, draws the ImGui overlay layers, then calls `SDL_RenderPresent`.
- Cost per frame (`m5_pf_new1`, p50): readback 0.69 ms; the "GPU submit + run" phase (submit, run and `Finish`) 4.44 ms; main thread 3.8 ms [log].

**FSR 3 frame interpolation:**
- AMD FidelityFX, MIT licence, with frame generation decoupled from upscaling since FSR 3.1.
- It needs: the current colour (the HUD-less image, optional but wanted), depth, motion vectors (converted from the 8-bit encoding by a small compute pass), camera near, far and FOV, and the frame time.
- Its optical flow pass covers pixels without game motion vectors, such as the sky (R=0), particles, fluid and the popit.
- The Vulkan backend in FidelityFX SDK 1.1 is to be confirmed against whatever SDK version gets pinned, since later SDK releases moved towards DX12 [inf].

**Two ways to wire it:**

1. **Keep SDL D3D11** (smaller change):
   - Run interpolation on shadPS4's Vulkan device at the flip interrupt.
   - Read back two images per base frame: the generated one and the real one. That is +8.3 MB and about +0.7 ms per 33 ms.
   - Present both on a 60 Hz grid (the pacer already supports 60 Hz slots: `g_flipRate` 0, `WaitForFlipSlot(60.0/…)`).
   - ImGui overlays stay sharp on both frames.
2. **Replace the presenter with a Vulkan swapchain on shadPS4's device**, using FSR 3's frame-generation swapchain:
   - It drops the readback and `Finish`.
   - The ImGui overlay has to move to ImGui's Vulkan backend (shadPS4 already has one).
   - It is a bigger change, but better in the long run.

**Pacing:**
- **240 Hz:** 60 fps is every 4th vblank. Even.
- **165 Hz:** 2.75 vblanks per frame, so frames last 3, 3, 3, 2 refreshes (18.2/18.2/18.2/12.1 ms) unless G-Sync or VRR is on, or the display runs at 120 Hz [inf, arithmetic].

**Cost:**
- **GPU:** about 1 ms per generated 1080p frame on a 3090 (estimate, not measured), plus the motion-vector conversion (~0.05 ms) and one 8 MB image copy for the HUD-less capture.
- **Input lag:** the real frame N can only be shown after the in-between frame (N−1, N), which needs N. So real frames are delayed by half a base frame, **about 17 ms, plus the generation time** [inf]. That is less than the "about one frame (33 ms)" said earlier.

## Feasibility

**(a) In-game interpolation:**
- **What exists:** the previous camera; previous-frame world positions per mesh for the velocity pass; a single render entry.
- **What's missing:**
  - an interpolation factor;
  - a second render per step;
  - interpolated positions in every material's vertex path (2,143 pipeline keys; the main pass binds only current positions);
  - the same for shadows and reflections, particles, the fluid (the FLIP sim, `gamedata/spu/flip_*.sbu`), ropes, decals and lights;
  - render-rate counters such as the fades;
  - double the render cost per second: the main thread's render part plus "patch + copy" (4.9 ms) plus the GPU (4.4 ms) per render.
- **Rejected:** interpolating the camera alone. The camera follows Sackboy, so a 30 Hz Sackboy under a 60 Hz camera would judder on screen.
- **Effort:** months, through hooks into an eboot with no source. **Risk: very high.**

**(b) Frame generation in the port:**
- **What exists:** native 1080p colour, depth, per-pixel motion vectors including object motion, and a HUD-less point after the final composite; a pacer that supports 60 Hz slots; an ImGui overlay drawn last.
- **What's missing**, with rough sizes:
  - capture hooks in shadPS4 for the HUD-less copy, plus finding the depth and velocity images by address or by their writers: 1–2 days;
  - FSR 3.1 interpolation and optical flow on the Vulkan device: 3–5 days;
  - present and pacing, either the double readback or a swapchain: 2–4 days;
  - edge cases: cuts (hook `ClearMotionBlur__` or detect them), pause, Bink videos and menus (switch generation off), the popit or text smearing, motion faster than ±120 px per frame: 3–5 days.

  That makes **about 2–3 weeks.**
- **Risk: medium, mostly visual:**
  - FSR 3's tuning targets bases near 60 fps;
  - 8-bit motion vectors (about 1 px steps);
  - the in-world UI has no motion vectors;
  - about 17 ms of added lag.

  An external tool (Lossless Scaling) at 2× is a free preview of the worst case, because it has no game motion vectors and no HUD-less input.

## Not checked, and cheap follow-ups

- **The pass order in a story level:** one `LBP3_TRACE_DRAWS` frame in Needlepoint would confirm the order, the velocity coverage of the backdrop, characters and particles, and the target addresses. This note's trace is one Create-mode frame, and it needs the game.
- **Values in motion:** a velocity-target dump while walking would confirm the encoding with real values.
- **The flip call:** what `FUN_0005caf0`'s 2-or-3 argument means.
- **The fade counter:** where it is incremented, per step or per render. That only matters for (a).
- **Depth convention:** near, far and reversed-Z or not, needed by FSR 3. It can be read from the depth clears and the camera block.
