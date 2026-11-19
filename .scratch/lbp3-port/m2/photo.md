# Milestone 2: black Popit photos

Clone `D:\Recomps\lbp3-recompiled`, branch `fix/photo-readback` from `m2/verify` `ac0ebfd`. Started 2026-11-10 23:59.

## Saves

Before the first run the human's `user\savedata\CUSA00473` was copied to `analysis\savedata_backups\before_photo_20261110_2359` (`diff -r` equal; md5 list `before_photo_20261110_2359.md5`). It is also equal to `before_m2v_20261110_2243`, which `m2_lib.sh` uses. Every run goes through `saves.sh` (baseline `savedata_thruster_on`).

## Display

First run (`ph_base`): `[video] LBP3_DISPLAY=165hz: window on "DQI27165" (165.01 Hz), one of 1 displays`, `display refresh 165.007 Hz`, `vblank source: \\.\DISPLAY2`, `[pace] ... refresh 165.01 Hz`. Only one display was attached.

## Reproduction and A/B

Script: `tools\t_m2v_photo.sh <tag> [NAME=value]` with `PHASE2=0`. JPEG pulled out of the test copy's `LBPxSAVE/00000001/bigfart*` with a scratch script (finds `FF D8 FF E0` ... `FF D9`, decodes with Pillow, prints mean and max RGB).

| Run | Env | Encoder line | JPEG in the save | Tile |
|---|---|---|---|---|
| `ph_base` | none | `[jpeg] encoded 640x360 BGRA, ratio 70 (quality 73) -> 4291 bytes (buffer 230400)` | `bigfart8` (7832 bytes) starts with the JPEG (offset 0, 4291 bytes); mean (0,0,0), max (0,0,0) | black (`m2_ph_base_pictures.png`) |
| `ph_relaxed` | `LBP3_READBACKS=relaxed` | 4291 bytes | mean (0,0,0) | black |
| `ph_precise` | `LBP3_READBACKS=precise` | none | no photo: the game never got past boot in the script's time (black window, no frame-status line after the savedata umount; `m2_ph_precise_1_stderr.txt`) | - |
| `ph_images` | `LBP3_READBACK_IMAGES=1` | 30911 bytes | 640x360, mean (131.9, 190.1, 200.0): the scene, sky cyan, wall, floor, Sackboy, the Thruster's edge | the scene (`m2_ph_images_pictures.png`) |
| `ph_imgrel` | `LBP3_READBACK_IMAGES=1 LBP3_READBACKS=relaxed` | 31011 bytes | mean (132.0, 190.2, 200.1), the same scene | the scene |

So buffer readbacks don't matter and the linear-image readback alone fixes it. The photo is the whole 1280x720 view at half size (the HUD and photo frame are not in it). With the photo, the game also rewrote `ICON0.PNG` (1063 -> 19074 bytes) and `littlefart85` grew (47020 -> 65028); with the black photo they didn't.

## The path from the frame to the encoder (static, Ghidra)

- `0x4026e0(…, flag=1)` -> `0x402560`: `0x4020f0` renders the scene again into the game's capture targets (render-target index 0x26 passed down), with a few full-screen passes (`0x424000` draws a screen quad); then `0xbce7f0` allocates w*h*4 bytes and `0x401ab0` queues the save job (`DAT_0132d6a0`, flag `DAT_0132d699`).
- `0x401a50` (main loop) counts calls and runs the job on the 4th one after the capture: `0x401760`. **The only wait is this frame count; there is no GPU fence or label wait.**
- `0x401760` first calls `0x4014f0(w, h, buffer, 4w, 0, …)`, a **plain CPU memcpy out of a render target's memory**:
  - The render-target table is at `0x133d8d0`, stride 0x68 (+0x8 width u16, +0xa height u16, +0x28 a sample/mip shift, +0x30 a memory handle; `0x57b80` turns the handle into a CPU address).
  - For h = render height / 2 the index is 0x27 (0x26 full, 0x27 half, 0x28 quarter); the copy is centred, and with a target as wide as the photo it is one memcpy of 4*w*h bytes at `0x401697`.
  - Then format 2 -> `0x4125a0` -> `0x412450` -> `sceJpegEncEncode`.
- So the GPU renders a linear target and the CPU reads its memory four frames later. In the port the image lives in Vulkan and nothing writes it back to guest memory unless `LBP3_READBACK_IMAGES` is set, so the memcpy reads zeros.

## Seen at run time (`ph_diag`, diagnostic build)

Temporary log `LBP3_LOG_LINEAR=1` in shadPS4's `TextureCache::FindRenderTarget`/`FindTexture` (storage): every linear (untiled) image the GPU writes, its first 3 uses and then every 300th. Breakpoints `--break=eboot.bin+0x4020f0` (capture render), `+0x40166c` (after `0x57b80`: rax = the target's address, esi = the copy width, ebx = the height) and `+0x401697` (the single memcpy: rsi = source, rdx = bytes). Log `analysis\m2_ph_diag_1_stderr.txt`.

- **The photo** (third `0x4020f0` call, r9 = 0x26) renders three linear R8G8B8A8 targets: `0x13b500000` 1280x720 (index 0x26), `0x13b884000` 640x360 (0x27), `0x13b965000` 320x180 (0x28), all `tile=8` (DisplayLinearAligned), pitch = width.
- Four frames later `0x4014f0` copies `0xe1000` bytes (640x360x4) from `0x13b884000` (the photo) and `0x38400` bytes from `0x13b965000` (the 320x180 thumbnail). The breakpoint's memory dump at `0x13b884000` and `0x13b965000` is all zeros: nothing ever wrote the GPU's image back there. Then `[jpeg] encoded 640x360 ... -> 4291 bytes`.
- Two earlier `0x4020f0` calls (at the Pod and when the level opened, another caller, `0x8025ec` <- `0x803a64`) render the same three targets and read a 256x256 square of the 640x360 one and 320x176 of the 320x180 one: the level/Pod thumbnails, presumably as black as the photo.
- The two earlier calls come from `0x7030ec` (in the Popit capture update `0x702c80`) -> `0x402f80` -> `0x403a5f` -> `0x402560`: the same `0x402f80` capture that Capture Object (mode 2) uses, format 0 (texture). The return addresses in the `[break]` "callers" lists are printed with 0x400000 added (`8025ec` = `0x4025ec`, `803a64` = after the call at `0x403a5f`, `b030f1` = after `0x7030ec`, `8017b5` = in `0x401760`, `801a88` = in `0x401a50`, `405b44` = after the main loop's call at `0x5b3f`). So **Capture Object and these portrait/icon captures read through the same `0x4014f0` copy from the same linear targets**, and were black for the same reason. Evidence that this path is fixed too: the profile's save icon `LBPxSAVExLITTLE/ICON0.PNG` (320x176, the size of the RT 0x28 read) is all (0,0,0,0) and 1063 bytes in `ph_base`/`ph_relaxed` (and in the baseline and the human's saves), and a Sackboy portrait (19074 bytes) in `ph_images`/`ph_imgrel`. The Capture Object marquee itself was not driven (it needs cross held while the stick moves; the remote input runs one command at a time).
- **Other linear images the GPU writes in normal play:** two 64x512 R8Unorm render targets (`0x13c96f400`, `0x13b99d400`) about once per frame each, and a 64x32x16 R8G8B8A8 storage image (`0x152df8a00`) now and then. Nothing else. `LBP3_READBACK_IMAGES=1` downloads every GPU-written linear image synchronously (`scheduler.Finish()`) at every submit and every EOP/EOS fence, so it waits for the GPU about once a frame because of the two 64x512 targets (inferred from the code; not measured as a stall).
- The saved `ICON0.PNG` with readback has content but its red and blue look exchanged: as stored (R,G,B,A) the background is (92,92,0) with alpha 92 and Sackboy's knit is light blue with B at 255 (e.g. (194,208,255,164)). Read as B,G,R,A the same samples are a beige Sackboy (255,208,194) on teal (0,92,92). So the target's memory has the same byte order as the photo's (B,G,R,A, which the photo's colours confirm), and the open question is only whether the game's format-1 PNG path (`0x4133c0` -> `0x413160`, libpng row pointers, pitch 4w) swaps; no swap call was identified there. Same bytes with `LBP3_READBACK_IMAGES=1` and with the fix. **So the fix turns a blank (all-zero) save icon into a red/blue-exchanged one.** Whether the PS4 icon looks the same was not established. Left open.
- Frame times in the Create level after it opened (49 status windows each, scratch script `fstats.py`): `ph_base` avg 33.39 ms, work 24.1 ms, 8 frames over 40 ms; `ph_images` 33.37 ms, work 23.8 ms, 9 over 40 ms; `ph_diag` (no readback) 33.37 ms, 8. So in this level the global switch costs nothing measurable either; the Introduction and loads were not measured with it.

## Fix

Branch `fix/photo-readback` in both repos, committed locally, nothing pushed.

| Repo | Commit | What |
|---|---|---|
| shadPS4 submodule (from `port` `25b48b3a`) | `88d84b6e` | `texture_cache.cpp`: `IsCpuReadRenderTarget(info)` (untiled, not depth, 32 bits per texel, 1 sample, depth 1, 1 layer, guest address set); `FindRenderTarget` queues such images for the existing `ProcessDownloadImages` (synchronous download at the next submit or EOP/EOS fence, the `readbackLinearImages` mechanism) without `LBP3_READBACK_IMAGES`; `g_lbp3ImageReadbacks` counts downloads |
| shadPS4 submodule | `50b4720a` | `ProcessDownloadImages` downloads those candidates only while `g_lbp3CaptureQueued` (the game's capture-job byte) is non-zero, and drops them otherwise; `readbackLinearImages` unchanged |
| clone (from `m2/verify` `ac0ebfd`) | `adf6e04` | gitlink; `runtime/src/hle/videoout.cpp`: the `[video] ... frames flipped` line says `N new images, M expanded, K read back`; comment in `runtime/shim/core/emulator_settings.h` |
| clone | `dd67f93` | gitlink; `runtime/src/gpu/renderer.cpp`: after the rasterizer is created, checks the job runner's code at `eboot+0x401a5a` (`4c 8d 35 38 bc f2 00 41 80 3e 00`) and hands `eboot+0x132d699` to the texture cache, or logs `[gpu] the capture job code differs from v01.28: photos will be black`; settings comment updated |

`adf6e04`/`88d84b6e` alone are not the fix: see "The Bunkum hitch" below. The branch head (`dd67f93` / `50b4720a`) is what was verified.

How it works: a capture render target (linear 32-bit 2D colour) drawn while the game's byte `eboot+0x132d699` is 1 is downloaded synchronously at the next submit or fence and written back to guest memory, where the game's `0x4014f0` memcpy reads it four frames later. `0x401ab0` sets the byte when it queues the capture job (right after `0x4020f0` has recorded the capture's draws, before the frame is submitted) and `0x401a50` clears it after the job has run. In every run the byte read 1 at all capture downloads.

Cost: 3 downloads per capture (1280x720, 640x360, 320x180, 4.6 MB). The first waits for the GPU (`Finish()`): 10.8-20.5 ms; the other two 0.15-0.68 ms (temporary timing log, `ph_gate`, `photo_bunkum_diag`, `photo_bunkum_gate2`). Captures happen at the Pod, when a level opens, on Save Changes / play mode (the Thruster recipe shows 4 per run), and per photo. No download in any other frame.

Not chosen:
- `LBP3_READBACK_IMAGES` by default: it downloads the two 64x512 R8 targets synchronously every frame and the cutscene's 1920x2160 target too.
- An asynchronous write-back: avoids the `Finish()`, but the port's write-back is a plain memcpy that faults on the image's write-protected pages and marks images CPU-dirty, so a late write-back can re-upload old data over a newer GPU frame; and it would still write the cutscene target over the per-frame ones.
- A size or aspect test: the cutscene target shows that "linear 32-bit" is not specific; the game's own flag is.
- Hooking the read at `0x4014f0`: exact, but needs a native call hook the runtime doesn't have.

### The Bunkum hitch (why the gate)

With `adf6e04` alone, `m1_checks.sh photo` passed (table below) but its Bunkum log had one `1 read back` window during the cutscene, right after `[game] CMD_SETPOINTSIZE`, with a 169.7 ms frame where `m2v_bunkum` and `m1port_bunkum` have 70.5 and 71.0 ms. A temporary timing log (`photo_bunkum_diag`) showed `[readback] 0x13b99d400 1920x2160 fmt 37 bits 32 tile 8: 117.60 ms` (frame 172.1 ms). The cutscene draws once into a linear 32-bit 1920x2160 target that spans `0x13b99d400` to `0x13c96f400` (`+ 1920*2160*4`), i.e. from one per-frame 64x512 R8 target to the other, so its write-back also landed on them.

With the gate (temporary log printing the byte at each candidate): `ph_gate` (photo, both phases) `flag=1 downloaded` for all 5 captures; `photo_bunkum_gate2`: `[readback] 0x13c96f400 1920x2160 flag=0 dropped`, and that window's worst frame 70.4 ms (2 over 40 ms), `0 read back`. (`photo_bunkum_gate`, the run before, never drew that target: no `CMD_SETPOINTSIZE` that time; the game doesn't do it every run.)

## Verification on the branch head (`dd67f93`, no LBP3_* variables)

- `ph_final` (`t_m2v_photo.sh ph_final`, phase 2 on): `[jpeg] encoded 640x360 BGRA, ratio 70 (quality 73) -> 30902 bytes (buffer 230400)`. `bigfart8` 48920 bytes, the JPEG at byte 2764 (`analysis\screenshots\m2_ph_final_extracted.jpg`), mean (132.0, 190.1, 200.0): the scene. The My Pictures tile shows it (`m2_ph_final_pictures.png`) and still does after the relaunch (`m2_ph_final_2_pictures.png`). USE! puts it on the cursor as a sticker (`m2_ph_final_sticker.png`). `ICON0.PNG` 19074 bytes. Status lines: `3 read back` once per capture (3 windows in phase 1, 2 in phase 2), `0 read back` in the other 135. No `S#`, stuck, device-lost or "capture job code differs" line. Both launches `LBP3_DISPLAY=165hz: window on "DQI27165" (165.01 Hz)`.
- Evidence sheet: `analysis\screenshots\m2_photo_evidence.png` (before/after JPEG, the window's photo-frame area, the tile before/after/after relaunch, ICON0 before/after, the sticker).
- **Colours.** The photo is the content of the on-screen photo frame (the rectangle (256,143)-(1024,575) of the 1280x720 window) rendered at 640x360. Against that crop of `m2_ph_final_frame.png` scaled to 640x360 (mean RGB):

  | Patch (photo pixels) | Photo (JPEG) | Window capture |
  |---|---|---|
  | sky (400-600, 40-140) | (163.0, 238.0, 244.0) | (155.3, 227.4, 233.6) |
  | wall (20-120, 40-140) | (101.1, 150.4, 169.2) | (94.7, 140.9, 158.1) |
  | floor (20-200, 290-340) | (105.1, 149.6, 166.0) | (93.3, 132.7, 147.0) |
  | Sackboy (300-340, 285-340) | (156.5, 155.6, 126.8) | (142.3, 150.3, 122.2) |

  The channel order matches everywhere (cyan sky R < G < B, beige Sackboy with B lowest); an R/B swap would make the sky (244, 238, 163). The photo is 5-10% brighter than the window (the photo frame may dim the screen slightly). Mean absolute difference 11.1 of 255. There is no red object in this view, so "red stays red" was checked through the cyan/beige order instead. (`ph_fix`, on `adf6e04`, gave the same numbers.)
- **Sticker.** `ph_stk` (scratch script `sticker_check.sh`, on `adf6e04`) and a hand-driven session after a relaunch on its saves (`m2_ph_hand_*.png`, `analysis\m2_ph_hand_stderr.txt`): My Pictures (after the relaunch) -> the photo -> USE! puts it on the cursor with the wall, floor and Sackboy visible. Over the sky it is tinted red with the same R:G:B ratio (about 1 : 0.29 : 0.15) everywhere in the picture, so the red is the game's "can't place here" tint over the picture's brightness, not its colour; the part over the wall face loses the tint (`m2_ph_hand_f.png`). **Placing it was not achieved:** the hint asks for a touch-pad tap with a finger; the remote input has the touch-pad button but no touch points, and neither Cross nor the touch-pad button placed it.
- Capture Object proper (the marquee) was not driven; it reads through the same `0x4014f0` copy (above), and the portrait captures on the same `0x402f80` path now have content (`ICON0.PNG`).

## Milestone 1 regression (165 Hz)

`sh tools/m1_checks.sh photo` on `adf6e04` (output `analysis\photo_m1_checks_out.txt`) and `sh tools/m1_checks.sh photo2` on the branch head `dd67f93` (`analysis\photo2_m1_checks_out.txt`). Every log has `LBP3_DISPLAY=165hz: window on "DQI27165" (165.01 Hz)` and `display refresh 165.007 Hz`, and no `Rejecting invalid S#`, stuck-wait, device-lost or "capture job code differs" line.

| Check | Baseline (m2/verify, 165 Hz) | `adf6e04` | `dd67f93` (branch head) |
|---|---|---|---|
| 1. Introduction | steps != 1 0.19-0.25%; averages 33.3-35.6 ms; 3 frames over 40 ms per run | 0.06% / 0.06%; 33.3-35.0 / 33.3-34.4 ms; 3 / 2 over 40 ms | 0.56% / 0.19%; 33.3-34.4 / 33.3-34.4 ms; 2 / 4 over 40 ms |
| 2. Thruster recipe | 3/3 | 3/3 (20 mounts each) | 3/3 (20 mounts each) |
| 3. Bunkum | 16/16 | 16/16, Needlepoint loads, 0 watchdog | 16/16, Needlepoint loads, 0 watchdog |
| 4. Create sky | (121, 177, 182) | (121, 177, 182) | (121, 177, 182) |
| Bunkum cutscene window (`CMD_SETPOINTSIZE`) | worst 70.5 ms (`m2v`), 71.0 ms (`m1port`), 1 over 40 ms | **worst 169.7 ms, `1 read back`** | worst 71.9 ms, 1 over 40 ms, `0 read back` |

On the branch head the downloads are: Introduction 2 windows of `3 read back` (the Pod, frames 91-120, and the level just after it opens, frames 1441-1470), both with worst frame 33.3-33.6 ms and none over 40 ms; each Thruster run 4 windows of 3; Bunkum 2 windows of 3; `0 read back` in every other window.

The Introduction differences on the branch head are not in the download windows:
- run 1's 9 non-1 steps: one at the first counted flip (also there in the baseline and `adf6e04` runs) and a 0/2 cluster at level flips 930-946, about 30 s after the level's capture, with no download near it and no frame over 40 ms;
- run 2's 4 frames over 40 ms: 3 in the load window (frames 1381-1410, `0 read back`, as in every run) and one of 42.6 ms at frames 2401-2430 (`0 read back`).
The `m1port` runs had 0.69% and 0.44%, so 0.56% is within this check's run-to-run spread.

## End state

- The human's `user\savedata\CUSA00473` is identical to `analysis\savedata_backups\before_photo_20261110_2359` (`diff -r` finds nothing; the md5 list `before_photo_20261110_2359.md5` matches), which is also identical to `before_m2v_20261110_2243`. No `analysis\savedata_human_kept`. The baseline `savedata_thruster_on` is intact (`bigfart7` `c94640d3`).
- No `lbp3.exe` running. `build\lbp3.exe` is built from the branch head (no diagnostic code).
- Clone on `fix/photo-readback` at `dd67f93`, submodule on `fix/photo-readback` at `50b4720a`, both trees clean. Nothing pushed. The game dump and firmware dump were not written; the worktrees under `D:\Recomps\lbp3-wt\` were not touched.
- Scratch scripts (session scratchpad, not in any repo): `extract_jpeg.py` (pulls JFIFs out of `bigfart*`, prints mean/max RGB), `fstats.py` (frame stats after a level opens), `sticker_check.sh`.

## Addenda (after review)

- **Sticker tint control** (hand-driven on the baseline, `dd67f93`, `m2_ph_ctl_*.png`, log `analysis\m2_ph_ctl_stderr.txt`): Stickers page -> Colors -> "White Square" -> USE! puts a red-orange square on the cursor over the sky, (255, 66, 32), ratio 1 : 0.26 : 0.13 (`m2_ph_ctl_c.png`). The photo sticker at the same spot read (214, 62, 31), 1 : 0.29 : 0.15. So every sticker gets this red tint there; it is not specific to the photo. Whether it is the game's "can't place here" colour or something the port draws wrongly for all sticker previews was not established (no sticker was placed).
- **Cost of a photo** (status window containing the `[jpeg]` line, frames 1921-1950): `ph_base` worst 35.8 ms, 0 over 40 ms; `ph_final` worst 41.2 ms, 1 over 40 ms, with `3 read back`. So taking a photo costs one frame of about 41 ms (the first download's `Finish()` was 10.8-20.5 ms in the timing runs).
- **Residual exposure:** any linear 32-bit colour target drawn during a capture's four-frame window is still downloaded synchronously. A window whose count is not a multiple of 3 would show it; none was seen in any run.
- **Side effects:** the profile's `ICON0.PNG` goes from blank (all zero, 1063 bytes) to a 19074-byte portrait with red and blue apparently exchanged (above). Create sessions that take a photo or reach a capture now write bigger saves (`littlefart85` 47016 -> 65024-65028 bytes, `bigfart8` 7832 -> about 49 KB with a photo), which shifts the sizes other scripts' notes quote.
- **Not driven:** placing a sticker (needs a touch-pad tap), the Capture Object marquee, and looking at a DXT capture texture itself; `ICON0.PNG` and the My Pictures tile are the visual evidence for the non-JPEG paths.

## ICON0 colours (static check)

**Answer: neither of the two options as asked.** The game does not swap R and B, and it does not write the bytes unchanged. It turns on libpng's swap-alpha transform, which assumes the buffer is A,R,G,B. Applied to the capture target's B,G,R,A bytes, it stores each pixel as (G, R, A, B) of the true colour. The port's `ICON0.PNG` is exactly what this code produces, so **the port is faithful and should not add a swap** (conclusion (b) in substance). This replaces "red and blue exchanged" in "Seen at run time" and "Side effects": the knit's "B = 255" is its alpha. Decompiles: `tools/ghidra_decomp.sh`. Disassembly: `analysis/eboot_full.asm`.

Format-1 path (`0x401760` -> `0x4133c0` -> `0x413160` -> `0x412fa0`, statically linked libpng 1.6.17, version string at `0xe80060`). `png_ptr->transformations` is the u32 at `+0xcc`; `0x9bd4b0` (png_set_interlace_handling) ORs `PNG_INTERLACE` = 2 into it.
- `0x401820 call 0x4133c0` receives the job buffer directly after the `0x4014f0` copies. Nothing touches the pixels in between. (`0x4133c0` has a second caller, `0x401de1` in `0x401d50`, called from `0x404354`, which passes its caller's buffer in the same way. Which of the two writes ICON0 was not determined. It doesn't matter: the file shows that unconverted B,G,R,A bytes reached libpng.)
- `0x412fa0` calls, in order:
  - `0x9be440` png_create_write_struct_2 and `0x9aca10` png_create_info_struct;
  - `0x9bdce0` png_set_write_fn;
  - `0x9bebb0(png, 1)`, which writes `+0x154`, the zlib level (not a transform);
  - `0x9bc490` png_set_IHDR, with `0x4130bd mov r8d, 0x8` and `0x4130c3 mov r9d, 0x6` (8-bit, colour type 6 = RGBA, other fields 0);
  - `0x9bdf70` png_write_info;
  - then `0x413112 call 0x9bd590`:
  `void FUN_009bd590(long p) { if (p != 0) *(byte *)(p + 0xce) |= 2; }`
  This is transformations |= 0x20000, `PNG_SWAP_ALPHA` (png_set_swap_alpha). It is the only call to `0x9bd590`, and the only reference to its address, in the `.text` disassembly.
- `0x413160` points the row pointers into the capture buffer itself (`rows[i] = buf + i*4w`, `0x4131e5`-`0x413332`). It then calls `0x9b12e0` (png_get_rowbytes; the result is unused), `0x9be8c0` png_write_image, `0x9be290` png_write_end and `0x9be9b0` destroy. png_write_image calls `0x9be4f0` png_write_row, which copies the row into libpng's `row_buf` and then runs `if (*(int *)(p + 0xcc) != 0) FUN_009becc0(p, &row_info);`.
- `0x9becc0` (png_do_write_transformations): for colour type 6 at 8 bits, the swap-alpha branch does
  `if ((*(byte *)(p + 0xce) & 2) != 0) ... if (color_type == 6 && bit_depth == 8) for (; n; n--) { t = q[0]; q[0] = q[1]; q[1] = q[2]; q[2] = q[3]; q[3] = t; q += 4; }`.
  So PNG (R,G,B,A) = memory (b1,b2,b3,b0), i.e. ARGB -> RGBA. It works on libpng's copy of the row; the capture buffer is unchanged.
- The BGR branch never runs. `if ((*(uint *)(p + 0xcc) & 1) != 0) FUN_009bd960(...)` (`0x9bd960` swaps bytes 0 and 2) needs `PNG_BGR`. png_set_bgr is `0x9bd460` (`or byte ptr [rdi + 0xcc], 0x1`), and the `.text` disassembly has no call to it and no reference to its address. The path does not use png_write_png (no transforms argument).

The other encoders read the same copies as B,G,R,A:
- **JPEG** (format 2, `0x412450`): the encode-param block sets pixelFormat (`+0x24`) to 1 (`local_54 = 1`), which is B8G8R8A8 (`kEncB8G8R8A8 = 1`; the port's `jpeg.h` asserts the offset is 0x24).
- **DXT** (format 0, and the photo's half-size thumbnail when byte `+0x5a` is 0):
  - Arguments: `0x4018bd` passes 1 and 1 as its last two stack arguments (`[rsp+0x38]`, `[rsp+0x40]`) and 0 as the ninth. `0x411d10` forwards them to `0x92d550` (`0x411de2`/`0x411dea`).
  - Gather: with those values, `0x92d550` builds each block pixel as (src[2], src[1], src[0], src[3]).
  - Packing: `0x92d300`/`0x92d120` put block byte 0 in the top bits of the 565 colour (`(b2 >> 3) | (b1 & 0xfc) << 3 | (b0 >> 3) << 0xb`). So red comes from memory byte 2: B,G,R,A.
  - The gather can also take alpha-first sources (flags bit 1 set and the last argument 0 gives (src[1], src[2], src[3], src[0])), but the capture job doesn't use that mode.
- **Same target:** with the 720-line frame, `0x4014f0` maps a height of 180 or 176 to RT 0x28. So the photo's 320x180 thumbnail (JPEG or DXT) and the 320x176 icon come from the same target. Two of the game's paths treat it as B,G,R,A; only the PNG path treats it as A,R,G,B.

The port's file matches the swap-alpha reading (`analysis/m2_ph_final_testsaves/LBPxSAVExLITTLE/ICON0.PNG`, 320x176):
- **Stored B is the alpha.** It takes only 5 values: 0 (51140 px), 255 (5083), 127, 64 and 191, a coverage mask.
- **Stored A is a colour channel.** It takes 254 values and correlates with stored R (0.83).
- **Undoing the rotation** (true RGBA = stored (G, R, A, B)) gives an opaque beige Sackboy in the explorer outfit, with clean edges, on a fully transparent background. The background (92,92,0,92) becomes grey 92 at alpha 0, and the knit (194,208,255,164) becomes (208,194,164,255), B lowest like the photo's Sackboy.
- Scratch files, not in any repo: `%USERPROFILE%\AppData\Local\Temp\claude\562e71d78f9ffc95\0c319d93-d1d3-42a7-85c8-ac3efa29d854\scratchpad\icon0_sheet.png` (stored vs remapped) and `remap.py`.

Not provable statically: what a real console writes. Only a PS4 save's `ICON0.PNG` would show that. A guess, not established: the swap-alpha is left over from the PS3 engine, where big-endian A8R8G8B8 surfaces hold A,R,G,B in memory.

## Sticker placement, one more attempt (merged head `d534bbe`)

Scratch script `sticker_try.sh fin_stk`: photo, USE!, cursor moved left twice (`lstick -1 0 900`), Cross (`analysis\screenshots\m2_fin_stk_*.png`). The cursor stops at the near wall's front edge; the part of the picture over the wall loses the tint and the part over the background stays red (`m2_fin_stk_left2.png`). Cross placed nothing (`m2_fin_stk_placed.png`, `m2_fin_stk_moved_away.png`: the picture is still on the cursor). The rotating hints there are **Place Sticker** with a touch-pad tap icon and **Scale / Rotate** with a two-finger icon, besides Flip Sticker (R3), Pause, Hover Mode Off and Undo. So Cross didn't place it *here*, with the cursor at the wall's edge and part of the picture still red; whether placing needs a touch-pad tap is **not established** (ticket 08 found the touch pad optional elsewhere). A DualShock 4 or DualSense delivers touch points through SDL (`gamepad_sdl.cpp`, `SDL_GetGamepadTouchpadFinger`); pads without a touch pad and the remote input give only the touch-pad *button* (Back is mapped to it), no points. Left to the human session: Cross first on a flat surface, then a touch-pad tap. A remote `touch` command or a touch emulation for other pads is a follow-up if it turns out to be needed.
