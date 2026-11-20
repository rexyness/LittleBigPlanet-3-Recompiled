# The DLSS upscaler (issue #24)

The design is settled in issue #24, `GLOSSARY.md` ("The picture") and ADRs 0002 (a DLSS build only) and 0003 (the end of the frame). This note records the build and what was measured, stage by stage.

**Labels:** **[measured]** from runs, **[looked at]** stills inspected by eye, **[inferred]** otherwise.

## Builds

| Repo | Branch | Worktree |
|---|---|---|
| Runtime | `feat/dlss` | `D:\Recomps\lbp3-wt\dlss` |
| shadPS4 | `feat/dlss` (the submodule checked out in the worktree, not a junction) | `D:\Recomps\lbp3-wt\dlss\third_party\shadPS4` |

- **NVIDIA's SDK:** `D:\Recomps\sdk\dlss-310.9.16`, a sparse clone of github.com/NVIDIA/DLSS tag v310.9.16 (`include/`, `lib/Windows_x86_64/x64/`, `lib/Windows_x86_64/rel/nvngx_dlss.dll`, `doc/`, `LICENSE.txt`). `nvngx_dlss.dll` is signed by NVIDIA Corporation (valid), file version 310.9.16.0, SHA-256 `d00b5b0a…6123e`. Never inside a repository.
- **Configure once** with `.scratch/lbp3-port/tools/build_dlss.bat` (build.bat's toolchain plus `-DLBP3_DLSS=ON -DLBP3_DLSS_SDK=D:/Recomps/sdk/dlss-310.9.16`). The cache keeps both, so `build.bat` afterwards builds the DLSS build too.
- **A DLSS build** has `nvngx_dlss.dll` and a `DLSS_BUILD_NOT_FOR_RELEASE` marker next to `lbp3.exe`. `tools/make_package.ps1` refuses such a `build\` and any `nvngx*.dll` in the staged package.
- **The worktree's externals** (shadPS4's submodules) are plain copies from the main clone, as in the other worktrees; `third_party\SDL3` is a junction to the main clone's.

## How it runs

- On when `LBP3_UPSCALER=dlss` (stage 3 adds the launcher setting). `LBP3_DLSS_LOG=1` turns on NGX's own log lines (`[dlss] ngx: ...`).
- **Vulkan:** shadPS4 enables the extensions NGX asks for through two hooks (`g_lbp3ExtraInstanceExtensions`, `g_lbp3ExtraDeviceExtensions`; null in every other build). On the RTX 3090 (driver 621.01) NGX asks for **1 instance extension** (`VK_KHR_get_physical_device_properties2`) and **4 device extensions** (`VK_NVX_binary_import`, `VK_NVX_image_view_handle`, `VK_KHR_buffer_device_address`, `VK_KHR_push_descriptor`) [measured]. NGX links no `vulkan-1` import: it uses the dispatcher's `vkGetInstanceProcAddr`/`vkGetDeviceProcAddr`.
- **NGX:** `NVSDK_NGX_VULKAN_Init_with_ProjectID` (the port's own GUID, custom engine), data folder `user\dlss`, NVIDIA's DLL found next to `lbp3.exe`.
- **Per frame** (`runtime/src/gpu/dlss.cpp`), with the composite and fill hooks chained after frame generation's or the capture tool's:
  - at the final composite: the HUD-less image (the display buffer) is copied into our own image;
  - at the `007f7f00` velocity clear: the velocity target, and the scene depth (the depth with the most depth-tested draws, through a buffer into an R32F image).
- **At the flip** (in `ReadbackDisplayBuffer`, before the readback's copy):
  1. the finished frame is copied;
  2. a compute pass turns the game's vectors into DLSS's: `mv = -((B, G) - 127) * (0.941 * W/1920, 0.529 * H/1080)` render pixels where R = 255, else 0 (current → previous, RG16F);
  3. DLSS: DLAA, flags `MVLowRes | AutoExposure` (8-bit perceptual colour, conventional depth, no jitter yet), reset on the first frame and whenever a frame is missing its inputs;
  4. a compute pass puts the screen HUD back: the finished frame where it differs from its HUD-less image by more than 8 levels in any channel, else DLSS's picture;
  5. the readback copies that picture instead of the display buffer.

## Stage 1: DLAA at 2560x1440 (2026-11-21)

`dlss_smoke.sh` (40 s at boot) then `dlss_run.sh a1 LBP3_UPSCALER=dlss` and `dlss_run.sh r1` (the same exe without the upscaler): res_run2.sh's route from the story test save, muted, `LBP3_RES=1440` with shaders, frame generation off, the capture hook on, the game lock held, the test saves swapped in and the human's put back, a shader cache copy in `analysis\dlss_cache`.

- **It runs** [measured]: `DLSS created: 2560x1440 -> 2560x1440 (DLAA), display format R8G8B8A8Srgb`. After boot, 300 of 300 flips per window upscaled; one window had 31 flips without a HUD-less image (a level change) and one had 5 without vectors. 0 failed evaluations, 0 GPU warnings, no crash.
- **Cost** [measured, frame work, the route's windows]:

  | | Walk | Standing at the basket |
  |---|---|---|
  | Native 1440p, SMAA (`r1`) | 21.84 ms | 19.80 ms |
  | DLAA 1440p (`a1`) | 25.48 ms | 22.77 ms |

  About +3 to +3.6 ms of work: the flip waits for the GPU (`Finish`), so DLSS's GPU time and the copies add straight to it. Both hold 30 fps (flip average 34.2 and 34.1 ms). The ~450 ms worst frames are the capture hook's, in both runs.
- **Picture** [looked at]: `dlss_compare.py` puts the same frame's HUD-less image (the game's SMAA'd picture, DLSS's input) next to its flipped image (DLSS's picture with the HUD back), so the two are pixel-aligned. Stills and 3x crops in `analysis\dlss_stills\` (`dlss_a1_<spot>_game.png`, `_dlss.png`, `_crops.png`).
  - Edges are smoother than SMAA's (Sackboy's outline, the white/blue cloth edge, the ornament's specular sparkle); the herringbone fabric loses its pixel noise and keeps its weave, slightly softer.
  - The screen HUD ("Close Popit") is back on top and crisp; the popit's "Customize Character" and its icons are intact; no colour shift.
  - Pixels changed by more than 8 levels: 5.4% (Pod) to 14.9% (the walk, in motion).
- **Not yet:** jitter, 16-bit vectors, skipping SMAA, mip bias (stage 2); a smaller render size (stage 3); frame generation (stage 4). With no jitter, DLSS can't resolve detail finer than a pixel; that is the next step.

## Stage 2: jitter and SMAA (2026-11-21)

- **How:** shadPS4 gained two hooks (`c863b1c4`): `g_lbp3ViewportJitter`, asked per viewport of a draw with clipping on, and `g_lbp3SkipDraw`, asked per draw before anything is bound.
  - **Jitter:** one Halton(2,3) offset per rendered frame, 16 phases at DLAA (NVIDIA's guide 3.7.1 asks for at least 8 there). It goes on every draw that is depth-tested, has more than 12 indices, has a render-sized viewport and isn't drawn into a display buffer.
  - **SMAA:** the copy-back draw (PS `6d4d5238b13eadc4`) is left out.
  - **Gating:** both only while the last flip was upscaled.
  - **Switches:** `LBP3_DLSS_JITTER`, `LBP3_DLSS_JITTER_SIGN`, `LBP3_DLSS_SMAA`, `LBP3_DLSS_MV` and `LBP3_DLSS_TRACE`, plus the remote `dlss <key> <value>` to switch them live with a history reset.
- **What gets jittered** [measured, trace]: exactly two colour targets at 2560x1440, `175448000` (about 100–240 draws a frame, the scene) and `1805d8000` (about 60–170, the velocity pass). No other target, no depth-only draws, and the stacked 2560x2880 targets aren't hit.
- **The vectors are decoded right** [measured, `dlss_check.py`, the e2 walks]: frame N−1 warped onto frame N with frame N's vectors, as DLSS reads them (prev = p − m). Mean reprojection error over 11 frame pairs, in each of the 5 walking sets:

  | Signs | Error |
  |---|---|
  | as DLSS gets them | 4.0 |
  | y flipped | 4.6 |
  | zero vectors | 20.8 |
  | x flipped / both flipped | 27.9 |

  The y-flipped case is close only because the walk is sideways.
- **The jitter reaches the game's picture and DLSS holds still** [measured, phase correlation on a static 512² crop]: the game's picture moves 0.49–0.56 px (x) and 0.37–0.42 px (y) rms frame to frame. DLSS's moves 0.002–0.06 px, whichever sign it is told. Stability doesn't settle the sign.
- **The sign: DLSS told `++`** [measured + looked at]: the viewport's (+jx, +jy) moves the scene right and down, the same axes as the vectors. At both spots `++` gives the sharpest DLSS picture (mean luma gradient, 12 frames each):

  | Told | Basket (e1) | Pod (e2) |
  |---|---|---|
  | `++` | **7.48** | **3.953** |
  | `-+` | 7.19 | 3.928 |
  | `+-` | 7.29 | 3.910 |
  | `--` | 6.99 | 3.836 |

  `--` is the blurriest at both, matching NVIDIA's "fuzzy" symptom of a wrong sign. In the Pod crops (`analysis\dlss_stills\dlssx_e2_pod.png`), the ring's edge is stair-stepped in the game's un-antialiased picture, still stepped with DLSS and no jitter, and smooth with any jitter.
- **Leaving SMAA out** sharpens DLSS's picture (basket: 7.16 → 7.97) at the same steadiness. SMAA was left out in 284–300 of 300 flips once switched off.
- **The game's other states** [measured, `dlss_lane.sh l1`, defaults on]:
  - The Pod, the Pod computer, Create mode, the pause menu and its fade, and a photo were all upscaled.
  - The 243 flips not upscaled were boot screens (no draws, or all draws into the display buffer), plus 41 single-draw flips while the Pod computer opened.
  - The photo encoded (640x360). The pause menu draws crisply over the game's own blurred scene. 0 GPU warnings.
- **A release build:** `build_nodlss\` (`LBP3_DLSS=OFF`) builds and passes all 16 test programs (restest 2672, launchertest 434, framegentest 120, exactmathtest 90068 checks, …) with 0 failed.
- **Open:**
  - **16-bit vectors:** not done. The 8-bit decode reprojects correctly, and what the extra precision would add is unmeasured.
  - **Mip bias:** NVIDIA's guide 3.5: `log2(render width / output width) − 1`, so −1 at DLAA. It comes with the render size in stage 3.

## Stage 3: modes, the start-up check, the launcher (2026-11-21)

- **Modes** (`runtime/src/gpu/dlss_modes.h`, shared with the launcher, no NVIDIA code):
  - heights in multiples of 8 and widths in multiples of 16 (`capture_pitch.md`), the output's aspect at most 0.9% wider so `Wide()` stays false at 16:9, never under 1280x720;
  - the render sizes:

    | Output | Quality | Balanced | Performance | Ultra Performance |
    |---|---|---|---|---|
    | 2560x1440 | 1712x960 | 1472x824 | 1280x720 | – |
    | 3840x2160 | 2560x1440 | 2224x1248 | 1920x1080 | 1280x720 |
    | 1920x1080 | 1280x720 | – | – | – |
    | 3440x1440 | 2256x944 | | | |

  - NVIDIA's own Quality size for 2560x1440 is 1707x960, allowed 1280x720 to 2560x1440 [measured, NGX's optimal settings], so 1712x960 is inside the range.
- **Start-up:** `dlss::Prepare()` at the top of `main()`, before anything reads `LBP3_RES`.
  - With `LBP3_UPSCALER=dlss`, it runs a child `lbp3.exe --dlss-probe`, which checks for NVIDIA's DLL next to the exe and runs NGX's `GetFeatureRequirements` on the GPU shadPS4 picks. It's a child process because, done in-process, the Vulkan loader, the driver and NGX took the thread-local-storage slots the runtime needs ("no inline TEB TLS slot available", the first try).
  - Then `LBP3_RES` becomes the mode's render size, and the output size is kept.
  - The capture slot used to be sized at static initialisation, before `Prepare`. It is now sized on first use and fits the output.
- **Fallback** [measured]: without NVIDIA's DLL, the log says `[dlss] no upscaler this run: NVIDIA's DLSS DLL (nvngx_dlss.dll) isn't next to lbp3.exe. The game renders at 2560x1440.`, the display buffers are 2560x1440, and NGX doesn't start. `--dlss-probe` answers 0 (`available on NVIDIA GeForce RTX 3090`), 1 (the reason) or 2 (`not a DLSS build`, a release build).
- **Per mode:** jitter phases 8 × ratio², at least 16 (Quality 18, Balanced 24, Performance 32, Ultra Performance 72), and mip bias `log2(render / output) − 1` (DLAA −1, Quality −1.58, Performance −2) on the scene's pixel-shader samplers, through shadPS4's `g_lbp3SamplerBias` (`7b17e96d`). `LBP3_DLSS_MIPBIAS` overrides it.
- **Cost** [measured, the route, frame work]:

  | 2560x1440 output | Render | Walk | Basket |
  |---|---|---|---|
  | Native, SMAA | 2560x1440 | 21.84 ms | 19.80 ms |
  | DLAA (stage 1, no jitter) | 2560x1440 | 25.48 ms | 22.77 ms |
  | Quality | 1712x960 | 23.20 ms | 20.57 ms |
  | Performance | 1280x720 | 22.85 ms | 20.43 ms |

  All hold 30 fps with 0 GPU warnings.
- **Picture** [looked at, `analysis\dlss_stills\dlss_p2_*`]:
  - In Performance (1280x720 → 2560x1440), DLSS is far sharper than the 720p picture stretched with bilinear filtering: the basket weave, the herringbone fabric and the bottle's edge, which is a staircase in the stretch.
  - The popit's label (in-world UI, through DLSS) reads cleanly.
  - The screen HUD ("Close Popit") is visibly soft: it is drawn at 720p and stretched, as ADR 0003 expected. At Quality the stretch is 960 to 1440 lines.
- **Launcher:**
  - Graphics & sound has Upscaler (Off / NVIDIA DLSS) and Mode. They are shown only for a DLSS build's `lbp3.exe`: its `DLSS_BUILD_NOT_FOR_RELEASE` marker first, then its own `--dlss-probe` answer, asked in the background.
  - Modes under 1280x720 are greyed with the reason, and the help line gives the render size.
  - About says "This build uses NVIDIA DLSS."
  - `launcher.ini` gains `upscaler` and `upscaler_mode`. `launchertest` 451 checks (+17), 0 failed, in both builds.
- **Not yet:** frame generation (stage 4); the Mode row seen live (the `--shot` pictures come before the probe answers); mip bias compared on against off.

## Stage 4: frame generation on DLSS's picture (2026-11-21)

- **How:** `fglive::OnUpscaled` replaces the display-image copy when DLSS made the picture. The generator gets three things at the output size:
  - DLSS's picture with the screen HUD back on top;
  - DLSS's picture before the HUD went back, as the HUD-less image;
  - the game's velocity target resampled to the output size (a third pass, nearest-neighbour, in the game's own RGBA8 encoding, which counts in screen fractions, so the generator's `W/1920` scaling still holds).
- **Frames DLSS can't take while upscaling** (loading screens) are the game's picture stretched to the output size, so the window and the generator keep one size.
- **Measured** (Quality 1712x960 → 2560x1440, `LBP3_FRAMEGEN=1 LBP3_FRAMEGEN_MODE=warp`, the route without the capture hook, `NODUMP=1 dlss_run.sh f1`):
  - each 30-flip window: 30 real in, 30 generated, 0 dropped, 3–5 a refresh late at 165 Hz;
  - every fetch used the HUD-less image and the game's vectors;
  - generation 3.4–3.6 ms of GPU;
  - frame work 25.4 ms (walk) and 23.5 ms (basket), 0 frames over 40 ms, worst 35.7 ms;
  - all 126 boot flips DLSS couldn't take were stretched.

## Checks before handing over (2026-11-21)

- **The packaging guard refuses a DLSS build** [measured]: `tools\make_package.ps1 -SkipBuild -AllowDirty`, run in the dlss worktree, stopped with "build\ is a DLSS build (LBP3_DLSS): a DLSS build is never published; package from a build without it". Nothing was staged.
- **DLAA with frame generation** (`NODUMP=1 dlss_run.sh d2`):
  - every fetch used the HUD-less image and the vectors, generation about 2.8 ms;
  - frame work 27.9 ms (walk, one frame over 40 ms, worst 50 ms) and 26.3 ms (basket, none over 40 ms).
  - It's the heaviest combination: the full-size render plus DLSS plus the generator.
- **Performance through the states route** (`dlss_lane.sh p3`, trace on):
  - jitter reached only the scene (`1710d0000`, about 105–120 draws) and velocity (`177ad0000`, 48–71) targets. The fixed 1280x720 offscreen photo view, the same size as this render, was not hit;
  - the photo encoded (640x360) and shows correctly in My Pictures;
  - the only flips not upscaled were at boot (stretched); 0 GPU warnings.
- **Mip bias is applied** [measured, the new per-window count]: 419 samplers a frame at −1.00 in DLAA, 965 at −2.00 in Performance. NVIDIA's value against none (`dlss_exp3.sh m1`, Quality, standing at the basket, 12 frames each):

  | Mip bias | DLSS frame-to-frame change | DLSS sharpness |
  |---|---|---|
  | 0 | 0.909 | 6.28 |
  | −1.58 (NVIDIA's) | 0.842 | 9.28 |

  NVIDIA's bias is sharper and no less steady. Its crops show crisper leather grain and stitching, and no moiré (`analysis\dlss_stills\dlssx_m1_standing.png`). In upscaling modes `dlss_temporal.py`'s "game" column comes from the render-size image, so it shows another region.
- **Fixed:** the child probe now waits first (30 s limit) and reads afterwards. Before, reading blocked until the child exited, so the time limit could never fire.

## Trying it (the user's session)

- Start `D:\Recomps\lbp3-wt\dlss\build\LBP3Launcher.exe`, the DLSS build's own launcher. It finds the main clone's `user\` (settings and saves) and starts the `lbp3.exe` beside it.
- Graphics & sound: Upscaler → NVIDIA DLSS, then Mode. Quality draws 1712x960 at 2560x1440; DLAA draws the full size and is the sharpest. Frame generation can stay on.
- The game log (`analysis\play_launcher_log.txt`) shows the `[dlss]` lines: the mode, the render size, NVIDIA's optimal size, and the per-300-flip counts.
- To compare without DLSS, set Upscaler back to Off.

## Still open

- **Stage 5, the done checks (issue #24):**
  - the user's session and verdict;
  - one online session with DLSS on, with LBPPC players if possible;
  - stills of the HUD's translucent parts, a photo and the pause fade in an upscaling mode (done in DLAA, `dlss_lane.sh l1`).
- **16-bit vectors** (deferred, see stage 2).
- **Mip bias** on against off has not been compared by eye.
