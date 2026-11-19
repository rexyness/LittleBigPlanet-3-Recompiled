# Lane (sethlikepie study): PC features and presentation, seth's port against ours (2026-11-19)

Part of [sethlikepie_vs_open_items.md](sethlikepie_vs_open_items.md). Read-only comparison of **sethlikepie/littlebigplanet3-pc `07cc7e7`** (studied from a temporary clone; check out `07cc7e7` from github.com/sethlikepie/littlebigplanet3-pc to follow the `seth:` citations) with our port (runtime `D:\Recomps\lbp3-recompiled` `main` `29dad5a`, v0.1.7) and our notes. Nothing was built or run. The only network reads were FireBurn/FSR-Vulkan's GitHub pages, at seth's pinned commit `c64f093`.

**Labels:**
- **read**: from code;
- **seth's claim**: from their comments, docs or patch-file notes, not measured by us;
- **inferred**: our reasoning;
- **web**: from a GitHub page summarised by the fetch tool, with no checkout.

**Verdicts** use five values: already have / portable as-is / portable with rework / not applicable / not worth it.

**Licences:**
- Every seth file used here is GPL-2.0-or-later: the shim, their shadPS4 changes, `src/runtime_lbp3_*.c`, and `src/runtime_pad.c`, which has no SPDX line and falls under the repo's GPL-2.0 `LICENSE`.
- So their code can go into our runtime (GPL-3.0-or-later) and into our shadPS4 fork (GPL-2.0-or-later), with their notices kept and credit given.
- `gpu/dlss_bridge` and `gpu/xess_bridge` are MIT (seth:gpu/dlss_bridge/LICENSE.txt:1-4).

**How seth's eboot hooks work:**
- Our eboot is byte-identical to seth's, so the addresses apply unchanged.
- Seth checks each call target before redirecting it, and otherwise logs "unavailable (code bytes differ)" (seth:src/runtime_lbp3_60fps.c:10652-10660).
- Our `core/sync_probe.cpp` already redirects calls through stubs in the same way (lbp3-recompiled:runtime/src/core/sync_probe.cpp:1-20).

## How to read the verdicts: what each feature writes

Online co-op runs both players' worlds in lockstep, so what a feature writes decides whether it can be on online. Every item below falls into one of four classes:

| Class | Meaning | Online |
|---|---|---|
| **A. Pad or touch synthesis** | Only changes what the game reads from player 1's pad | safe by construction (inferred: pad data is what lockstep exchanges) |
| **B. Host-side render only** | Vulkan viewport, sampler, extra passes, the presenter; no guest memory written | safe (inferred) |
| **C. Guest render state, restored** | Writes guest memory that the renderer reads, then puts it back | unproven; keep offline |
| **D. Simulation state** | Writes guest memory the simulation reads, calls the game's state-changing functions, or skips simulation steps | **diverges online**; offline-only |

**No online gate on seth's side (read).**
- None of the free camera, its "Pause the game", teleport or the Create mouse checks online mode.
- A plain `grep -i online` finds nothing in `gpu/shim/{lbp3gpu,window,lbp3_mouse,lbp3_freecam,lbp3_teleport}.cpp` or `src/runtime_lbp3_mouse.c`.
  - In `gpu/shim/lbp3_settings.cpp` the only hits keep the `lbp3_online=` ini keys (seth:gpu/shim/lbp3_settings.cpp:532-537).
  - In `gpu/shim/lbp3_overlay.cpp` the only hits are the About text and the page list (seth:gpu/shim/lbp3_overlay.cpp:1971-1975, 2426-2429).
- `runtime_online_enabled()` is used only in `src/runtime_online.c`, `runtime_netloop.c` and `runtime_http.c`.
- In `run.py` and `scripts/`, online mode only sets the environment and skips external `*online*` server patches (`--offline`) (seth:run.py:229-240, 385; seth:scripts/patches.py:191-201).
- Their online build therefore has class-D features live. If we port any of them, we must add the gate ourselves.
- Seth names eboot+0x254270 `G_IS_ONLINE` (seth:src/runtime_lbp3_60fps.c:137). The game uses it to choose between two thing render lists (seth:src/runtime_lbp3_60fps.c:2399). **That name is seth's claim; check it in Ghidra before using it as a gate.** Our runtime knows from its own state when it is online (`LBP3_ONLINE*`), which is the safer gate.

**What needs a Vulkan swapchain** (we have none: shadPS4 renders headless, and we read back every frame into an SDL D3D11 window):
- Reflex and Anti-Lag 2;
- seth's ImGui Vulkan backend;
- seth's FSR 3 frame-generation present path;
- the video recorder's NV12 conversion.

None of these comes over as-is.

---

## 1. Upscalers (DLSS SR/DLAA, FSR 3.1, FSR 4 / 4.1.1, XeSS, native TAA)

### What seth does

**Settings** (seth's claim, seth:docs/upscaler.md:5-16):
- Off, DLAA/Native AA, Quality 67%, Balanced 58%, Performance 50%, Ultra Performance 33%.
- One provider is chosen under Anti-aliasing: DLSS, FSR 3.1, FSR 4, FSR 4.1.1 or XeSS.
- Render size = output × ratio, with the width a multiple of 8, the height even, and at least 640x360. At 1440p output: Quality 1704x960, Performance 1280x720.
- **Only lines 1-30 of `docs/upscaler.md` are the LBP3 section.** The rest is lineage history (God of War III's frame, Bloodborne detection, `run.sh`, October dates). Its figures, such as the 7-8 fps on a Steam Deck and FSR 4.1.1 at 4.12 ms for 4K Balanced, may not be LBP3's.

**Render size against output size**, done by eboot patches at start (read):
- `patches/LittleBigPlanet3.xml` has two templates, "Internal Render Resolution" and "Output Resolution".
- `scripts/patches.py --render-res/--output-res` applies them (seth:scripts/patches.py:368-400; seth:run.py:375-385).
- "Output Resolution" is just the 3 display globals: width, height and pitch, in the VideoOut init (seth:patches/LittleBigPlanet3.xml:105-115).
- The template header says the 1080p display buffer, the UI and the pixel-sized effect constants keep their display values, and the game's own last pass scales the scene image into the display buffer (seth's claim, seth:patches/LittleBigPlanet3.xml:1-8). So the scene and the display buffers can legitimately differ in size, and the UI is then rasterised at the output size.
- **Two more fixes for when the sizes differ** (read sites, seth's claim on what they do):
  - **Screen-space refraction:** the scene pixel size for refraction (prize bubbles, glass) is read from the display globals at 0x42e820 and 0x430ce0, and their `lea`s are pointed at the render-size immediates.
  - **ScriptRenderToTexture** (0x46d250, video on the in-world cardboard TVs): its viewport and scissor are treated the same way (seth:patches/LittleBigPlanet3.xml:86-97, six "fixed" lines).
- A "live" path without patches also exists: guest buffers stay at 1080p and the host proxies are resized. The startup patch is the default again (seth's claim, seth:docs/upscaler.md:67-82).

**Jitter: neither an eboot patch nor a constant rewrite.** It is a host Vulkan viewport offset per draw (read):
- `viewport.x = (xoffset - xscale) * target_scale[0] + draw_jitter[0]` (seth:gpu/shadps4/video_core/renderer_vulkan/vk_rasterizer.cpp:5404-5407).
- Only draws that use the scene depth and have more than 6 indices get it (seth:gpu/shadps4/video_core/renderer_vulkan/vk_rasterizer.cpp:2154-2161). Small depth-tested world draws are an exception (seth:gpu/shadps4/video_core/renderer_vulkan/vk_rasterizer.cpp:1213-1229).
- The sequence is Halton(2,3) (seth:gpu/shadps4/video_core/renderer_vulkan/vk_temporal_upscaler.cpp:523-653).
- After the upscale, `Jitter()` returns 0, so post-processing and the UI are not jittered (seth:gpu/shadps4/video_core/renderer_vulkan/vk_temporal_upscaler.h:91-108).

**Mip bias** (read): the host sampler's LOD bias is set to log2(render/output) on draws with 5 or more colour targets, i.e. the G-buffer (seth:gpu/shadps4/video_core/renderer_vulkan/vk_rasterizer.cpp:3040-3048; seth:gpu/shadps4/video_core/renderer_vulkan/vk_temporal_upscaler.cpp:1787-1793).

**Motion vectors: computed, not the game's own:**
- **Camera vectors** (read): the main scene vertex shader's constants (VS `8af210a5e06f1b3e`) give the world-to-clip matrix (seth:gpu/shadps4/video_core/renderer_vulkan/vk_rasterizer.cpp:3756-3767).
  - `OnViewProjection` keeps the current and previous matrices (seth:gpu/shadps4/video_core/renderer_vulkan/vk_camera_motion.cpp:604-615).
  - A compute pass (`camera_motion.comp`) turns depth plus the reprojection into vectors (seth:gpu/shadps4/video_core/renderer_vulkan/vk_camera_motion.cpp:381-402).
- **Object vectors** (seth's claim from the header): G-buffer shaders keep clip positions per draw, matched across frames by vertex and index data, with camera motion as the fallback (seth:gpu/shadps4/video_core/renderer_vulkan/vk_object_motion.h:1-6). That needs changes in the recompiler.

**Where it runs, and the display-pass redirect** (read):
- The scene colour and depth are noted at the post composite (trigger PS `33c89263dcb0a389`, after SMAA) (seth:gpu/shadps4/video_core/renderer_vulkan/scene_profile.h:12-17, 44-58).
- `OnDisplayPass()` runs the upscaler at the first draw into the display buffer, **into** an output-size image (seth:gpu/shadps4/video_core/renderer_vulkan/vk_temporal_upscaler.cpp:379-406; seth:gpu/shadps4/video_core/renderer_vulkan/vk_rasterizer.cpp:1700).
- That draw's sampled scene image is swapped for the upscaled one (`RedirectSampled`, seth:gpu/shadps4/video_core/renderer_vulkan/vk_rasterizer.cpp:4361). The swap ends at the next draw or dispatch (seth:gpu/shadps4/video_core/renderer_vulkan/vk_temporal_upscaler.h:159-176).
- Bloom and the rest of post-processing stay at the render size.
- **The upscaled frame lands in the guest display buffer** (inferred from the code). In the patched super-resolution path the game's own display pass writes it there.
  - `DisplayOverride`, a presenter-side substitute (seth:gpu/shadps4/video_core/renderer_vulkan/vk_presenter.cpp:511-512), is only filled in the live and scaled path (`RedirectColor`, seth:gpu/shadps4/video_core/renderer_vulkan/vk_temporal_upscaler.cpp:2308-2330).
  - So our readback, D3D11 present and frame generation would pick the result up unchanged.

**The fix tail** (seth's claim, the F-numbers in the code). After the base pass, seth needed:
- F51: a stable unjittered depth copy;
- F61: overlay AA;
- F64: jitter for small world draws;
- F108 and F109: the redirect across every display-buffer draw;
- F114: an output-size copy of the scene depth. The UI is drawn into the output-size buffer with the render-size scene depth bound, so it was clipped to the render rectangle (seth:gpu/shadps4/video_core/renderer_vulkan/vk_temporal_upscaler.h:113-158).

**Providers**, all behind one interface:
- **FSR 3.1:**
  - It comes from the FireBurn/FSR-Vulkan submodule, pinned at `c64f093` and patched by 3 local patches. Patch 0003 fixes a frame-generation leak of about 64 MB/s (seth:gpu/patches/fsr-vulkan/README.md:1-13).
  - It is MIT per seth's comment (seth:gpu/CMakeLists.txt:115).
  - **web:** at `c64f093` the repo has checked-in shader headers. The README says "The checked-in headers under `generated/ffx-1.1.4/vk/fsr3upscaler/` are build inputs". `generated/ffx-1.1.4/vk/` also holds a `fsr3framegeneration` folder; we saw names only, and optical flow inside it is unconfirmed.
  - AMD's `FidelityFX_SC.exe` is only needed to *regenerate* those headers.
- **DLSS:**
  - `lbp3_dlss.dll` (MIT, adapted from IFreemz) is built with MSVC and statically links NVIDIA's `nvsdk_ngx_s.lib`. Its build file says "the DLL ships in releases" (seth:gpu/dlss_bridge/CMakeLists.txt:24-31).
  - NVIDIA's `nvngx_dlss.dll` is never shipped. The player adds it through the launcher, which copies it to `<data>\dlss\` (seth:gpu/shim/lbp3_dlss_file.h:1-25).
  - The NGX instance and device extensions are added when shadPS4 creates the device (seth:gpu/shadps4/video_core/renderer_vulkan/vk_dlss.h:35-38; seth:gpu/shadps4/video_core/renderer_vulkan/vk_instance.cpp:474-481).
- **XeSS:** `lbp3_xess.dll` (MIT) wraps Intel's `libxess.dll`. It is listed only when both files are present; "a release never has them" (seth:gpu/shim/lbp3_upscaler_support.h:83-85).
- **FSR 4:**
  - The v07 INT8 assets are not in the repo. The launcher downloads them from FireBurn/Q2RTX, "built from AMD's MIT-licensed FidelityFX source" (seth's claim, seth:launcher/lbp3_lang.py:28).
  - FSR 4.1.1 assets are extracted by the user from AMD's DLLs and are "never in a public package" (seth:packaging/default.nix:12-15; seth:docs/upscaler.md:316-323).
- **Native TAA:** their own `taa.comp` (GPL-2.0), which uses the same jitter and motion vectors (seth:docs/upscaler.md:34-47).
- **Selection and fallback** (read): `lbp3_upscaler_support.*` detects once at start what the PC can run and falls back DLSS → FSR 3.1 → off, with a reason the player can read (seth:gpu/shim/lbp3_upscaler_support.h:1-114).

### What we have

- **Real render sizes from 720p to 4480x1260 and ultrawide:**
  - The eboot patches are built from one table in groups (lbp3-recompiled:runtime/src/core/res_patches.cpp:46-161):
    - `core` = the display globals 0x5bf83, 0x5bf90 and 0x5bf9e;
    - `targets`;
    - `pairs` (DrawOffscreenComposite and the SMAA pass, plus two distortion floats);
    - `ui` (snap grid and NDC-to-pixel floats);
    - `bloom`;
    - `aspect`.
  - **Every group scales together from one size** (`Build(size, options)`, lbp3-recompiled:runtime/src/core/res_patches.h:38). There is no separate render size and output size.
  - The aspect split keeps the simulation's camera at 16:9 online (lbp3-recompiled:runtime/src/core/res_patches.h:28-33).
- **FSR 1 to the window** (EASU+RCAS, 1.92 ms best at 4K offline): built as `d06a0a1` on `exp/upscale` (notes:perf/upscale.md).
  - **No local or remote-tracking branch contains it now, and it isn't in `main`** (`git branch -a --contains d06a0a1` is empty). **Correction (main doc):** the tag `archive/exp/upscale` keeps it, so it is safe.
  - It upscales on Vulkan before the readback, so the readback is at the window's size.
- **4K is over budget** in heavy scenes: Needlepoint walk 35.7 ms of work and 120 frames over 40 ms, against 1440p's 21.8 ms and 1080p's 19.7 ms (notes:perf/resolutions.md).
  - Our notes put the extra cost mostly on GPU drawing, plus a readback and SDL upload of a 33 MB frame (notes:research/lbppc_vs_open_items.md §4).
- **Frame generation** is our own warp, from the game's own RGBA8 velocity buffer: 8-bit, and cleared before the composite (notes:perf/fg_inputs.md).
- **The FSR 3.1 frame-generation spike is blocked** because running AMD's bundled `FidelityFX_SC.exe` was refused (notes:perf/fg_fsr31.md, part 2).

### Mapping seth's split onto our table (partly done)

| Side | Our groups | Seth's template |
|---|---|---|
| **Render size** | `targets`, the integer sites of `pairs`, the MainRenderTargets region (0x1517d) and the pool site (0x5bf55) | the 0x827a14-0x8280a4, 0x82704d-0x8271cb and 0x824bf1-0x824d47 lines (base 0x400000 + immediate), area and heap (seth:patches/LittleBigPlanet3.xml:13-104) |
| **Output size** | `core`, and by the template's rule `ui` | "UI and pixel-sized effect constants" follow the display |
| **Not decided** | `pairs`' two distortion floats (0xe811f4 and 0xe811fc, which may count as a pixel-sized effect constant), `bloom` (seth's template doesn't scale the bloom chain) and seth's six "fixed" redirects | the redirects have no counterpart in our table |

### What lower-res rendering plus upscaling would buy (inferred, unmeasured)

- **The GPU scene cost drops to the render size; the readback and upload do not.** Output-size pixels still cross to the CPU and into D3D11.
- **4K Performance** (1080p render): roughly 1080p's walk work, plus the upscaler, a 4K UI and the 4K readback and upload. A guess: 25–30 ms against the 33 ms slot. It needs measuring.
- **The cheapest first step needs no upscaler:**
  1. Split our table (`core` and `ui` at the output size, the rest at the render size, plus seth's six redirects).
  2. Let the game's own last pass scale the scene bilinearly. That is seth's `LBP3_SR_DEBUG=1` path (seth:gpu/shadps4/video_core/renderer_vulkan/vk_temporal_upscaler.cpp:389-393).
  - This gives a 4K UI and text over a 1440p or 1080p scene, and measures the gain first.
  - Expect seth's F114 problem (the UI clipped to the render rectangle).
- **Moving a spatial upscale after the readback** (FSR 1 or bilinear on the D3D11 side) would also shrink the readback and upload to the render size. A temporal upscaler can't do this: it needs depth and motion vectors on the Vulkan side.

### Lockstep

- **Jitter and mip bias are host Vulkan state only** (read). Lockstep-neutral (inferred).
- **The render-size patches are the same kind we already ship.** SR sizes stay about 16:9; keep `aspect` off for them (inferred).
- **Regression items:**
  - Photo, thumbnail and icon capture (the CPU copies GPU targets at 0x4014f0) may come out sub-pixel jittered.
  - Our frame-generation warp reads the game's velocity and depth, which would then be at the render size and jittered: it needs rescaling, and its disocclusion checks see jitter.
  - The cut rule reads the camera at `0x13a2a60+0x20` (lbp3-recompiled:runtime/src/hle/videoout.cpp:613-623) and is unaffected.

### Licensing and redistribution

- **FSR-Vulkan and FSR 3.1:** MIT per seth. The web page shows a `LICENSE.txt` and a `NOTICE.md`, which we didn't read. Read both before shipping.
- **FSR 4 v07 and 4.1.x assets:** unclear provenance; the 4.1.x assets are extracted from AMD's DLLs. **Don't redistribute any of them.**
- **DLSS:**
  - Seth ships an MIT bridge that contains NVIDIA's static NGX library, and makes the player supply `nvngx_dlss.dll`. "NVIDIA's licence doesn't let us ship it" is seth's claim (seth:README.md:43-53).
  - **Open question for the user:** is it acceptable for our GPL-3.0 runtime to load, at run time, a bridge that links proprietary NGX code? We did not judge NVIDIA's or Intel's terms.
- **XeSS:** the same pattern; seth effectively ships it only for developers.

### Verdict

**Portable with rework, L.**
- The order: the render/output split (M), then camera motion vectors and jitter in our shadPS4 fork (M), then the trigger, the redirect and FSR 3.1 (M–L), then DLSS and XeSS (S–M each on top), then object motion (L).
- Risk: low for lockstep; high for regressions in the capture chain and frame generation.
- Touches: notes:perf/resolutions.md (4K over budget), notes:research/lbppc_vs_open_items.md §4, notes:perf/upscale.md.

**Worth trying soon, M:** take FireBurn/FSR-Vulkan with its checked-in shader headers, to unblock the FSR 3.1 spike (notes:perf/fg_fsr31.md).
- The upscaler headers are confirmed (web). The frame-generation headers are a folder name only, so check `generated/ffx-1.1.4/vk/fsr3framegeneration` for the interpolation and optical-flow permutations first.
- Take seth's patch 0003 (the leak) too.

**Cross-check:**
- Our notes call `3a0615bd0d8d3a5e` "the composite" (notes:perf/best_gfx.md §1).
- Seth's profile calls it the SMAA weights pass, with the post composite as `33c89263dcb0a389` and the display composite as `5ed16706fc9e0eb1` (seth:gpu/shadps4/video_core/renderer_vulkan/scene_profile.h:12-17; seth:gpu/shadps4/video_core/renderer_vulkan/vk_rasterizer.cpp:1869).

---

## 2. Mouse in Create

### What seth does (read, seth:src/runtime_lbp3_mouse.c, 1,196 lines; seth:gpu/shim/lbp3_mouse.cpp)

**When it runs:** on the main game thread before every simulation step. The step's call at **0xb2aa → 0x254450** is redirected (seth:src/runtime_lbp3_60fps.c:7238-7243, 10618).

**What it reads:** player 1's poppet mode through the game's own getters, the cursor point at poppet `+0x200`, and the render camera at `*0x13a2a60` (seth:src/runtime_lbp3_mouse.c:1-48, 794-806).

**How it moves the cursor:**
- **Motion → target:** the cursor's screen point plus the mouse motion is unprojected onto the cursor's layer plane. The pointer is hidden and captured (relative mode) while the cursor is out (seth:src/runtime_lbp3_mouse.c:993-1003; seth:gpu/shim/lbp3_mouse.cpp:117-124).
- **Mode 2, empty cursor (class D):** it **writes 7 copies of the cursor point inside the poppet** directly before the step (seth:src/runtime_lbp3_mouse.c:1007-1020).
- **Mode 1, holding an object or drawing material (class D):**
  - The 3 callers of the cursor-velocity function 0x4ba440 are redirected, and the hook **returns its own velocity**, "exactly the rest of the way to the mouse target" (seth:src/runtime_lbp3_mouse.c:1172-1194; seth:src/runtime_lbp3_60fps.c:10631-10636).
  - Square and blended brushes get their own drag rules (seth:src/runtime_lbp3_mouse.c:1095-1150).
- **The older path** (`LBP3_MOUSE_HOLDSTICK=1`):
  - It steers player 1's left stick in a closed loop, but also **overwrites the poppet speed settings** at `*0x13b2250+0x50..0x64` (class D; seth:src/runtime_lbp3_mouse.c:188-282).
  - **Its fallback without those settings is a proportional stick only: class A** (seth:src/runtime_lbp3_mouse.c:270-274).

**Buttons** (class A): only while a poppet is open, left = Cross, right = Square, middle = Triangle, back = Circle, and the wheel = one d-pad press per notch (seth:src/runtime_lbp3_mouse.c:174-181, 848-860).

**Coupling to their 60 fps mode:** part of the code is tied to it ("ring60" and "tether" blending, a cursor-set hook at 0x6f5276). We would strip that.

**`mouse_create_menus`:** saved and passed on (seth:gpu/shim/lbp3_settings.cpp:150-151; seth:gpu/lbp3gpu.h:106), but nothing in `src/` reads `menus_enabled`.

**Abandoned approach** (seth's claim): "Round 1 used the touchpad, which also scales / rotates the held thing ('weird placing')" (seth:src/runtime_lbp3_mouse.c:17, 186).

### What we have

- **`input/mouse_touch.*`** (lbp3-recompiled:runtime/src/input/mouse_touch.h:1-12): `LBP3_MOUSE_TOUCH=1`, with F7 capturing the mouse.
  - Moving the mouse slides one relative finger on player 1's touch pad.
  - The left button presses the pad, the wheel spreads or pinches two fingers, and Ctrl + wheel twists them.
- **It is class A** and safe online.
- **It is seth's round 1.** Our in-game test covered opening the Popit and picking an entry, not placing a held object (notes:best/input.md). "Weird placing" is unchecked on ours.

### Verdict

**Split it in two:**
- **Buttons and wheel → pad, while a poppet is open:**
  - **portable with rework, S–M, class A, safe online;**
  - it needs a pre-step hook to call the poppet-mode getter on the game thread.
- **Absolute-feel cursor** (the mode-2 writes, the velocity hook):
  - **portable with rework, M** (about 500 lines once the 60 fps parts are removed);
  - class D, so **offline-only, gated on our own online state.** Online falls back to `mouse_touch` or to buttons plus the proportional stick.
- Value: high for keyboard-and-mouse creators.
- Risk: offline low (every pointer is guarded by `rd_ok` and sanity checks); online none, if gated.
- Touches: notes:issues/12-milestone-2-create-mode.md, notes:best/input.md.
- **First, a cheap check:** place a held object with our touch finger.

---

## 3. Free camera and teleport

### Free camera: what seth does (seth:src/runtime_lbp3_60fps.c:6609-6623; seth:gpu/shim/lbp3_freecam.h:16-39)

**How it moves the view:**
- **seth's claim, from the header:** every present it puts the free camera into the **render camera block** (`*0x13a2a60 +0x20..+0x19f`) through the game's setter 0x963d10, and puts the game's bytes back at the end of the render stage (0xbc16), so "the game never sees the free camera".
- **Read:** it is re-applied through the setter whenever something rewrote the block, with the previous view×proj kept (seth:src/runtime_lbp3_60fps.c:6790-6812).
- That makes it **class C**: culling uses the free camera.

**Its other parts:**
- **Pause** (read): with "Pause the game" on, the simulation-step call is simply not made: `if (fc.on && fc.pause) return;` (seth:src/runtime_lbp3_60fps.c:7242). **Class D.**
- **Hide HUD** (F6, also without the free camera; read):
  - `ret` stubs on the scene draw's UI callbacks (seth:src/runtime_lbp3_60fps.c:7245-7246);
  - guard stubs on 14 call sites of the UI tree draws 0x654010 and 0x6538e0;
  - one site, 0x42f99e → 0x1948e0, the frame render's "script UI pass … thing scripts → draw natives" (seth:src/runtime_lbp3_60fps.c:6813-6822, 10478-10494).
  - "Never the logic" is seth's claim. The script pass is the least certain part.
- **Standalone install** without their 60 fps hooks: hooks at 0xb8ca, 0xbc16 and 0xba2f, plus its own sites (seth:src/runtime_lbp3_60fps.c:10652-10670).

### Teleport: what seth does

- **Read:** before the simulation step it finds player 1 through the level's PYellowHead list, checking ownership of each part (seth:src/runtime_lbp3_60fps.c:6846-6900).
- **Read:** **it moves him by calling the game's own warp 0xd5c10(thing, target, 1, 0, 1)**, then several resets: the creature state (0x129820 / 0x101340), rope 0xd9e20, the grab state, physics 0x72ae10 on him and any held thing, the bodies (0x17d580) and part 23 (0xe4150) (seth:src/runtime_lbp3_60fps.c:7170-7230). **Class D.**
  - It follows the game's own teleport, the script native TeleportSackthings (0x80c660 → 0x3ca080) that teleporter gadgets use (seth's claim, seth:src/runtime_lbp3_60fps.c:7169-7171).
  - A bare SetPosition "accelerated him extremely fast" and killed him (seth's claim, seth:src/runtime_lbp3_60fps.c:7172-7173).
  - The header comment still describes the older SetPosition path (seth:src/runtime_lbp3_60fps.c:6833-6840).
- **Refused** while paused, in the Popit, in menus or while loading (seth:src/runtime_lbp3_60fps.c:7122, 7151).
- **Slots:** F2–F5 load per-level slots and Shift+F2–F5 save them; T teleports to the free camera. Test commands `teleport=<slot | x,y,z | camera>` and `teleport_log` (seth:gpu/shim/lbp3_teleport.h:18-51).
- **Read-only report:** player 1's position and a "can be teleported" state every step (seth:gpu/shim/lbp3_teleport.h:21-30). `record_pad.h` defines a per-present game-state snapshot: positions, screen u/v, Popit and pause flags, `SimFrame`, frontend and loading (seth:src/record_pad.h:53-71).

### What we have

- **Remote input** (`analysis\input_cmd.txt`): per-player timed presses, sticks and `ime` (lbp3-recompiled:runtime/src/input/gamepad_sdl.cpp:382-399).
- **`tools/nav.py`:** recognises screens by thumbnail (lbp3-recompiled:tools/nav.py:1-10).
- **Read-only probes:**
  - `GetSimFrame__` and the camera at `0x13a2a60+0x20` (lbp3-recompiled:runtime/src/hle/videoout.cpp:578-623);
  - `LBP3_SYNC_PROBE`.
- No free camera, no teleport, no position readout.

### Verdict

- **Player 1 state readout** (read-only):
  - **portable with rework, S–M, safe online.**
  - The best value for the self-driving tests: assertions such as "reached the basket" without comparing screenshots.
- **Teleport:**
  - **portable with rework, M, offline-only** (gate it); it needs the 0xb2aa hook.
  - Value for tests: start a route at a known spot. Never for players online.
- **Free camera:**
  - **portable with rework, M–L, offline-only:** class C, and class D when paused.
  - Value: a photo mode, and fixed viewpoints for test stills. LBP3 already has its own Popit camera, so it is medium-low.
  - Our frame-generation cut rule reads the same camera block, so its jumps count as cuts (inferred).
- **Hide HUD:**
  - **portable with rework, S–M.** It is render-only, but **gate it offline** until the script UI pass is shown to be side-effect free.
  - Value: clean stills.
- Touches: `tools/nav.py` and the self-driving routes, `LBP3_SYNC_PROBE`, ticket 09 (the step hook sits next to its code).

---

## 4. F1 settings overlay against our L3+R3 menu

### Seth's menu (read, seth:gpu/shim/lbp3_overlay.cpp, 172 KB)

**Opening it:** F1, Insert, or R3+L2. It is ImGui drawn with Vulkan into the swapchain (seth:gpu/shim/lbp3_overlay.h:1-4). Pages (seth:gpu/shim/lbp3_overlay.cpp:2425-2445):

| Page | What it holds |
|---|---|
| **Display** | Monitor; window, borderless or exclusive mode with a resolution and refresh list; frame rate limit; VSync; Reflex and Anti-Lag; frames queued; frame-generation debug view (seth:gpu/shim/lbp3_overlay.cpp:958-1197) |
| **Graphics** | Render resolution and upscaler; engine frame rate; a **Bloom** toggle (the bloom image is zeroed before the composite, host-side: seth:gpu/shadps4/video_core/renderer_vulkan/vk_rasterizer.cpp:1892-1901); background shader builds; upscaler output and debug view; jitter; object motion; FSR 1, nearest or integer scaling, RCAS strength; two readback-waiting switches (seth:gpu/shim/lbp3_overlay.cpp:1452-1570) |
| **Camera** | Hide HUD, free camera, teleport (seth:gpu/shim/lbp3_overlay.cpp:663-818) |
| **Controls** | Button mapping; connected pads; players and the keyboard player; motion on/off and sensitivity; controller works in the background; mouse in Create (toggle, buttons, speed) (seth:gpu/shim/lbp3_overlay.cpp:1681-1864) |
| **Online co-op, Trophies** | Only when the port has them |
| **Recording** | Video (F9), effect diagnostics (F10), input logs (seth:gpu/shim/lbp3_overlay.cpp:2006-2123) |
| **Overlay** | The performance overlay: FPS, ms, upscaler, GPU %, CPU %, RAM, VRAM, plus scale, corner, opacity and layout; Discord status (seth:gpu/shim/lbp3_overlay.cpp:1881-1922) |
| **About** | Save a report (seth:gpu/shim/lbp3_overlay.cpp:1932-1934) |

**Shared option lists:** `lbp3_menu_options.*` keeps one list of names, options and hints for the menu and the launcher (seth:gpu/shim/lbp3_menu_options.h:1-14).

### Ours

The menu has Stats, Frame generation, Volume, Import LBP1/LBP2 items, Quit and Close (lbp3-recompiled:runtime/src/ui/menu_model.h:9). It is ImGui over the SDL D3D11 renderer. Already present elsewhere:
- background pad events (lbp3-recompiled:runtime/src/input/gamepad_sdl.cpp:342);
- `LBP3_WINDOW` and F11, `LBP3_DISPLAY` (lbp3-recompiled:runtime/src/video/presenter.cpp:148-177);
- the launcher's controls editors; a shared controls table (`input/action_table.h`).

### Worth borrowing (ideas and logic only; their ImGui drawing code doesn't fit our renderer)

1. **Hide HUD** and, offline, **Free camera** rows, after §3.
2. **Save a report** (F12): see §5.
3. **Performance overlay extras:**
   - GPU %, VRAM and a corner choice;
   - VRAM from DXGI `QueryVideoMemoryInfo` on our D3D11 adapter, GPU % from the Windows GPU-engine counters;
   - S, low-medium value (it helps friends' reports).
4. **Mouse in Create** rows, after §2.
5. **Scaling filter and sharpening strength:** if FSR 1 is revived.
6. **Motion sensitivity:** S, on top of our motion filter.
7. **Bloom off:** S, host-only, cosmetic.

Not worth it: Reflex, frames queued, the DLSS model, recording, the readback-waiting switches (another lane's area), window-size lists.

**Verdict:** portable with rework (reimplemented in our menu model), S each. No lockstep risk, except Hide HUD and Free camera (§3).

---

## 5. Reflex, window fit, display, perf stats, record/replay, bug report, diagnostics

### Reflex / Anti-Lag 2 (read, seth:gpu/shim/lbp3_reflex.h:1-42)

- **What it is:** `VK_NV_low_latency2` with present IDs, and `VK_AMD_anti_lag`. The game's thread sleeps after the frame submit.
- **It needs a Vulkan swapchain.** We present through D3D11; there the equivalent would be NVAPI.
- **Our own study found nothing to gain safely** (notes:perf/best_gfx.md §3):
  - our frame already starts about 27 ms before its slot;
  - using the remaining slack moves frame starts past the frame clock's 8 ms dead zone, which is ticket 09's area.
- **Verdict: not applicable.**

### Window fit, display (read, seth:gpu/shim/lbp3_window_fit.h:1-20; seth:gpu/shim/lbp3_display.h:1-38)

- **What it is:** the windowed size is clamped to the monitor's work area, and the fullscreen modes are listed per monitor.
- **Verdict: already have**, through `LBP3_WINDOW`, F11, `LBP3_DISPLAY` and the launcher.

### Perf stats (read, seth:gpu/shim/lbp3_perf_stats.h:1-80)

- **What it is:** a sampler thread reads CPU %, GPU %, RAM and VRAM twice a second.
- **Ours** (lbp3-recompiled:runtime/src/video/live_stats.h:1-27): game fps, frame ms, worst, work, shown fps, generated share, latency.
- **Verdict:** portable with rework, S, low-medium.

### Input record / replay (read, seth:src/runtime_pad.c:437-552; seth:src/record_pad.h:1-71)

- **How it works:**
  - `LBP3_PAD_RECORD=<file>`, toggled by F9, writes `ms buttons lx ly rx ry l2 r2` on each change.
  - **Time is wall-clock milliseconds**, for **player 1 only**, with no touch and no motion.
  - `LBP3_PAD_REPLAY` plays it back, started by the token `replay` in `LBP3_PAD_FILE`.
- **Against ours:**
  - Our remote queue scripts timed presses per player.
  - `nav.py` waits out loads by recognising screens.
  - Wall-clock replay drifts with load times and with frames that run 0 or 2 simulation steps (0.1–1% for us).
- **Our improvement (inferred):**
  - Key the recording on **`GetSimFrame__`** (eboot+0x7c79e0, already read in lbp3-recompiled:runtime/src/hle/videoout.cpp:578) from a detected level start.
  - If replay from the same save is deterministic, routes become exactly repeatable, and the same tool doubles as a divergence check (replay on two builds or PCs and compare `LBP3_SYNC_PROBE` hashes).
  - **Unchecked:** random-number seeding, level-start state and our own host-side timing. Prove determinism first. If it fails, replay still beats timed presses for in-level segments.
- **Verdict:** **portable with rework (our own implementation, seth's as reference), S–M, class A, safe.** Touches `tools/nav.py`, the remote input, `LBP3_SYNC_PROBE`.

### Bug report (read, seth:gpu/shim/lbp3_bug_report.h:1-46; seth:README.md:67-72)

- **What it is:**
  - F12 in-game writes `context.json`: mode, level, position, camera, players, frame stats, settings.
  - Plus `teleport.json`, the **last 30 s of controller input**, a screenshot of the next real frame and a typed note.
  - Then `scripts/bug_report.py` zips it to the Desktop; per seth the user name is scrubbed.
  - `last_context.json` is refreshed every 10 s.
- **Ours:** the launcher's "Make a report zip": logs, `launcher.ini`, system info and checks (lbp3-recompiled:runtime/launcher/launcher_sys.cpp:610-638).
- **Verdict:** **portable with rework, S–M, read-only, safe.**
  1. F12 writes a log marker, a screenshot (from our readback frame), the context and the last N seconds of input into `analysis\reports\pending\`.
  2. `MakeReport` adds them.
  - Medium value for friends' support. Touches notes:best/launcher.md.

### Video recorder (read, seth:gpu/shim/lbp3_record.h:1-60)

- **What it is:** NV12 conversion on Vulkan plus FFmpeg (NVENC, AMF, QSV, Media Foundation).
- **Why not for us:** the FFmpeg dependency and their Vulkan presenter; OBS and Windows' capture exist.
- **Verdict: not worth it.**

### Drawdiff (read, seth:gpu/shim/lbp3_drawdiff.h:1-43)

- **What it is:**
  - A signature per draw and present, built to find draws that differ between their 60 fps extra and real frames, or that alternate frame to frame.
  - **Skip lists** (`LBP3_DRAWDIFF_SKIP`, re-read every second) drop draws by shader hash, live.
  - F10 captures 16 frames.
- **Ours:** the per-draw hook and `[draw]` lines on `exp/fg-capture` (notes:perf/fg_inputs.md).
- **Verdict:** already have, mostly. **The live skip list is portable with rework, S**, for flicker and dropped-draw hunts.

### F83 scan (read, seth:gpu/shim/lbp3_f83_scan.h:1-14)

- **What it is:** scans guest memory for a constant-buffer value.
- **Verdict: not worth it** (we have Ghidra and `tools/find_refs.py`).

### Tests that travel with the code (read, headers only)

- `seth:tests/test_camera_motion.cpp` (production camera-motion SPIR-V);
- `seth:tests/test_taa_shader.cpp` (production TAA shader, Lavapipe);
- `seth:tests/test_motion_history.cpp` (CPU);
- `seth:tests/test_scene_resolution.cpp` (target round trips);
- `seth:tests/test_upscaler_availability.cpp` and `seth:tests/test_upscaler_support.cpp`;
- `seth:tests/test_perf_overlay.cpp`;
- `seth:tests/test_bug_report.py` (scrubbing, zip contents).

---

## Ranking (user value against effort)

| # | Item | Verdict | Effort | Class / lockstep | Touches |
|---|---|---|---|---|---|
| 1 | Pad record/replay keyed on the simulation frame | portable with rework (ours; seth's is wall-clock) | S–M | A, safe; determinism unproven | test rigs, sync probe |
| 2 | Player-1 state readout | portable with rework | S–M | read-only, safe | test rigs, bug report |
| 3 | FSR-Vulkan with checked-in shaders, for the FSR 3.1 spike | portable with rework (upscaler headers confirmed, frame-generation folder unverified) | M | B, safe | perf/fg_fsr31.md |
| 4 | In-game F12 report | portable with rework | S–M | read-only, safe | best/launcher.md |
| 5 | Create mouse: buttons and wheel → pad | portable with rework | S–M | A, safe | issue 12, best/input.md |
| 6 | Create mouse: absolute cursor | portable with rework, **offline-only** | M | **D** | issue 12 |
| 7 | Render/output split | portable with rework (mapping partly done) | M | B; keep the aspect off | perf/resolutions.md (4K) |
| 8 | Teleport | portable with rework, **offline-only** | M | **D** | test rigs |
| 9 | Hide HUD | portable with rework, gate offline | S–M | B/C (script pass unproven) | photos, test stills |
| 10 | Temporal upscalers (FSR 3.1, DLSS, XeSS, FSR 4, TAA) | portable with rework | L | B; capture and frame-generation regressions; DLSS licensing question | 4K, lbppc_vs_open_items §4 |
| 11 | Free camera | portable with rework, **offline-only** | M–L | C, plus **D** when paused | photo mode |
| 12 | Perf overlay extras, menu options | portable with rework | S each | B or A | — |
| 13 | Drawdiff skip list | portable with rework (the rest: already have) | S | none | dropped_draws |
| 14 | Window fit, display tab | already have | — | — | — |
| 15 | Reflex / Anti-Lag 2 | not applicable | — | — | best_gfx §3, ticket 09 |
| 16 | Video recorder | not worth it | L | — | — |
| 17 | F83 scan | not worth it | — | — | — |
