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

**Not known:** why #492–494 draw nothing. Candidates, none tested: their vertices (the volumes'
bounding geometry), the 3D images' contents after the array→3D conversion and per-frame slice
renders (shadPS4 renders into slices through 2D/2D-array views, `ImageViewInfo(ColorBuffer)`),
the RGBA16F view of `0x188700000` while the same memory is a scene target. The base project's
shadPS4 (`PurpleGitty/shadPS4`, 132 commits past our fork point of 2026-10-09) has texture-cache
fixes for missing slices (`ca36c7e3` #5524, `d3ff0e0c` #5433, `699009c9` #5444), none in our
branch.

## Open

- The gas/fire effects: why the final draws produce nothing (above; tracked as an issue).
- Play mode still has 39 of 600 frames over 40 ms with the user's settings. What's left on the
  serial path at 4K: the readback's 33 MB memcpy (3.3–5.1 ms, host-cached staging: near the
  bandwidth floor; removing it means handing the staging buffer to the presenter), and the fluid
  flag wait (4.6–5.0 ms; `gfx_wait.md` fix 5, Ticket 10's risk class).
- Why three device-local 4K copies cost ~2 ms inside the frame.
