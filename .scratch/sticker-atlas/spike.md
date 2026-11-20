# Sticker atlas spike: 512-px tiles (prototype/sticker-atlas)

Throwaway code. The answer folds into the real change; this branch stays as the primary source.

## Question

Can the sticker atlas be made 8192x8192 with 512-px tiles (16x16 grid kept) by eboot edits alone, and does it make stickers visibly sharper?

## What was built

Ten checked same-length edits in `runtime/src/core/patches.cpp`, applied only with `LBP3_STICKERS_HD=1`:
- the atlas size at 0x427971 and 0x427976 (0x1000 → 0x2000);
- the two RenderDecals tile viewports, at 0x47ffee, 0x47fff7, 0x47fffd and 0x480002, and at 0x480549, 0x480554, 0x480557 and 0x48055c (shl 8 → 9, 0x100 → 0x200).

## Run (2026-11-20)

- **Setup:** a local test save, offline, muted; `LBP3_RES=1440`, shader rescale, frame generation, borderless on the test display.
- **Builds:** the same spike exe with and without the variable.
- **Shots:** boot to the Pod, one `cross`, then a still of the default view.
- **The 0.377 runs:** G (0x1062a30) was held there in the test process, with the controller's counter (0x13a168c) held at 0. 0.377 is the cut seen in a play session.
- **Measure:** high-frequency detail of one floor sticker (std of a 4-neighbour Laplacian over x 760–1320, y 1300–1410 at 2560x1440). Higher is sharper.

| Floor sticker | 4096² (PS4) | 8192² (512-px tiles) |
|---|---|---|
| G = 1.0 (default view) | 5.38 | 6.09 (+13%) |
| G = 0.377 (held) | 3.24 | 4.02 (+24%) |

## Verdict

- **Mechanically sound.** All 10 edits applied and the game boots. Placement is identical: mean |A−B| is 0.29 of 255 at G = 1.0 and 0.82 at 0.377. There are no seams, and the page table, the rects and the free counts are unchanged.
- **The gain is small and not explained.** Some limit other than atlas size is in play.
  - The owner's rect is 31x31 tiles at G = 1.0 (capped at 31 per axis) and 16x15 at 0.377. So B at 0.377 holds about as many atlas texels per UV as A at 1.0 (16×512 = 8192 against 31×256 = 7936).
  - If the 512-px tiles carried twice the detail, those two would score alike. They don't (4.02 against 5.38), and B at 0.377 stays close to A at 0.377 (mean |diff| 0.82, against 3.14 between A at 1.0 and A at 0.377).
  - So either the tile content is an upscale, or the sampling caps the detail.
  - No CPU-side tile-size constant was found: no `shl 8` or `0x100` pixel sites beyond the ten, and no 256, 1/256, 4096, 1/4096, 512 or 1/16 float loads in the sticker code (0x46e000–0x481000, 0x271000–0x273000, 0x42b000–0x42e000).
  - The named constants the sticker code sets are `tc0biasscale`, `uvmultiplier`, `extrude`, `flipped`, `centroid_param`, `uThingCol`, `campos`, `decalScale` and `fSlotIndex`.
  - The limit is likely in the decal shaders (`decalvshader.vpo`, `decalpshader.fpo`) or in the material shader's atlas sampling. Not verified.
- **Only one sticker was measured.** It has a blocky outline even at G = 1.0 in both builds, so it is partly limited by its own image.

## Photo-sticker run (2026-11-20, later)

- **Setup:** the test baseline save, then `capfix_lane.sh`'s route to a Popit photo.
- **Placement:** `cross` (USE!), then `lstick 0 1 500` (Sackboy walks toward the camera and the held photo loses the red tint), then `cross`.
  - That placed it: the owner went from none to one (2x2 tiles in B, 3x2 in A; 4 and 2 tiles in use).
  - This is the first scripted sticker placement in this project.
- **Harness note:** multi-call sessions must use `m2_lib.sh`'s `human_keep`, `use_baseline` and `human_restore`. `saves.sh`'s `saves_keep` sets an exit trap that kills the game.
- **The dump:** `LBP3_DUMP_NO_TRACE=1` plus F9 dumps the atlas as `t07_8192x8192_*.bmp`, `R8G8B8A8Unorm`. That settles the cost: **+192 MB** (256 MB against 64 MB).
- **Tile detail:** score = rms(tile − up2(down2(tile))) / std on luma. 0 means the tile is an upscale with no detail at full resolution.

| Tile | Score |
|---|---|
| 4096 atlas, 256-px tile | 0.082 / 0.095 |
| The same tile upscaled 2x bilinearly (no extra detail) | 0.033 / 0.040 |
| 8192 atlas, 512-px tile holding photo content | 0.046 / 0.075 |

- **So the decal pass draws real detail into 512-px tiles.** There is no hidden 256-px assumption that reduces them to an upscale.
- **The pair isn't matched.** The owners differ (2x2 at [30,0] against 3x2 at [29,0]), so the sticker landed differently in each run. The photo's Sackboy is about 70 texels tall in A's tile and about 60 in B's. How much sharper the *same* sticker gets is still unmeasured.

## Matched A/B in the user's Pod (2026-11-20, the verdict run)

The Create level ("Edit Now") has no stickerable material, and scripted placement doesn't register in the Pod either (neither ✕ nor an emulated touch-pad tap). So the user placed library stickers by hand: a huge Garfield on the floor and one on the back wall.

To save Pod edits, leave the Pod (go into a level and back). "Quit Game" and closing the window don't save: the first attempt was lost that way. The save was copied, and the same copy was booted in both builds to the default Pod view.

- **The pairing is exact.** Both runs had G = 1.0, the same 31x31 owner and 227 tiles free. A no-sticker control region is pixel-identical (mean |A−B| 0.00).
- **Screen detail** (std of a 4-neighbour Laplacian):

| | A: 4096² | B: 512-px tiles | Gain |
|---|---|---|---|
| Floor Garfield, left half | 11.84 | 13.05 | +10% |
| Floor Garfield, right half | 10.66 | 11.75 | +10% |
| Wall Garfield | 8.90 | 9.67 | +9% |

  Visibly crisper outlines, clearest on the smaller wall Garfield.

- **Same atlas tiles across runs:** the contents are identical (means and spreads match). B's "detail above half resolution" is about 0.53× A's on most tiles and 0.8× on two. A pure 2x enlargement gives about 0.41×, and full double detail would give about 1×.
  - So the 512-px tiles add real detail, but on a sticker blown up this large the game's own sticker image limits the gain.
- **No density cut:** one big floor sticker plus one wall sticker kept G at 1.0. The original 0.377 needed heavier coverage.
- **Verdict:** the change works mechanically, with no shader patch.
  - It gives a real, moderate improvement: about +10% detail at full density (and +24% at a forced cut), for +192 MB of GPU memory.
  - It can't make an oversized sticker sharper than its own image.

## Open

- **Look at the atlas directly.** In one run per build, check whether a 512-px tile holds real extra detail. F9 dumps every target, but it also traces every draw of that frame, so add a way to skip the per-draw dumps first.
- **Not measured:** the extra memory, and any stall on entering the Pod.
- **What triggers a cut in practice is not established.** The default Pod view of the test save stayed at G = 1.0 with 24 tiles in use. Sticker coverage, not only camera angle, may be what drives it.
- **The 31-per-axis cap evens out the levers.** A 32x32 grid holds the same texels for a large object as 512-px tiles at the cut. The earlier "32x32 grid takes the cut view to full density" comparison overstated it.
