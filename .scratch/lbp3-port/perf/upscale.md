# The window's upscaler: FSR 1 from the 1080p render (2026-11-12)

The user asked "can we run the game at 2k?". Both their monitors are 2560x1440. The game renders at 1920x1080, and the window stretched that with SDL's bilinear filter. True 1440p rendering is feasible but a 3–5 day patch job ([res1440.md](res1440.md)). This lane builds the cheap win first: upscale the 1080p frame to the window with AMD FidelityFX Super Resolution 1, which is spatial (EASU edge-adaptive upscale, then RCAS sharpening), on the GPU before the readback.

**Labels:** **[measured]** from runs and the offline tools, **[looked at]** stills inspected by eye, **[inferred]** otherwise.

- **Branches** (pushed to the forks; `port` untouched):
  - runtime `exp/upscale` `d06a0a1`, from `port` `7d04e28`; worktree `D:\Recomps\lbp3-wt\ups`.
  - shadPS4 `exp/upscale` = `55d38d87` (no shadPS4 changes); worktree `D:\Recomps\lbp3-wt\ups-shadps4`.
- **Use:** `LBP3_UPSCALE=fsr1` (default off). `LBP3_UPSCALE_SHARP` = RCAS sharpness in stops (0 = sharpest, default 0.25).

## How it works

- **Shaders:** `runtime/src/gpu/fg_shaders/upscale.comp`, two passes (EASU, RCAS) compiled with the glslang built from shadPS4's externals, including shadPS4's in-tree `fsr/ffx_fsr1.h` and `ffx_a.h` (AMD, MIT).
  - shadPS4's own `FsrPass` isn't in our build: `host_passes/` is excluded with the presenter, and it depends on shadPS4's settings globals.
  - So these are our own passes on storage buffers, using FSR's 32-bit path. EASU's `textureGather` is emulated from the buffer; its sample positions sit on texel corners, so the 2x2 lookup is exact.
- **Real frames:**
  - In the flip's readback (`ReadbackDisplayBuffer`), the display image is also copied into the upscaler's device-local input buffer. This is a transfer read of the game's image, the same as the readback's own; nothing of the game's is written.
  - After the readback's `Finish`, `KickReal` records EASU, RCAS and a copy into a host-visible slot (a ring of 3) into a command buffer of its own, and submits it without waiting.
- **With frame generation's GPU warp:** the in-between frame is upscaled inside the generator's own submission, reading its output buffer. With a CPU generator (blend, repeat, warp-cpu) the upscaler stays off, because its in-between frames couldn't be upscaled to match.
- **Fetch:** the presenter (without frame generation) or frame generation's worker waits for the ticket's GPU tick and copies the upscaled frame out of its slot, seq-checked against reuse. That happens on their threads, not on the command-processor thread.
  - The flip path the game sees gains only the input copy and one submit.
  - The 1080p frame still goes everywhere it went: the frame dump, the photo capture, frame generation's inputs and the cut detection.
- **Size:** the presenter publishes the window's client size in pixels each refresh.
  - The frame is upscaled to exactly that size when the window is bigger than 1920x1080 in at least one direction and not smaller in either. Windows that are smaller, minimised or over 4096 a side keep the plain stretch.
  - A new size takes effect after holding for 8 frames, so dragging a window edge doesn't reallocate on every frame. The upscaler waits for its work in flight before reallocating.
- **Status line:** `[upscale] fsr1 -> WxH: N real + N generated submitted, N + N shown upscaled, N not; gpu real/generated avg/max; fetch wait/copy avg/max`.

## Offline [measured]

- **`upscaletest`: 23 checks, 0 failed.**
  - The size rules and the debounce.
  - On a headless device:
    - flat colour stays flat (±1) with channels in place and alpha 255;
    - a vertical and a horizontal step edge land where bilinear puts them (within 0.75 output px in every channel, so no half-pixel shift either way);
    - smooth content stays within 1.5 levels of bilinear on average;
    - RCAS at 0 stops is sharper than at 2.
- **The seven existing test programs pass:** cooptest 81, dlgtest 154, osktest 263, dialogtest 368, jpegtest 157, imetest 403, framegentest 103, all with 0 failed.
- **GPU time** (passes plus the copy to the host-visible slot, RTX 3090, offline):

  | | best | average |
  |---|---|---|
  | 2560x1440 | 0.83–1.04 ms | 1.86 ms |
  | 3840x2160 | 1.92 ms | 3.98 ms |

  The averages include the GPU clocking down between the offline tool's submissions [inferred]; the in-game number is what counts.

## Stills: bilinear against FSR 1 at 0.25 and 0.75 [looked at]

`upeval <frame.raw> <out> --sharp 0.25,0.75 --crop name:x,y,w,h` on lane A's dumped frames. The stills are in `D:\Recomps\lbp3-recompiled\analysis\upscale\stills\`, as full frames (`*_bilinear.png`, `*_fsr0.25.png`, `*_fsr0.75.png`) and 2x-zoomed crops with three panels side by side: bilinear, then FSR 0.25, then FSR 0.75.

- **Text** is visibly crisper with FSR:
  - Bunkum's subtitles (`bk_b_f030_final_subtitle.png`), the "WELCOME" banner (`_banner`), the level's subtitle bar (`np_walk_f045_final_subtitle.png`) and the score bubble's "80" (`_score`);
  - the JAM label's small print (`_jam`), which is mush under bilinear and nearly legible under FSR.
- **Textures** gain visible detail: Sackboy's knitted fabric (`np_walk_f045_final_sackboy.png`), the basket weave (`np_load_f010_final_weave.png`), the gingham, and the banner's brush strokes.
- **No colour shift, misplacement or blockiness** in any crop.
- **One artifact:** a faint light rim at high-contrast silhouettes, most visible on Sackboy's hair against the bright sky at 0.25 and weaker at 0.75. That is RCAS's sharpening halo. If it shows in play, raise `LBP3_UPSCALE_SHARP`.
- **Staircase diagonals** (the map's fabric edges in `np_load_f010_final_weave.png`) look as jagged as under bilinear. FSR 1 doesn't add anti-aliasing, and the game's SMAA output is what it gets.

## In game

Pending: the game is in use by the human. To measure on the 165 Hz display at 2560x1440, with frame generation on and off:
- GPU and fetch costs;
- main-thread work on the Needlepoint route;
- cadence and latency with frame generation;
- the photo test;
- stills.
