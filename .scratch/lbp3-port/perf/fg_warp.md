# Frame generation, lane D: our own motion-compensated generator (2026-11-12)

This is the default prototype generator for opt-in frame generation ([fps60.md](fps60.md), present path in [fg_present.md](fg_present.md)). It plugs into lane C's generator interface as `LBP3_FRAMEGEN_MODE=warp`. It is developed and judged offline on lane A's dump sets ([fg_inputs.md](fg_inputs.md)). The game was never launched for this lane.

- **Branch:** runtime `exp/fg-warp`, commits `c9aca3f` and `5e1fef5`, on lane C's `exp/framegen` `7f86658`. Pushed to the fork; `port` is untouched. Worktree `D:\Recomps\lbp3-wt\fgw`, with shadPS4 a junction to `fgp-shadps4`; no shadPS4 changes.
- **Files:** `runtime/src/video/framegen_warp.{h,cpp}` (new), `video/framegen.{h,cpp}` (registration, two `GenInputs` fields), `tests/fgeval.cpp` (new), `tests/framegentest.cpp` (warp checks).

## How it works

All of it runs on the CPU, multi-threaded (its own pool, 8 threads by default), with SSE2. It works on host copies only.

1. **Luma pyramid:** half, quarter and eighth resolution, (R + 2G + B) / 4, so the channel order (RGBA or BGRA) doesn't matter. N's pyramid is reused as the next frame's N−1 when the content signature matches.
2. **Two one-sided block-motion fields:** 8×8 blocks at each level, coarse to fine. It runs a full search of ±128 px of motion at 1/8 resolution, then takes the parents' vectors as candidates with a ±1 refinement, then two neighbour/median smoothing passes. One field is anchored on N (where each block of N came from) and one on N−1 (where each block of N−1 went). Anchoring on a real frame's content means a thin object can't be "matched away".
3. **The in-between field:** one vector per 16×16 output block. It is chosen only among the motions actually measured around the block: both one-sided fields at the block and its four neighbours, zero motion, and the game's vector. The winner is the candidate whose two ends (N−1 at p − m/2, N at p + m/2) agree best. Two smoothing passes follow. The first version searched every vector bilaterally, and it erased the Needlepoint flagpole and its ropes (`np_walk` k=20, below). Restricting the candidates fixed it, which is why it works this way.
4. **Compose, per pixel:**
   - The block field is bilinearly interpolated, and each side is sampled bilinearly (4 channels at once).
   - If the two samples agree within 24, they are averaged.
   - Otherwise the pixel tries the distinct vectors of its 3×3 blocks. If none comes within 48, it takes N's sample ("the nearer frame").
   - A pixel within 2 in N−1 and N is copied unchanged, which keeps static HUD text sharp even without a HUD-less image.
5. **Optional inputs** (`GenInputs`), with lane A's facts:
   - **The game's vectors** (N's velocity target: m.x = (B−127)·0.941, m.y = (G−127)·0.529, R = coverage). Per block, the median of covered, unsaturated vectors becomes one more search candidate.
     - The frame is declined (`Drop::Generator`) when over 97% of the covered vectors are saturated. That is the camera cut, where saturation is 100%. The map's camera swoop (33–93%) is not declined: the colour search handles it better than skipping does.
   - **The HUD-less images of N−1 and N.** These are interpolated instead of the finals. Where N differs from its HUD-less image by more than 8, the pixel is taken from N (the screen HUD: banners, subtitles, bars). Elsewhere that small difference (the display pass) is added on, so unchanged areas come out exactly as N.
     - If the HUD mask covers over 40% of a sparse grid, the HUD-less image is stale (the loading screen has no composite), and the generator uses the finals.
6. **Settings:** `LBP3_FRAMEGEN_WARP_*` sets threads, radius, refinement, smoothing, lambdas, thresholds, fallback, `VEC` (0 = ignore the game's vectors) with its decode, `HUDLESS` and `HUD_T`.

## The offline evaluator

`build\fgeval.exe <set dir> [--gens warp,blend,repeat] [--novec] [--nohud] [--stills k,..] [--out dir] [--crop x,y,w,h]`

- **Skip-one score:** generate the midpoint of k−1 and k+1 (with k+1's vectors ×2), then compare it with the real k.
  - The comparison is PSNR over the frame and over the moving pixels (k−1 and k+1 differ by more than 10), plus the mean absolute error on the moving pixels.
  - Only k whose neighbours are one simulation step away count.
  - A declined frame is scored as the repeated N−1, which is what the window would show.
  - This is twice the motion of real use, so it is a harder test than the actual half-step frame.
- **Stills:** the real in-between frame G(k, k+1) between k and k+1, the skip-one result next to the truth with a 4× error map, and the block field (R/G = 128 + 4·half-step, B = cost).
- **Outputs:** `D:\Recomps\lbp3-recompiled\analysis\fg_warp\stills\` and `analysis\fg_warp\scores.txt`.

## Results, skip-one [measured]

Six sets, 322 scored triples. Times are at 8 threads on the 5900X with the game not running.

| Set | warp (vectors + HUD-less) | warp, colour only | blend | repeat | warp ms avg / max |
|---|---|---|---|---|---|
| `np_walk` (Needlepoint walk) | 27.37 / **23.89** | 27.64 / 24.24 | 21.91 / 17.89 | 19.80 / 15.62 | 10.3 / 12.7 |
| `np_popit` (popit open, moving) | 31.84 / **27.56** | 31.96 / 27.70 | 26.65 / 22.11 | 23.70 / 19.00 | 8.6 / 12.7 |
| `np_load` (map swoop, then load) | 37.57 / 41.66 | 50.21 / 53.89 | 49.44 / 50.88 | 32.63 / 37.23 | 11.6 / 21.7 |
| `bk_a` (Bunkum, with a cut) | 32.96 / **25.97** | 32.44 / 25.43 | 29.41 / 22.19 | 26.21 / 18.83 | 9.3 / 19.8 |
| `bk_b` (Bunkum, subtitles) | 35.32 / **27.64** | 35.07 / 27.37 | 30.45 / 22.58 | 26.64 / 18.65 | 9.0 / 15.6 |
| `bk_c` (Bunkum, slow move) | 38.09 / **33.41** | 38.11 / 33.14 | 29.78 / 22.97 | 26.58 / 19.53 | 8.9 / 11.9 |

Each cell is PSNR in dB, whole frame / moving pixels.

- **Against the trivial generators:** in moving areas, warp beats the 50/50 blend by 3.8–10.4 dB and the repeat by 7.1–13.9 dB in the five play and cutscene sets.
  - Its mean absolute error on moving pixels is 1.9–7.3, against 9.9–21.4 for blend.
- **The game's vectors** help a little in the cutscenes (`bk_a` moving: +0.5 dB, MAE 8.44 → 7.32) and cost ~0.3 dB in the Needlepoint sets. The colour search already finds most of the motion. They mainly guard fast motion and low texture.
- **The HUD-less input** is about neutral on PSNR (+0.1–0.3 in Bunkum, −0.1–0.3 in levels). It is kept on because it keeps subtitles and bars crisp over a moving scene.
- **`np_load`'s averages are skewed by near-static frames.** In the swoop's moving frames (k=1–22), warp scores 20.4–28.1 dB on moving pixels against blend's 14.9–21.9, and the default (vectors + HUD-less) is the best or equal variant. After that, the white-out and loading frames (k=23–41) are near-static. There the HUD-less path scores 37–55 dB where colour-only reproduces them exactly (99), so the set averages read worse. These are all far below visible.

## Stills [looked at]

All are in `analysis\fg_warp\stills\`. The `_mid` files are the real use.

- **`np_walk_k030_warp_mid.png`:** Sackboy on the walk, the jelly river, ropes and the score bubble. Clean, with Sackboy halfway.
- **`np_walk_k020`, the flagpole and ropes:**
  - The first, purely bilateral version erased the pole (seen during development; that image wasn't kept).
  - The measured-candidate version (looked at on `c9aca3f`) keeps it intact, with the knot and the drips at their halfway positions.
- **`np_popit_k030_warp_mid.png`:** the popit open while moving. The "Personal" label, the icons and the "Close Popit" prompt are intact.
- **`bk_b_k030_warp_mid.png`:** subtitles and cinematic bars are crisp, and the banner, gramophone and characters are interpolated.
- **`np_load_k022_warp_mid.png`:** the camera swoop into the level node, where the cyan ring expands past the screen in one frame. The middle frame is mostly white with a few cyan wedges and one blocky edge. That is the worst case seen. It is a one-sixtieth-second frame inside a white-out transition.
- **`bk_a_k045`:** this pair spans the cut at k=46. The generator declines (100% saturated), so the window repeats N−1.

## Cost: the CPU path doesn't fit the default delay

- **Generation time:** typically 8–11 ms at 8 threads (luma ~1, search ~3, compose ~4–5, HUD ~2), with bursts to 20 ms on whole-screen motion such as the swoop. Colour-only is 6–9 ms.
- **It scales poorly with threads:** 14 ms at 4, 9.8 at 8, 8.0 at 16. About 5 ms of it is serial or pool overhead.
- **Lane C's present path** shows G on time only if it is ready within about 4 ms of the flip slot (`D` = one refresh + 4 ms). It drops G if it isn't ready a refresh before N.
  - So in game, with this generator, most G frames would be counted "a refresh late" unless `LBP3_FRAMEGEN_DELAY_MS` is raised to about refresh + 12 ms (≈ 18 ms at 165 Hz). That adds that much display latency to every real frame.
- **The inputs also need a readback** (inferred): 8 MB of vectors plus 8 MB of HUD-less colour per frame, on top of today's final-frame readback. N−1's HUD-less image can be kept from the previous frame.

## Verdict and next steps

The method works and is visibly better than blend: it is clean on the hard cases above, it has one bad frame type (the swoop's white-out), and it declines correctly at a cut. The CPU implementation is a prototype. For the default delay it needs about 2× less time, or a GPU version.

1. **Port compose and search to Vulkan compute** on shadPS4's device, since the vectors, depth and HUD-less image already live there. This removes the extra 16 MB/frame readback and should bring generation to about 1 ms.
2. **Until then, to try it in game:** `LBP3_FRAMEGEN=1 LBP3_FRAMEGEN_MODE=warp LBP3_FRAMEGEN_DELAY_MS=18`, plus lane A's capture of vectors and HUD-less colour into `GenInputs` (not wired yet). Without those inputs it runs colour-only, which scores nearly the same.
3. **Quality follow-ups:**
   - a zoom-aware candidate (scale about the centre) for swoops;
   - depth-ordered occlusion choice (the depth is available but unused);
   - treating the game's vectors as authoritative where they are covered and unsaturated, once the GPU path makes the extra input cheap.
