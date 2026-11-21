# Sharper stickers, on by default

The port makes the sticker atlas 8192x8192 with the 16x16 grid kept, so every sticker tile is 512 px instead of the PS4's 256: ten checked same-length eboot edits (`core/sticker_patches.h`), applied all together or not at all. It is on by default at every render size, with a launcher checkbox and `LBP3_STICKERS=ps4` to keep the PS4's atlas. The output then differs from a PS4's, as the 1440p patches already do, and costs 192 MB more graphics memory. The player who reported "the stickers I place are hella pixelated" chose it (2026-11-20), and the sticker density is read only by the sticker code, so online play can't diverge over it.

Measured on the prototype (`prototype/sticker-atlas`, `.scratch/sticker-atlas/spike.md`): the same save and view in both builds, pixel-identical outside the stickers, about +10% sticker detail at full sticker density and +24% when the density is cut. Story levels cut it routinely (0.28 to 1.0 in one run). A sticker blown up past its own image's resolution doesn't get sharper. Booted at 1080p, 1440p and 4K, in the Pod and a story level.

## Considered Options

- **A 32x32 grid of 256-px tiles** (more tiles, the same memory). Rejected: the material shaders work in grid units (the 1/16 per tile is a shader literal), so it would need shader patching on top. Inferred from the code, not tried. It also gains nothing for one big surface, which the game caps at 31 tiles per axis.
- **Both: a 32x32 grid of 512-px tiles** (16384x16384, about 1 GB). Rejected for the memory.
- **Off by default, or only inside `LBP3_RES`.** Rejected: `LBP3_RES` makes no edits at 1080p, so 1080p players would never get it, and the player asked for sharper stickers by default.
- **Leaving it as on the PS4.** Rejected by the player; the setting keeps it one click away.
