# LBPPC: graphics, compared with our port (2026-11-13)

Study lane B. Lane A covers the licence, shader compilation and architecture in `lbppc.md`.

## Sources

- **Repository:** https://github.com/emurphy04/LBPPC, `main` at **`aad6a91bb7035e242899b223a16b49d91fb194ea`** (2026-11-12, "0.1.2 hotfix").
- **How it was read:** through the read-only shallow clone at `D:\Recomps\lbp3-wt\_ref\LBPPC`, which lane A made (nothing was built or run), plus the GitHub API tree.
- **Files read:**
  - docs: `README.md`, `docs/rendering-fixes.md`, `docs/feature-status.md`, `docs/research.md`, `docs/releases/0.1.*.md`
  - `run.py`, and `src/probe.c` (`render_res_install`)
  - in `gpu/shadps4/video_core/renderer_vulkan/`: `scene_resolution.h`, `vk_scene_resolution.*`, `vk_temporal_upscaler.cpp`, `vk_frame_generation.h`, `lbp_camera.h`, `vk_object_motion.h`, `ui_composition.h`, `vk_rasterizer.cpp`
  - `gpu/shadps4/video_core/texture_cache/texture_cache.cpp`
  - `gpu/shadps4/shader_recompiler/backend/spirv/emit_spirv_context_get_set.cpp`
  - `gpu/fg401_dx12/*.h`, `gpu/shadps4/core/libraries/gnmdriver/gnmdriver.cpp`
- **Licence, in passing:** the repository is GPL-3.0-or-later (`LICENSE`, README "Licence"). Its shadPS4-derived files keep `SPDX-License-Identifier: GPL-2.0-or-later` headers, and `LICENSE-GPL-2.0` sits beside them. Lane A does the analysis. Until then, everything below is treated as **ideas to reimplement, not code to copy**.
- **Base:** LBPPC's runtime and graphics layer were imported from the Bloodborne PC port 1.9.5 (`bbport`, a shadPS4 derivative). Its video core is a different shadPS4 lineage from ours (PurpleGitty's base plus our `port`), so file-level comparisons are approximate.

## 1. Frame generation

| | LBPPC | Ours (`port` `34e3e8d`; perf/fg_*.md) |
|---|---|---|
| Generator | Vendor: FSR 3.1 frame interpolation (FireBurn/FSR-Vulkan, any GPU, 2×), DLSS-G (RTX 40 2×, RTX 50 up to 4×), and a native AMD path through D3D12 shared textures (`gpu/fg401_dx12`) | Our own motion warp on Vulkan compute (`fg_warp.comp`), byte-identical to the CPU version, about 1.6–2 ms of GPU at 1440p |
| Motion vectors | Computed by the port, RG16F, previous minus current. The camera is recovered from each draw's world-view-projection in its vertex buffer (`lbp_camera.h`: "LBP3 has no camera constants of its own"; the motion most draws agree on is the camera's). Object motion comes from re-matching G-buffer draws across frames (`vk_object_motion.h`), falling back to camera motion | The game's own 1080p RGBA8 velocity target, written for motion blur, decoded with the encoding measured in `fg_inputs.md`. Saturation and missing coverage are handled by our own colour block search |
| Depth | The scene depth at the upscaler's trigger | Not needed by our warp; captured for diagnosis |
| HUD | The picture "before its UI": LBP3 draws into the display buffer several times a frame, the finished-frame copy first and then the UI, and the copy is taken before the first UI draw (`vk_rasterizer.cpp` ~1017). FSR 3.1 interpolates the HUD-less frame and draws this frame's UI over it; DLSS gets it as its HUD-less input | The same split point: the HUD-less colour after the composite, with the HUD put back from the current frame by a final-minus-HUD-less mask |
| Pacing and latency | The presenter shows the generated frames before the game's frame, evenly paced. NVIDIA Reflex or AMD Anti-Lag 2 is on by default, with the 30 fps limit applied *before each game frame* instead of the game waiting on 60 Hz vblanks: "from 84 to 29 ms between a frame's start and its present" (`run.py`) | Generated, then real, half a period apart on our vblank and slot grid. Real frames reach the screen about 19 ms later than without frame generation (25 ms slot to scan-out on the user's 240 Hz monitor). No Reflex-style limiter |

**Verdict:** our generator stays; it works on the user's RTX 3090, where DLSS-G doesn't.
- **Worth adopting:** the *latency* idea, pacing the frame start rather than the flip (§6, idea 2).
- **Not needed:** their computed camera and object motion. The game's own velocity target already includes object motion, and our quality is measured.

## 2. Resolution

LBPPC has two paths.

**(a) The startup resolution patch, `LBPPC_RENDER_RES=WxH`** (`src/probe.c` `render_res_install`, checked against the original bytes like ours). It patches the same sites as ours:
- the video setup at `0x5bed0` (display width, height and pitch);
- the target setup in `0x424790`, `0x426dc0` and `0x427940`;
- the "texture16/texture32" read-back descriptions at `0x424c94…0x4271cb`;
- the region at `0x1517d`/`0x15180`.

**Checked against our `runtime/src/core/patches.cpp`:** every one of their screen-sized sites has an equivalent in our table. Ours are keyed by instruction address: `0x424c90` covers their operand `0x424c94`; `0x424c96`, `0x424cd9`, `0x424ce2`, `0x427049`, `0x42704f`, `0x4271b3`, `0x4271bc`, `0x424c89`, `0x4271c5` and `0x424beb` cover the rest. Their comment explains why those matter: left at 1080p, "they show a zoomed corner of the screen (a grey wash with a giant Sackboy)" behind the Pod computer and the pause-menu fade. Our patch set already has them.

Differences from ours:
- **The bloom chain:** they scale the 1280x720, 640x360 and 320x180 targets (`0x427d1d`, `0x427d3e`, `0x427d62`), "grow with the size but never shrink" (a centred 256x256 cut once read past a shrunk target at 720p). **We leave the chain at its 1080p sizes** (`res1440.md` treated it as resolution-independent). At 1440p ours therefore samples a relatively lower-resolution bloom: a quality difference, not a bug.
- **The region:** they size it to 0x2441400 × the pixel ratio, rounded up to 1 MB; ours is a fixed 68 MB. Both cover 1440p, but theirs scales to 4K.
- **The width limit:** "5120 wide stops the game … 4480 works."
- **Aspect and ultrawide:** the camera's aspect at `0x3fdbc3`; the 2D canvas at `0x1060a04` widened, with six centre and edge constants moved (`0xe675c0`, `0xe8f668`, `0xe90394`, `0xe903b8`, `0xe90448`, `0xe9046c`); and an online "aspect split" stub so co-op worlds step alike. Not needed at the user's 16:9.

**(b) Live scene-resolution presets** (`vk_scene_resolution.*`, `scene_resolution.h`). The game keeps rendering at 1920x1080 *logically*. Its 1920x1080 and 960x540 scene targets get host **proxy images** at the chosen scene size, which is `output / {1.0, 1.5, 1.7, 2.0, 3.0}`, so it can be larger than 1080p. Native-size images are kept for guest compute, integer loads, copies and readbacks (coherence: `ProxyWrite`, `Resolved`, `NativeWrite`).
- **No shader literal lists:** the recompiler converts `FragCoord` back to native 1920x1080 units through a push constant, `scene_size` (`emit_spirv_context_get_set.cpp` ~121: `value * (1920 / scene_width)`). Shaders that address native-size textures with 1/1920-style literals keep working without any list. That's the problem we solve with 17 hash-listed literal swaps, plus 27 more held unseen.
- **Temporal upscaling:** FSR 3, FSR 4 or DLSS reconstruct the output. "Native AA" (scale 1.0) runs on the HDR scene; the scaled presets run on the tonemapped frame.
- **Jitter without patching the game:** Halton(2,3) jitter is added through the push-data viewport offset, only for draws with scene depth, not full-screen passes (`SetProjectionJitter`, `vk_rasterizer.cpp` ~47 and ~1192).
- **"Sharp UI":** Scaleform draws at the output resolution, with the UI viewport scaled up from the 1920x1080 stage (`ui_composition.h`).
- **FSR 1** spatial upscaling with adjustable sharpening, for playing below the screen's resolution.

**Against ours:** at the user's 1440p, ours already renders everything natively at 2560x1440, the UI included, because the game's own display size is patched. Their proxy and `FragCoord` design is the more *general* answer to the shader-literal problem; it would retire both our literal list and the held 27. But it's a large piece of work (proxies, transit and resolve, depth/stencil resampling, a coherence state machine) and only pays off for render scales other than 1.0, or for temporal anti-aliasing.

## 3. Performance and synchronisation

- **Buffer writes reaching images, "BufferWriteImageSync"** (`docs/rendering-fixes.md`, `vk_rasterizer.cpp` ~2374, on by default):
  - **What it does:** before any shader writes buffer memory, images there holding GPU-drawn data are written back (oldest first), and they reload before their next use. It's general, with no rules per shader or target.
  - **Cost:** about 2.4 ms of GPU a frame at 1440p (about 166 MB of reloads).
  - **What it fixed:** the orange flares on score bubbles, and the 3D scene turning into a "black, streaky mesh" on sticker-bubble pickups and zombie deaths. The cause was the masked-fill compute shader `5c5e1be6` that we also handle.
  - **Ours:** the long-standing path (a pre-fill copy back to the buffer, then a re-upload at the next bind) plus `fix/rt-fills`, which turns whole-image single-value fills into clears (perf/reuploads.md) and cut re-uploads from 26 to 3 a frame. We do the same thing more cheaply for the common case.
  - **Gap:** we haven't checked their repro scenes (§6, idea 4).
- **Skipped copy-back on guest writes into GPU-written memory** (`LBPPC_WRITE_FAULT_NO_READBACK=1`, on by default: "faster in busy levels, nothing found wrong"). Worth a look in our buffer cache: the Diner slowdown may involve write faults (perf/gfx_wait.md: about 1,380 faults per fluid frame).
- **The 3D lighting volumes:** a 2D colour target at a volume's address renders straight into the volume's depth slices through a 2D-array view (`texture_cache.cpp` ~350): "Replacing it each time lost what was drawn and cost every frame a new image and a copy." Our expansion fix (upload only new layers, grow by powers of two) attacks the same cost differently, and it is measured. A direct-render path would remove even the remaining 19 expansions per Prologue-map frame. That's an option, not a need.
- **The stencil plane:** "LBP3's stencil address is its depth buffer's": linking that copy as a stencil plane made a new image at every depth bind, 270,000 images in five minutes, and halved the frame rate (`texture_cache.cpp` ~939). **Our logs show a stable image count** (`[video]` "0 new images" in steady play), so it isn't present in our lineage, as far as we can tell.
- **Geometry shaders over NVIDIA's 1,024-component limit:** they *split the shader over invocations* (`emit_spirv.cpp` ~439). Ours declares the real vertex count, 4, from the copy shader, plus a guard that drops and logs anything over the limit; we measured 0 dropped draws. Theirs is more general if a bigger geometry shader ever turns up.
- **An SPU barrier at submit-and-flip** (`LBPPC_SPU_BARRIER`): an opt-in diagnostic that waits for the frame's SPU jobs before showing it. Not on by default.
- **Parallel draw preparation off for LBP3** (`BB_DRAW_PIPE=0`): it "corrupts memory under LBP3". Not relevant to us.
- **Not found:** nothing about the fluid/graphics-queue waits (our perf/gfx_wait.md) or a Diner-style slowdown at higher resolution. Their docs say slow stretches outside shader compiles "have another cause", unidentified.

## 4. Rendering bugs

| Bug | LBPPC | Ours |
|---|---|---|
| Orange flares (glow piling up) | A compute clear never reached an additively blended target (a 2560x1080 target, One/One, read by the final composite), so each frame's glow added to the last. Fixed by BufferWriteImageSync | Not seen; our fills reach images. **Unchecked** in their scenes (§6, idea 4) |
| Streaky black scene on pickups | The same root cause | Unchecked in their scenes |
| Create sky red/blue swap | Nothing found (perhaps fixed in their newer shadPS4 base) | Fixed (`fix/clear-swizzle`) |
| Early flip (stale PatchedFlip marker) | Uses shadPS4's standard PatchedFlip NOP markers (`gnmdriver.cpp` ~2132); no stale-marker fix found | Fixed (`port` `64d6252`) |
| Dark Pod and fog white-out | Nothing found | Known pitfalls of our fill path; fixed and guarded |
| Geometry-shader stages | Split over invocations (above) | Real vertex count plus guard |
| Brightness screen hang | shadPS4 compatibility reports: "froze on the brightness screen" (`docs/research.md`) | Not an issue here |

## 5. Brightness and exposure (our ticket 17, parked)

**Over-brightness of the mid-tones isn't mentioned or fixed in LBPPC.** Two leads for ticket 17 came out of reading it:

1. **Glow that piles up from a missed clear.**
   - **Their method:** dump each composite input across frames and look for one that only ever gets brighter (`rendering-fixes.md` steps 1–4).
   - **Our checks so far:** `LBP3_RT_FILLS=old` and zeroing the bloom feedback changed nothing, which argues against it, but we never dumped every composite input across frames.
   - **Cost:** cheap to run on the WELCOME shot with the existing capture tooling.
2. **LBP3's own brightness setting.**
   - **The evidence:** the game has a first-boot brightness calibration screen ("froze on the brightness screen" in three shadPS4 compatibility reports).
   - **Not checked:** whether that setting feeds the final composite. Ticket 17 found "1.3 × scene + bloom" and a mild colour grade from constants. If the 1.3, the grade or a gamma term comes from the profile's brightness value, a test save or the user's profile could simply hold a brighter value than the PS4 default the YouTube reference used.
   - **Check:** find the option in Settings and its stored value in the profile, change it, and see whether the composite's constants move.

## 6. Ideas worth adopting, ranked

Effort is in our codebase; code is reimplemented unless lane A clears reuse.

1. **Ticket 17 leads (§5).**
   - **For the user:** possibly the cause of the over-bright mid-tones.
   - **Effort:** small, about an hour of game time for both checks (dump composite inputs across frames; the brightness setting and its constants).
   - **Their files:** `docs/rendering-fixes.md` for the method.
2. **A lower-latency frame limiter.**
   - **For the user:** less input lag, especially with frame generation on, where real frames currently reach the screen about 19 ms later than without it. They report 84 → 29 ms from frame start to present.
   - **The idea:** pace the start of each game frame (sleep before the game samples input) rather than holding the flip on a vblank slot; optionally NVIDIA Reflex markers.
   - **Effort:** medium. It touches our pacing (`videoout.cpp` WaitFlipSlot, the frame-anchored clock) and must keep steps ≠ 1 and the Thruster recipe intact.
   - **Their files:** `run.py` (BB_LOW_LATENCY, BB_FPS_LIMIT, BB_VBLANK_HZ=0), `vk_low_latency.cpp`.
3. **Scale the bloom chain at 1440p.**
   - **For the user:** slightly finer bloom, matching the render size.
   - **Effort:** small: three more same-length patch pairs (`0x427d1d`, `0x427d3e`, `0x427d62`; their "never shrink" rule), memory checked, and an A/B screenshot comparison.
   - **Their files:** `src/probe.c` `targets[]`.
4. **Check their two buffer-write bugs in our port.**
   - **For the user:** confidence that our cheaper fill path covers them.
   - **Effort:** small: run their repro scenes (score bubbles; a sticker-bubble pickup in Needlepoint Peaks; the "Gates of Stitchem Manor" movie on the Prologue map, about 10 s in, no input) and watch for flares or streaks. Only if one shows would we need something like their general write-back and reload.
   - **Their files:** `docs/rendering-fixes.md`.
5. **Look at skipping the copy-back on guest writes into GPU-written memory.**
   - **For the user:** possibly faster busy levels; relevant to the Diner investigation.
   - **Effort:** medium, and risky (our dark-Pod and fog history), so it needs the full regression and screenshot comparisons.
   - **Their files:** `buffer_cache.cpp` ~272.
6. **Proxy scene targets plus `FragCoord` conversion.**
   - **For the user:** 1440p and 4K without per-shader literal lists (it retires the held 27); render-scale presets; a base for temporal AA or upscaling.
   - **Effort:** large: proxies, transit and resolve, depth/stencil resampling, coherence.
   - **Their files:** `vk_scene_resolution.*`, `scene_resolution.h`, `emit_spirv_context_get_set.cpp`.
7. **Temporal AA ("Native AA") with jitter through the viewport offset.**
   - **For the user:** better anti-aliasing than SMAA 1x.
   - **Effort:** large. It needs higher-precision motion than the game's 8-bit vectors (their camera and object motion) and a temporal resolver.
   - **Their files:** `vk_temporal_upscaler.cpp`, `vk_rasterizer.cpp` SetProjectionJitter, `lbp_camera.*`, `vk_object_motion.*`.
8. **Area-scaled region and the width limit,** if we ever offer 4K.
   - **Effort:** small.
   - **Their files:** `src/probe.c`.
9. **Ultrawide (aspect, the 2D canvas, the centre constants, online aspect split):** not needed on 16:9 monitors.
10. **Vendor frame generation (FSR 3.1 FI, DLSS-G):** not needed. Ours works on the user's GPU; DLSS-G needs RTX 40 or newer, and FSR 3.1 is still waiting on the user's build permission.
