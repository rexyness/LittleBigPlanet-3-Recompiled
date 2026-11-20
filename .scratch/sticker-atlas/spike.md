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

## Open

- **Look at the atlas directly.** In one run per build, check whether a 512-px tile holds real extra detail. F9 dumps every target, but it also traces every draw of that frame, so add a way to skip the per-draw dumps first.
- **Not measured:** the extra memory, and any stall on entering the Pod.
- **What triggers a cut in practice is not established.** The default Pod view of the test save stayed at G = 1.0 with 24 tiles in use. Sticker coverage, not only camera angle, may be what drives it.
- **The 31-per-axis cap evens out the levers.** A 32x32 grid holds the same texels for a large object as 512-px tiles at the cut. The earlier "32x32 grid takes the cut view to full density" comparison overstated it.
