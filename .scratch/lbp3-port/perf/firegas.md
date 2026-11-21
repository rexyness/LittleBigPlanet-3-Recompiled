# A Create-mode level with fire and Horrible Gas: why it drops below 30 fps (2026-11-21)

The user built a small level ("Fire & Horrible Gas": an entrance, a block of the fire material, a block of
Horrible Gas, a cork) and played it with the DLSS build: 3840x2160 output, DLSS Quality (2560x1440
render), frame generation `warp`, online. It ran at 24–27 real fps for two minutes (work 37–43 ms),
then ~30–31 fps with frames over budget. The gas never appeared on screen.

**Labels:** **[measured]** from runs and logs; **[inferred]** otherwise.

## The repro

- **Save:** `analysis/savedata_firegas_20261121` (the user's saves after the session; not in git).
- **Route:** Pod → Cross (close the offline box) → Square (Pod menu, opens on Create) → Square (Edit Now)
  opens the level in Create mode at its start view.
- **Measure:** 20 status windows (600 frames) once `[faults]` passes 15,000 a window (the level's
  signature), on the 165 Hz test display, the `dlss` worktree's exe (`8399a84`), the user's
  launcher settings minus online (`LBP3_RES=3840x2160 LBP3_UPSCALER=dlss LBP3_UPSCALER_MODE=quality
  LBP3_FRAMEGEN=1 LBP3_FRAMEGEN_MODE=warp LBP3_ASYNC_PIPELINES=1 LBP3_LBPPC_COOP=1`), plus
  `LBP3_FRAME_SPLIT=1`. About 2.5 minutes a run.
- **Deterministic:** two baseline runs, work 35.7 and 35.9 ms [measured].

## Results [measured]

Median `work` (frame time minus the slot wait) over the 600 frames; 30 fps needs ≤ 33.3 ms.

| run | work | fps | frames > 40 ms of 600 | gfx blocked (fluid flag) | GPU finish |
|---|---|---|---|---|---|
| user's settings ×2 | 35.7 / 35.9 | 26.6 | 174 / 157 | 5.5 | 17.5 |
| `LBP3_FAULT_WINDOW=0` (faults 779 → 1,484 a frame) | 35.6 | 26.5 | 174 | 5.6 | 17.1 |
| `LBP3_GPU_ASYNC=1` | 34.1 | 28.3 | 82 | 5.7 | 15.9 |
| frame gen `repeat` | 32.0 | 29.6 | 16 | 4.3 | 11.0 |
| frame gen off | 25.0 | 30.0 | 0 | 4.0 | 10.0 |
| native 4K, no DLSS: off / repeat / warp | 25.5 / 30.2 / 35.4 | 30 / 30 / 27.0 | 0 / 0 / 147 | 4.3 / 4.2 / 5.5 | 9.8 / 9.9 / 16.9 |
| 1440 output, no DLSS: off / repeat / warp | 19.1 / 20.9 / 21.7 | 30 | 0 | 4.1–4.5 | 5.4 / 5.4 / 6.5 |
| native 1080, no res patches, no async pipelines, no LBPPC mode, no frame gen | 17.8 | 30 | 0 | 4.5 | 4.6 |

**The Pod against the level** (same runs, Pod frames before Edit Now):

| frame gen | Pod | level | the level adds |
|---|---|---|---|
| off | 21.7 | 24.9 | +3.2 |
| repeat | 26.8 | 32.0 | +5.2 |
| warp | 30.4 | 35.6 | +5.2 |

## What it means

1. **The level's own cost is +3–5 ms of serial frame time**, nearly all of it the graphics queue waiting on
   the fluid flag (`gfx_wait.md`): 4.0–5.6 ms a frame, every frame, once fire or gas is in the level
   (0 in the Pod). The fluid jobs and their ~780 write faults a frame are the same chain as the
   Needlepoint fluid frames.
2. **Write tracking is not on the critical path here.** Turning the fault window off doubled the faults
   and changed nothing.
3. **Frame generation at 4K output costs +9–11 ms a frame everywhere**; with it the Pod already runs at
   30.4 of 33.3 ms, so any heavier scene tips over. It follows the output size, not the DLSS path
   (native 4K is the same). At 4K the 60 Hz present path alone (`repeat`) costs +5–7 ms (main thread,
   prep and the readback copy slow down: CPU copies of 33 MB frames), and the warp another ~5 ms,
   almost all of it in the game's GPU finish (+7 ms over `repeat`).
4. **The warp's own passes are ~2.5–3.3 ms of GPU at 4K** (`LBP3_FRAMEGEN_GPU_PROFILE=1`: text
   0.5–1.1, compose 0.66–1.3, init l1 0.3, init l0 0.26, coarse 0.19, the rest < 0.1 each), against
   5.5–7.5 ms reported per generation and +7 ms on the game's finish.
5. **In the user's session the frame time tracks the generator's GPU wait:** in the level's windows,
   worker wait 0–1 ms → work 30.4; 4–5 ms → 36.3; 5–6 ms → 38.8.

**Drift:** later in the day the old exe measured 34.3–35.2 ms in the same repro (35.7–35.9 in the
morning). Compare only runs made back to back.

## What frame generation's 4K cost is made of

Knock-outs (a temporary `LBP3_FG_KNOCK` build, since removed), one piece left out per run. In the
level the control sits ~1 ms over budget, so any saving drops it into locked 30 fps and the stages
shift by themselves; **the Pod frames (under budget in every mode) are the clean read** [measured]:

| left out (Pod) | work | change | where |
|---|---|---|---|
| nothing (control ×2) | 29.0–29.2 | | GPU finish 14.0–14.5 |
| the three output-size input copies (upscaler picture, HUD-less, vectors) | 26.9 | −2.2 | finish 14.0 → 12.0, as `repeat` |
| the worker's full copy of each real frame as N−1 | 26.9 | −2.2 | prep 3.8 → 2.8 |
| the fetch's 33 MB copy of the generated frame | 28.4 | −0.7 | |
| the warp's passes | 28.7 | −0.4 | |

- The previous generation was never still on the GPU when the game's Finish began (0 of 30 in every
  window): no direct collision.
- The generator's buffers are all in video memory (`[framegen] GPU warp at 3840x2160: inputs in
  video / video / video memory`), so the copies' ~2 ms isn't PCIe; why ~100 MB of device copies
  costs that much inside the frame is not known.

## The fix (`perf/fg-4k`, `8748e7f`)

1. **No full N−1 copy for the GPU warp.** Generators say whether they read N−1 on the host
   (`NeedsHostPrev`); the live GPU warp doesn't (N−1 is on the GPU). The worker then keeps only the
   cut detection's samples (`DiffSamples`/`MeanAbsDiffSamples`, the identical value; framegentest
   checks it on random frames up to 3840x2160). A still request brings the full copy back.
2. **The upscaler-path input copies go into the Kick's own submission**, first, instead of the
   frame's last command buffer: after the frame (its Finish is done), before the game's next one,
   so the same pixels; `Record`'s first barrier orders them before the passes.
   `LBP3_FRAMEGEN_COPY_IN_FRAME=1` restores the old place. The game-sized path (no upscaler) still
   copies in the frame.

**Results, back to back with the old exe** [measured]:

| | old exe | new exe |
|---|---|---|
| Create mode, work | 34.7 / 34.9 / 35.2 ms | 30.1 / 30.8 / 31.6 ms |
| Create mode, fps; frames > 40 ms of 600 | 27.5–27.8; 103–112 | 29.8–30.0; 1–10 |
| Play mode, work; fps; > 40 ms | 38.6; 24.6; 313 | 32.0; 29.2; 39 |
| Pod, work | 29.2 / 29.4 | 27.2 / 28.2 (N−1 fix alone) |

- The old exe's Play mode is the user's bad state (37–43 ms in their log).
- Generated frames: 594 of 600 made, all with HUD-less images and the game's vectors, 0 declined,
  0 late (588 with the copies in the frame). The worker's wait for the GPU rises from ~4.5 to ~6 ms;
  generated frames were already shown a refresh after their target in the user's session.
- The fluid flag wait shrinks too (5.5 → 4.6–5.0 ms): the fire/gas jobs run faster with 33 MB less
  memcpy traffic a frame [inferred: memory bandwidth].
- Not run on the 240 Hz display.

## The gas

Neither the Horrible Gas block nor the fire material's flames draw, in every configuration above
(DLSS + frame gen, DLSS only, 1440, plain 1080, fault window off). The user confirms it should show
a gas cloud. A Thruster's flame glow does draw (Ticket 10's shots).

**The pipeline**, from an F9 trace of a level frame against a Pod frame (`LBP3_RES=1440`, no
upscaler, no frame gen; draw numbers from that trace) [measured]:

1. #4–17 splat the level into 512x256 RGBA8 slices, 8 render targets at a time, at `0x188700000`
   upward (512 KB apart). Slices 8–15 hold the blocks (the fire and gas shapes).
2. #20 reads the same memory as one 512x4096 RGBA8 atlas into `0x190700000`: the shapes arrive.
3. #21–84, 64 draws, write a 64-slice 512x256 R8 density volume at `0x1861ea000`, one slice each;
   #85–100 reduce it to 64x512 (one column per slice).
4. #119–128 read `0x188700000` as 256x8192 RGBA16F; #129–256 build two 256x128 lighting volumes
   (`0x178499000`, `0x176499000`).
5. Scene objects sample the density volume (#389, #402, #413–420); #492–493 draw the volumes'
   bounding geometry (160 indices) into two 2560x1440 targets; #494/#496 ray-march it into a
   1280x720 target; #500 composites that into the scene.

**What the dumps show** (render targets after chosen draws, `analysis\dump_draws.txt` + `f9`)
[measured]:

- The simulation has the shapes: the splat slices (#17), the atlas (#20) and the density volume's
  reduction (#100: data in slice columns 35–55 of 64, contiguous, the rest empty).
- **The effect's final draws produce nothing:** after #494 its 1280x720 target is all zero, and
  #492/#493 leave their 2560x1440 targets at flat values (0 and 255). #495 then fills the 1280x720
  target with a dim copy of the scene, and #496 adds nothing visible.
- #494 reads `0x178499000` (a 256x128 lighting volume that is a uniform 127, alpha 0) and
  `0x176499000` (all 0), among others; #129–256, which write them, read `0x188700000` as
  256x8192 RGBA16F, which is ~0 after #128.
- At level load the texture cache grows three of these images layer by layer as the slice draws
  reach higher slices (`ExpandImage` 1 → 2 → 3 → 5 → … → 64 layers, "SanitizeCopyLayers: Coercing
  copy … to minimum" each time), then turns them into 3D images: `0x1861ea000` 512x256x64,
  `0x1923cf800` 64x32x16, `0x178499000`. Once per session (19 expansions), not per frame.
- `0x188700000` also holds a 2560x1440 scene-sized target in the same frame (aliased memory).
- **Ruled out:** the geometry-shader gap (fixed, `d84eae8`; 0 draws dropped here); the res patches,
  DLSS, frame generation, async pipelines, LBPPC mode and the fault window (missing in all).

### Why they draw nothing (2026-11-21, `/diagnosing-bugs` on issue #27)

**The check:** `sh .scratch/lbp3-port/tools/gas_check.sh <tag> [NAME=value ...]` (FG_EXE picks the
exe; about a minute): the firegas save at 2560x1440, no upscaler / frame gen / async pipelines /
LBPPC mode, Edit Now, F9 with draws 486–508 dumped, judged by `gas_verdict.py`. The ray-march
shader (ps `f790c0f92f865e6d`) runs twice into the 1280x720 target: the **gas pass** (#494, into a
cleared target: non-zero pixels) and the **fire pass** (#496, after #495's dim scene copy: pixels
changed). GREEN only if both drew. `GAS_KEEP=1` keeps the session; then `gas_check.sh probe`,
`gas_check.sh dump <n,n>` (seconds each) and `gas_check.sh stop`. Baseline: `RED: gas pass #494
... colour 0 alpha 0 of 921600; fire pass #496 changed 0` (5 runs, same) [measured].

**What it found** [measured, instrumented build in `D:\Recomps\lbp3-wt\gas`, tags `[DEBUG-gas]`]:
- #492–494 assemble **no primitives at all** (pipeline statistics: input-assembly primitives 0,
  vertex shader invocations 0). Their index buffers (16-bit, 160–1080 indices, triangle fans,
  primitive restart on) hold only `0xffff`, the restart index. The draws that work have real
  indices (#119: `2 1 0 65535 3 2 0 65535 …`).
- The index buffers are written by the CPU (no GPU pass writes them; a hardware watch shows a
  per-frame fill with `0xffff`, writer at `0x1b145b20018`, callers `0x1b141f18391` / `…03bf5` /
  `…03f90`; which modules those are is not known: that session's `loaded … at` lines were
  overwritten, and the prx modules load at a different address every run). They sit in a ring of
  per-frame slots, so their addresses change (`0x1016c0180`, `0x1002c0180`, `0x133fc0180`, …; Y at
  X − 0x140000, Z at X − 0xa0000 in every frame logged). Three lists: X (#492, additive glow), Y
  (#493 and the first ray-march pass), Z (the second pass). **Y = the gas, Z = the fire is
  [inferred]** (with Z filled the flames show and no gas cloud does); the check's gas half rests on it.
- **The CPU builds them from compute-shader results the port never copies back to guest memory.**
  Copying back the buffers two compute shaders write, at the next fence, fills X and Z, and the
  fire block's flames draw on screen (`analysis/screenshots/m2_gas_csr.png` against
  `m2_fg_user_exe.png`): `cs f8ca5fa410806334` (12 dispatches a frame, 1x1x1/4x1x1, four 0x21000
  output buffers) and `cs db624fa6942b9961` (5 a frame, writes `0x13bd3f400`+0x20000). Two runs:
  fire pass changed 84,598 and 118,434 pixels. Without `db624fa6`: 677–2,731 (a flicker); without
  `f8ca5fa4`: 0. Debug switch: `LBP3_CS_READBACK=<cs hash>,…` (or `=1`: every compute-written
  buffer up to 4 MB, ~100 ms frames). **Not free:** status-line `work` 17.5–18.4 ms → 22.5–25.6 ms
  (+5–7 ms) at this 1440 setting, as each fence's copy waits for the GPU (`scheduler.Finish()` in
  `DownloadMemory`); the paced 33.3 ms hid it. A fix needs the copy without the wait (or once a
  frame); not measured at the user's 4K settings.
- **Y (the gas) stays empty** with every readback tried: all compute-written buffers, every linear
  image (`LBP3_READBACK_IMAGES=1`), shadPS4's relaxed buffer readbacks (`LBP3_READBACKS=relaxed`;
  it also fills X, by luck: it copies 512 KB around pages the CPU *writes*). Precise readbacks (the
  CPU's *reads* of GPU-written pages) stall the boot (no frame in 7 minutes), and switching them on
  after the level loads trips `page_manager.cpp:78 Not enough watchers`.

**Ruled out** [measured]: the density volume as a separate cache image (#21–84 render into image
#209, 3D 512x256x64, one slice each; #85, #389 and #494 sample that same image); the depth
(`17d170000` at #492: min 0.987, max 1.0, the scene's depth); occlusion queries (shadPS4 fakes
them as always visible); `ca36c7e3` (already in our branch as sgdc3 80ffa53's `Contains`).
Correction to the above: the splats #119–128 do draw (#119: 888 fragments); the targets' 0.0/255
average just can't show it.

**Not known:** what the CPU reads to build Y. Next probe: the code that writes X's real indices
(hardware watch on an X slot with `LBP3_HWWATCH_SKIP=ffffffffffffffff`, under the readback), then
what it reads for Y.

## Open

- The gas/fire effects (issue #27): the fire needs the two compute shaders' buffers copied back
  (above); the gas list stays empty, input not found yet.
- Play mode still has 39 of 600 frames over 40 ms with the user's settings. What's left on the
  serial path at 4K: the readback's 33 MB memcpy (3.3–5.1 ms, host-cached staging: near the
  bandwidth floor; removing it means handing the staging buffer to the presenter), and the fluid
  flag wait (4.6–5.0 ms; `gfx_wait.md` fix 5, Ticket 10's risk class).
- Why three device-local 4K copies cost ~2 ms inside the frame.

### Where the fix stands (2026-11-21, paused)

- **Built, uncommitted** in `D:\Recomps\lbp3-wt\gas` (branch `fix/gas-draws`, shadPS4 submodule
  dirty): `Rasterizer::MirrorFluidResults` + `BufferCache::MirrorToGuestAsync` copy the two
  shaders' output buffers to guest memory without waiting (`LBP3_FLUID_READBACK=0`: off). Fire pass
  changed 57,447 / 63,181 / 53,915 pixels; `work` unchanged (17.3–18.7 ms vs 17.0–18.4); off: 0.
- **Doubt:** its flames look different from the waiting copy's. Non-waiting copy (`m2_gas_fix_a/b.png`):
  an orange haze trailing right of the coal block. Waiting copy of every compute buffer
  (`m2_gas_csr.png`): a bright flame ball on the block. Either the copy lands a frame late, or the
  waiting copy's 512 KB windows also brought back other shaders' outputs. Screenshots taken to tell
  them apart, not yet looked at: `m2_gas_sync2_a/b.png` (waiting copy, the same two shaders only).
- Still to do: that comparison; the user's 4K settings (the gas worktree build has no DLSS);
  removing every `[DEBUG-gas]` line; the gas (Y list) input.

### The fire fix as built, and the gas so far (2026-11-21, later)

- **Fire fix (in `fix/gas-draws`, uncommitted):** `Rasterizer::QueueFluidResults` / `DownloadFluidResults`
  copy the two shaders' buffers to guest memory at the next fence, waiting for the GPU. Fire pass
  changed 125,212 / 8,162 / 46,587 / 52,892 / 130,900 pixels; off (`LBP3_FLUID_READBACK=0`): 0.
  Cost in this level: native 4K + frame gen 26.5 → 24.1 fps (work 36.0 → 38.9 ms, over-40 frames
  178 → 368 of 630); 1440 work 17.8 → 25.0 ms. The cost is the lock-step: the compute queue writes
  a label after each fluid batch and waits on the next ring slot, which a CPU job writes after
  reading the results (~17 round trips a frame), and each now waits for the GPU on one Vulkan queue.
- **Dead ends:** a copy that doesn't wait (free; the CPU builds from data a frame old, and a late
  copy can overwrite newer CPU writes); deferring the compute labels to GPU completion (no fire at
  all: the jobs check the label once and skip the frame). Screenshots can't tell copies apart: the
  fire's centre moves per session (x 1520–1807).
- **Gas:** `Flip_Index` (`spu.prx+0x18150`) calls the list builder (`0x17db0`) per mode; a particle
  word is life (bits 24–31) | type (16–19) | age (0–15). Mode 0 = Y (the gas pass) keeps types 7
  and 11; mode 2 = X keeps 3 and 8; mode 1 = Z keeps all but 3/7/8/11. The particles (217, read by
  the GPU shaders, written by the CPU) are only types 1 and 3: **no gas particle is ever spawned.**
  Nothing in `spu.prx` writes type 7 or 11, so the type comes from whoever requests the spawn.
- **Read probe** (debug remote command `readprobe <addr> <size>`): of the fluid images, the CPU jobs
  read only the density reduction (64x512 R8, linear) and #117's 64x32x16 RGBA8 storage volume
  (linear), from `Flip_Alive` / `Flip_Apply_Boundary_Markers` / `Flip_Add_Wind`. With
  `LBP3_READBACK_IMAGES=1` both are read back (logged as queued) and still no gas type appears.
  Next: what requests the spawns (the eboot's SPLUID chain setup) and why it never asks for gas.

### Correction: the gas draws with the fix (2026-11-21, user)

The user watched the fixed build and confirms the Horrible Gas shows: the gas is a particle type
the fix already covers (type 1, list Z, drawn by the second ray-march pass with the fire), not
types 7/11. List Y (types 7 and 11, the first ray-march pass #494) is another fluid kind this level
doesn't use, so its emptiness is not a bug. `gas_verdict.py` now goes GREEN on the second pass
alone. Judged live: baseline and `LBP3_FLUID_READBACK=0` RED (fire/gas pass changed 0), the fix
GREEN (8,162–130,900). The verdict compares the dumped BMPs, which each run overwrites, so it only
means something run right after its own dump (as `gas_check.sh` does), not on an old log.

### Confirmed (2026-11-21, user, playing the level)

The look the user confirms ("this is it": a bright flame ball on the burning block, a white gas
cloud beside it) needs **both**: the fluid buffers copied back (`QueueFluidResults`) **and** the two
GPU images the fluid jobs read (the density reduction, 64x512 R8 render target, and #117's
64x32x16 RGBA8 storage volume; both linear and 64 wide), here with `LBP3_READBACK_IMAGES=1` (2
images read back a frame). Without the images the fire drifts sideways as an orange haze: one of
them feeds `Flip_Add_Wind`. The particle-type histogram didn't change between the two, so it was
the wrong measure for this. To do: read back just those two images (not every linear image),
measure, strip `[DEBUG-gas]`, commit.

### The final fix, built and checked (2026-11-21)

`fix/gas-draws` (shadPS4 submodule, 3 files, no debug code; the probes are saved in
`tools/gas_debug_shadps4.patch` and `tools/gas_debug_runtime.patch`): the two shaders' buffers
(`Rasterizer::QueueFluidResults` / `DownloadFluidResults`, at the next fence, waiting) and the two
64-wide linear images (`TextureCache` `IsFluidCpuReadImage`, at the next submit or fence). One
switch for both: `LBP3_FLUID_READBACK=0`.
- `gas_check.sh`: GREEN ×2 (fire/gas pass changed 40,408 and 66,461), `LBP3_FLUID_READBACK=0` RED (0).
  Screenshot `m2_fin1_2.png`: the flame ball on the burning block, the white gas cloud beside it,
  the look the user confirmed in game.
- Image readbacks: 60 per 30 frames in the level (2 a frame), 0 with the switch off.
- Cost at 1440: work 24.6–25.4 ms against 18.1 off, the same as the buffers alone (25.0): the
  images add nothing measurable. 4K not re-measured with the images (buffers alone: 26.5 → 24.1 fps).
- Not tried yet: copying exact ranges instead of `ReadMemory`'s 512 KB windows (a page the GPU wrote
  and the CPU then rewrote would get the GPU's older bytes back); a non-waiting buffer copy now that
  the images are in (the sideways drift came from the missing wind image, not from timing).

### The two follow-ups, tried (2026-11-21, after 4efad9f7)

Debug variants in the gas worktree (`LBP3_FLUID_MODE=window|exact|async`, not committed), with the
two images copied back in all of them. Status-line work at 1440, and the fire's orange pixels in
three screenshots each (`firepos`: centre of the orange; the coal block sits at about x 1650, y 810):

| Copy of the buffers | Check | Work (ms) | gfx blocked (ms) | Fire centre (3 frames) |
|---|---|---|---|---|
| off | RED | 18.1 | 4.0 | – |
| waiting, 512 KB windows (committed) | GREEN ×2 | 24.6 / 25.4 | 11.9 / 12.2 | x 1639–1643, y 794–805 |
| waiting, exact ranges | GREEN ×2 | 26.3 / 26.5 | 12.3 / 12.6 | x 1630–1635, y 781–793 |
| not waiting | GREEN ×2 | 19.9 / 20.6 | 4.2 / 4.5 | x 1783–1802, y 740–760 |

- **Not waiting is wrong:** the flames float off the burning block, up and to the right, in every
  frame (`m2_async1_*.png` against `m2_exact1_*.png`). With the images in, the fire no longer
  wanders between sessions, so this is the copy's timing: the jobs build the lists from the
  previous frame's results. The cost stays.
- **Exact ranges work** and remove the overwrite risk of the 512 KB windows, at about 1 ms more at
  1440 (two runs each): each output buffer is now its own download, each waiting for the GPU,
  where one window covered several. Batching the ranges into one wait per fence would remove that.
  Not adopted: no overwrite has been seen.
