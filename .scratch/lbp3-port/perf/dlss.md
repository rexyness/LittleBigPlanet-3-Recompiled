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
