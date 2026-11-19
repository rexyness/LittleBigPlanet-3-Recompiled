# True 1440p: the experiment (2026-11-12)

Follows [res1440.md](res1440.md). Both the offline and the in-game parts are done; see **Results** at the end.

## Builds

| Repo | Branch | Commit | Worktree |
|---|---|---|---|
| Runtime | `exp/res1440` (from `port` `7d04e28`) | `8110f36`, then `928c42f` (the fix below) | `D:\Recomps\lbp3-wt\r1440` |
| shadPS4 | `exp/res1440` (from `55d38d87`) | `56120048` | `D:\Recomps\lbp3-wt\r1440-shadps4` |

Both are pushed to the forks; `port` is untouched.

Settings:
- `LBP3_RES=1440`: the patch set.
- `LBP3_RES_SKIP=<group,...>`: leave groups out.
- `LBP3_RES_POOL_MB=<MB>`: also resize the Garlic pool.
- `LBP3_RES_SHADERS=1`: the shader literals.

With none of them set, the build behaves like `port`.

## The patch set (`runtime/src/core/patches.cpp`)

There are 79 same-length edits, generated from `analysis\eboot.elf` and Ghidra listings by `D:\Recomps\lbp3-wt\_tools\res1440_gen.py`. Each entry holds the whole original instruction. Every enabled site is checked before any is written, and one mismatch means none is patched (logged as `[res] ... NOT applied`). Each applied site is logged as `[res] eboot.bin+0x... <group>: <instruction> (<role>)`.

| Group | Sites | What |
|---|---|---|
| `core` | 3 | `FUN_0005bed0`: display width 0x780 → 0xA00 (`0x5bf83`), height 0x438 → 0x5A0 (`0x5bf90`), pitch 0x1E00 → 0x2800 bytes (`0x5bf9e`). The display buffers and the ~20 functions that read the global follow. |
| `targets` | 46 | `FUN_00427940`'s declarations: 19 × 1920x1080 → 2560x1440, 2 × 1920x2160 → 2560x2880, 2 × 480x270 → 640x360. The bloom chain (1280x720 and smaller), shadows, atlases and volumes stay. |
| `pairs` | 26 | `DrawOffscreenComposite` (`0x424790`): 13 immediates (widths, heights, pitches) plus its 1920.0/1080.0 floats (`0xe811f4`/`fc`). The SMAA passes (`0x426dc0`): 11 immediates. |
| `ui` | 4 | Floats 1920/1080 → 2560/1440 in `0x433880` (a snap grid, `0xe8235c`/`60`) and `0x6cd050` (NDC to pixels, `0xe9fdc8`/`cc`). These are a separate group because their meaning isn't proven; A/B them with `LBP3_RES_SKIP=ui`. |
| (optional) | 1 | `LBP3_RES_POOL_MB`: the Garlic pool immediate at `0x5bf55` (0x6a200000 = 1698 MB). |

- **Left out on purpose:** the `0x1e00` arguments to `FUN_0005acc0` in both pair functions (`0x425382`/`87`/`8c`, `0x4272f6`/`fb`, `0x4274d8`/`dd`/`e2`). That function is a GL-style stencil-op mapper, so `0x1e00` there is `GL_KEEP`, not a pitch (decompiled).
- **Vector scale:** the frame-generation warp (CPU `framegen_warp.cpp` and GPU `fg_gpu.cpp`) now scales the game's vectors by W/1920 and H/1080. That is exactly 1 at 1080p.
- **Capture slot:** the `LBP3_FG_DUMP` slot (`renderer.cpp`) grows to 2560x1440 when `LBP3_RES=1440`. Bigger images were already skipped, not overflowed.
- **Presenter:** it already sizes its texture from the registered display buffers.

## Shader literals (shadPS4 `recompiler.cpp`, behind `LBP3_RES_SHADERS=1`)

- **Rescan:** the translated cache (`user\shader_cache\CUSA00473`, all `.spv` `OpConstant` floats) has **17 shaders** with 1080p-derived constants, including SMAA's `3a0615bd…`, `c9311bbd…` and `0b2a3346…`, the composite's `5ed16706…` (1/480) and `33c89263…` (480/270).
- **Guest literals:** where guest dumps exist (12 of 17), they are 1/1920, 1/1080, 1/960, 1/540, 1/7680, 1920, 1080, 480, 270 and 1/480, plus 1/2160 in `03101c0e…` (the stacked target's texel).
- **The rule:** both axes scale by 4/3, so a literal equal to a 1080p size v (1920, 1080, their halves, quarters and doubles) becomes v·4/3, and 1/v becomes 3/4 of itself. It applies only to the 17 listed hashes, at decode, before translation.
- **New shaders:** other shaders with such literals are logged once (`[res] shader …: N 1080p-like literals, NOT on the list`), so new ones show up in unseen levels.
- **Separate cache:** translations with the swap on go to their own folder (`user\shader_cache\CUSA00473_res1440`), so the normal cache never gets 1440p shaders.

Scan scripts: `_tools\res1440_scanspv.py` and `res1440_scanbin.py`.

## Offline checks

- Built clean: 0 errors.
- `cooptest` 81, `dlgtest` 154, `osktest` 263, `dialogtest` 368, `jpegtest` 157, `imetest` 403 and `framegentest` 103 checks all pass with 0 failed.
- The patch table's expected bytes were checked against `analysis\eboot.elf` when generated (every immediate asserted).

## In game (to do, when the game is free)

1. Snapshot the human's saves.
2. Run the `r1440` exe on the 165 Hz display, window 2560x1440, frame generation off:
   - first 1080p (no settings), as the reference;
   - then `LBP3_RES=1440`: boot, the Pod, then the Needlepoint route.
3. Check:
   - the `[res]` lines and the `[video]` display-buffer line (2560x1440);
   - memory (Garlic allocation failures; then `LBP3_RES_POOL_MB=1954`);
   - GPU time from frame split;
   - stills against 1080p: HUD and popit placement, SMAA edges, bloom, DOF, motion blur;
   - `LBP3_RES_SKIP=ui` against with it.
4. Then add `LBP3_RES_SHADERS=1`.
5. Then frame generation.

## Results (in game, 2026-11-12 15:13–16:40, 165 Hz display)

The tool is `D:\Recomps\lbp3-wt\_tools\res_run.sh` (`res_run2.sh` adds `NODUMP=1`). It runs m5_np_route.sh's route from the story test save via saves.sh. Native-resolution stills come from the capture hook (`fgdump`) at the Pod, the Prologue map, Needlepoint's start, mid-walk, the basket and the popit, with `LBP3_FRAME_SPLIT=1`. All runs used the same exe (`r1440\build\lbp3.exe`); only the environment changed.

### What broke first: memory, at boot [measured + decompiled]

With the first patch set, `LBP3_RES=1440` died about 9 s into boot, right after the archives loaded and before the first frame. The game raised its own fatal exception, then crashed in libc on the way out.
- **Bisect:** `core` alone boots fine at 2560x1440. Any set that includes `targets` dies.
- **Where:** logging the fatal path (`linker.cpp`, `928c42f`) found the call chain `FUN_00427940` (`0x428122`) → `FUN_004626a0` (`0x46276d`) → `FUN_00015090`. That function returns a block's base, or calls `_Exit(1)` when the request exceeds the block's capacity [dec].
- **Why:** the render targets sit in two pools.
  - **"LocalRenderTargets"** (type 0, Garlic) needs 635 MB at 1440p and fits.
  - **"MainRenderTargets"** (type 1: the bloom chain and the two stacked 1920x2160 targets) needs **60.9 MB at 1440p**. It comes from a fixed block whose capacity is **36.25 MB** (`0x2441400`, a 1080p fit). That capacity is one immediate in the MainHeap layout table (`FUN_000150f0`, `0x1517d: MOV dword [RBX+0x58], 0x2441400`).
- **Not the cause:** raising the Garlic pool (`LBP3_RES_POOL_MB=1954`) changed nothing.
- **Fix:** that immediate goes from 36.25 to 68 MB (`0x4400000`, in the `targets` group). The MainHeap mapping is summed from that table, so it grows by itself: **820 → 852 MB**. After that, the full route ran in every configuration with no crash, no GPU warnings and no `nvlddmkm` events.

### Picture [measured, stills in the clone's `analysis\res1440_cmp\` and `analysis\fg_dump\png\res_*`]

- **The game renders a real 2560x1440:** display buffers 2560x1440, and the captured final images are 2560x1440.
- **No 1080p sub-rectangles:** no stretched passes and no misplaced layers in any still.
- **Composition matches 1080p:** the Pod menu, the Prologue map's panel and "PLAY" tag, the HUD score bubble, the subtitles, the popit and its labels, and "Close Popit" all sit where they do at 1080p.
- **Sharper:** text and edges are visibly sharper than 1080p upscaled. See the zoomed sheets `pod_zoom3.png` and `map_zoom3.png`: 1080p upscaled, 1440p, 1440p with the shader swap.
- **The shader swap (`LBP3_RES_SHADERS=1`):**
  - 13 of the 17 listed shaders came up on this route and were rescaled, with no unlisted shader carrying 1080p literals.
  - On these stills, with and without it look the same at 4x zoom; the SMAA and bloom literals make no visible difference here. Moving edges weren't checked.
  - Its translations live in `user\shader_cache\CUSA00473_res1440` (23 MB). The normal cache is untouched.
- **The `ui` group A/B** (`LBP3_RES_SKIP=ui`): no visible difference at the map, the popit or the basket. What `0x433880` and `0x6cd050` do is still unproven. Both settings look right on this route.
- **Not checked:** fluid, Create mode, cutscenes with bars, photos (the photo target is a fixed 1280x720 offscreen view), and the stacked 2560x2880 targets' purpose.

### Cost [measured, frame split over each whole run, about 150 s; route walk and standing work from the `[video]` lines]

| Run | GPU submit + run (mean / p50 / p95) | Readback | Walk work | Standing work |
|---|---|---|---|---|
| 1080p (`ref1080`) | 4.20 / 4.10 / 5.75 ms | 0.93 ms | 19.59 ms | 18.30 ms |
| 1440p (`r1440`) | 6.19 / 5.96 / 8.22 ms | 1.22 ms | 21.11 ms | 18.89 ms |
| 1440p + shaders (`r1440s`) | 6.16 / 5.96 / 8.09 ms | 1.23 ms | 21.63 ms | 18.92 ms |
| 1440p + shaders, `ui` skipped | – | – | 21.37 ms | 19.19 ms |
| 1440p + shaders + frame generation (`r1440fg2`) | 5.40 / 5.26 / 6.92 ms | 1.32 ms | 21.66 ms | 19.58 ms |

- **GPU:** about +2 ms of GPU per frame (+47%) for 1.78x the pixels, and about +1.5 ms of game work in the walk. The slot wait stays about 16 ms, so the 30 fps budget has room.
- **Memory:** MainHeap +32 MB (the MainRenderTargets block) and LocalRenderTargets 635 MB at 1440p. Garlic stays at 1698 MB with no allocation failure. Host and Vulkan memory weren't measured.
- **Frame-capture hitches:** every run with captures has 1–3 windows of about 400 ms. Those are the capture hook's traces, not the game; the frame-generation run (no captures) had 0 frames over 40 ms.

### Frame generation at 1440p [measured]

`r1440fg2` ran with `LBP3_FRAMEGEN=1`, `LBP3_FRAMEGEN_MODE=warp` and no capture hook.
- **First attempt invalid:** the capture hook (`LBP3_FG_DUMP`) and frame generation install the same GPU hooks. With both on, frame generation got no HUD-less images or vectors and ran colour-only (run `r1440fg`, discarded).
- **Coverage:** 4,620 real frames, 4,500 generated (97.4%). Of the 120 dropped, 84 were 0/2-step flips, 21 the boot's 60 Hz screen and 11 late frames.
- **Inputs:** every fetch (4,500 of 4,502) came with the HUD-less image and the game's vectors, so the hooks find the 2560x1440 targets by role.
- **Generator GPU:** 1.79 ms mean (the estimate was 2.8; at 1080p it's 1.6–1.7).
- **A refresh late:** 297 generated frames (6.6%) at 165 Hz, against about 0 at 1080p in lane E's runs. At 240 Hz or with a smaller margin this may differ.
- **Stills** (`analysis\fg_stills\png\res_r1440fg2_*`): the popit box, its labels and "Close Popit" are crisp in the generated frame. A snowflake particle ghosts, which is the known gap (particles have no vectors).

### Verdict

**True 1440p works on this route** once the MainRenderTargets block is enlarged. It looks right, is sharper, costs about 2 ms of GPU and fits the 30 fps budget with frame generation on.

Before merging:
1. A human session through varied content: fluid, Create mode, cutscenes, photos, Manglewood.
2. Read the remaining 4 listed shaders' and any new shaders' `[res]` lines in other levels.
3. Decide the defaults: `LBP3_RES=1440` with `LBP3_RES_SHADERS=1`, `ui` group on.
4. Check the stacked 2560x2880 targets' use.

The patch set refuses to apply on any byte mismatch, so other game versions are safe.

## Night checks (2026-11-12 evening, stopped by the human partway)

Run data: `analysis\night_port\`, `analysis\night_r1440\`.

- **`port` `7d04e28`, frame generation off:** everything passed.
  - Introduction steps ≠ 1: 0.39% and 0.26%.
  - Thruster recipe 3 of 3.
  - Bunkum 16 of 16 and Needlepoint loads.
  - Sky cyan.
  - Photo: 1 JPEG.
  - Backup: 6 dialogs on the overlay; no save file changed on cancel.
  - Keyboard: PASS.
  - `t13_coop`: PASS.
- **1440p build `928c42f`** (`LBP3_RES=1440`, `LBP3_RES_SHADERS=1`, frame generation on): milestone 1's checks all passed.
  - Introduction steps ≠ 1: 0.00% and 0.00%. Frame averages 33.3–38.3 ms, with 8 and 3 frames over 40 ms after the level opened.
  - Thruster recipe 3 of 3: Create mode and water at 1440p.
  - Bunkum 16 of 16 and Needlepoint loads.
  - Sky cyan.
- **Not finished at 1440p** (the human said "stop for now" during the photo test): the photo JPEG, backup, keyboard, co-op, the heavy route and the stills. The clone's exe was restored by md5, and the saves are back, identical to `human_play_20261112_1458`.
- 2026-11-12 night, **the human played the 1440p build** on the 240 Hz display (`play_1440.bat`, frame generation on). The log shows 2560x1440 display buffers and every patch applied. They said "game plays wonderfully". They opened the backup (save) and import (load) dialogs at 1440p: "both of the dialogs are drawn crisply".
