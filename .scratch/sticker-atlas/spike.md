# Sticker atlas spike: 512-px tiles (prototype/sticker-atlas)

Throwaway code. The answer folds into the real change; this branch stays as the primary source.

## Question

Can the sticker atlas be made 8192x8192 with 512-px tiles (16x16 grid kept) by eboot edits alone, and does it make stickers visibly sharper?

## What was built

Ten checked same-length edits in `runtime/src/core/patches.cpp`, applied only with `LBP3_STICKERS_HD=1`:
- the atlas size at 0x427971 and 0x427976 (0x1000 → 0x2000);
- the two RenderDecals tile viewports, at 0x47ffee, 0x47fff7, 0x47fffd and 0x480002, and at 0x480549, 0x480554, 0x480557 and 0x48055c (shl 8 → 9, 0x100 → 0x200).

## Run (2026-11-20)

- **Setup:** a copy of the user's save; offline, muted; `LBP3_RES=1440`, shader rescale, frame generation, borderless on the test display.
- **Builds:** the same spike exe with and without the variable.
- **Shots:** boot to the Pod, one `cross`, then a still of the default view.
- **The 0.377 runs:** G (0x1062a30) was held there in the test process, with the controller's counter (0x13a168c) held at 0. That reproduces the cut the user saw with the ceiling in view.
- **Measure:** high-frequency detail of the floor sticker (std of a 4-neighbour Laplacian over x 760–1320, y 1300–1410 at 2560x1440). Higher is sharper.

| Floor sticker | 4096² (PS4) | 8192² (512-px tiles) |
|---|---|---|
| G = 1.0 (default view) | 5.38 | 6.09 (+13%) |
| G = 0.377 (held) | 3.24 | 4.02 (+24%) |

## Verdict

- **Mechanically sound.** All 10 edits applied and the game boots. Placement is identical: mean |A−B| is 0.29 of 255 at G = 1.0 and 0.82 at 0.377. There are no seams, and the page table, the rects and the free counts are unchanged.
  - Nothing in the material shaders needed patching, which confirms that the 1/16 per tile is in normalised terms.
- **The gain is small on this sticker.** The density cut costs about 40% of the detail, and 512-px tiles recover about a third of that.
  - B at 0.377 has nearly the same atlas texels per UV as A at 1.0 (16×512 = 8192 against the 31-tile cap's 31×256 = 7936), yet it scores 4.02 against 5.38.
  - So something else that scales with G costs detail. Likely the sticker image is sampled coarser when each tile covers more UV.
  - Keeping G at 1.0 matters more than adding texels.
- **The 32x32 grid isn't better on budget.** The per-object cap of 31 tiles per axis makes a 32x32 grid at G = 1.0 hold the same texels for a large object as 512-px tiles at 0.377. It would still win wherever the cut is avoided, for the reason above.

## Open

- **A cheaper candidate (one float):** 512-px tiles with the tiles-per-world-unit constant halved. 0.004 at 0xe84920 has one reader, 0x47f406. Visible pages then need a quarter of the tiles, so the 255-tile budget would stop forcing the cut in big views, while each tile still covers the same UV per texel as the PS4 at G = 1.0.
  - Untested.
  - It needs a save with the user's ceiling layout: their Garfield layout isn't in the save, which shows the Pod with a Squidward floor sticker.
- **The source image:** the Squidward floor sticker is blocky at G = 1.0 in both builds (stair-stepped outline), so it is limited by its own resolution. Cleaner cartoon stickers like the user's Garfield ones may gain more.
