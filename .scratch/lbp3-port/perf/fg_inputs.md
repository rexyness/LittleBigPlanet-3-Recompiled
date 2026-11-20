# Frame generation: the inputs, measured (2026-11-12)

Lane A of the frame-generation prototype (the human: "give it a try with frame gen"). This lane
built a capture tool, took six dump sets on current `port` (`0570df8`, shadPS4 `d5af2ae7`,
GS stages on), and measured from them what a frame generator can use and where to take it.
Nothing in `port` changed. Labels: **[measured]** from the dumps, traces or logs; **[inferred]**.

It corrects one point of `fps60_render_re.md` (a Create-mode trace from the 11-10 build): **in
a level and in cutscenes the velocity target is cleared *before* the final composite**, so it
cannot be read at the composite. Its vectors have to be copied just before that clear.

## The tool

`LBP3_FG_DUMP=<n>`, branch `exp/fg-capture`: runtime `fgcap` worktree
(`D:\Recomps\lbp3-wt\fgcap`), shadPS4 `D:\Recomps\lbp3-wt\fgcap-shadps4`. Both branches are pushed
to the forks; `port` is untouched.

- It is armed by the environment variable. The remote command `fgdump <set>` traces the next
  frame's draws (`[draw]` lines), then captures n frames into `analysis\fg_dump\<set>\`.
- **Copies only.** Every image is copied GPU-side (`copyImageToBuffer`) into the tool's own
  host-visible buffers, with the same barriers the existing F9 dump uses. Nothing is written into
  the game's images, and no texture-cache flags are touched. The copies complete with the flip's
  own `Finish`, and a writer thread saves them after the last frame.
- Two null-by-default hooks in shadPS4's rasterizer: one after each executed draw (VS/PS hashes,
  index count), one at each masked fill it inspects (destination, size, value, mask).
- Without a capture, a `[fg] 30 flips:` line reports what the hooks found: the composite's pixel
  shader, display draws per flip, the velocity fill and its address, and the depth chosen.
- **Per frame:**
  - `fNNN_hudless.raw`: the display image right after the composite; RGBA8 sRGB, 1920×1080.
  - `fNNN_final.raw`: the flipped image, the readback's own bytes.
  - `fNNN_velocity.raw`: RGBA8 UNORM, copied just before its clear.
  - `fNNN_depth.raw`: float32, the depth aspect of the D32S8.
  - `fNNN_view.bin`: the main view's first 0x200 bytes at submit. The current camera matrix is at
    +0x20 and the previous one at +0x160 (`DAT_013a2a60`, see `ClearMotionBlur__`).
- **`manifest.json`, per frame:**
  - the sim and game counters at submit, and QPC times at submit, IRQ and shown;
  - draw counts and the pixel shaders of the display-buffer draws;
  - the composite's shaders, and the velocity and depth addresses;
  - the top depth candidates;
  - when the velocity and depth copies were taken: `fill`, `composite` or `flip`.
- **Scripts** (`D:\Recomps\lbp3-wt\_tools\`):
  - `fg_capture.sh` runs the routes from the story test save via `saves.sh`;
  - `fg_png.py` writes PNGs;
  - `fg_analyse.py` runs the encoding test, the HUD mask and the per-flip table;
  - `fg_cuts.py` scans for cuts;
  - `fg_passes.py` groups a trace into passes.

## Dump sets

All are in `D:\Recomps\lbp3-recompiled\analysis\fg_dump\`, 60 consecutive flips each. They are
about 1.9 GB each, not in git.

| Set | What | Notes |
|---|---|---|
| `np_walk` | Needlepoint Peaks, running right with a jump | All 59 steps = 1. Camera pan up to ~34 px/frame. A hint banner (screen HUD) appears from k=40 |
| `np_popit` | Running left, then the popit opens; the stick is held while it is open | The popit is in-world UI. Steps 1, the last one 2 |
| `np_load` | The Prologue map: Play pressed → the camera swoops into the level node → the loading screen | Vectors saturate (k=20–26). From k=43 the loading screen: no composite, 0 steps |
| `bk_a` | The Legend of Bunkum cutscene, first shots | **Camera cut at k=46.** A still camera at k=23–33, a 0-step flip at k=29, 2-step flips at k=25 and 59 |
| `bk_b` | Bunkum, "Welcome to Bunkum" | Subtitles and cinematic bars after the composite. A 2-step flip at k=20 |
| `bk_c` | Bunkum, a slow camera move | Subtitles |

The logs are `fg_np_stderr.txt` and `fg_bunkum_stderr.txt`, each with the traced frame before
every set. The pass summaries are `walk_passes.txt` and `bk_b_trace.txt`. PNGs, warp-error maps,
HUD masks and the cut contact sheet are in `png\`.

## The hook points (runtime spec)

The frame here is the draws between two flip interrupts (all on the GPU thread). In-level order
[measured, Needlepoint trace; the Bunkum trace has the same tail]:

1. Velocity pass, `rt0 17a818000`, its own depth `178938000`: vs `ed24e101`, ps `a9a72c3f`,
   about 150–160 draws.
2. Main colour pass into the scene buffer `171fe8000`, depth `171478000`.
3. DoF and motion blur: ps `c2e07acc` ×3, `8d6de026`, sampling `17a818000`.
4. SMAA.
5. Post into `17a018000`.
6. Bloom.
7. Masked fills (cs `5c5e1be6`): `171fe8000`←0, `17a018000`←0, `17b018000`←0,
   `17b818000`←00ffffff, and **`17a818000`←007f7f00, the velocity clear**.
8. Two full-screen draws on the velocity target (ps `9f99bb04`). They leave its colour cleared:
   the tool's first version copied it after them and found every texel `00 7f 7f 00`.
9. Display buffer:
   - a draw with no pixel shader;
   - **the composite** (ps `5ed16706`, n=4, sampling `17b018000`, `171fe8000` and the 480×270
     bloom);
   - then the letterbox bars (`3c4e8791`, n=8);
   - then the screen HUD: hint banners (`b2ff073c`, `3c91dce7`…), subtitles and the cinematic
     bars.

**The rules the tool uses. They held on every captured in-level and cutscene frame (≈340
flips) and on every `[fg] 30 flips` line of both runs [measured]:** in the Bunkum run, 110 of
119 lines show 30 of 30 composites with the velocity fill at `17a818000` and depth `171478000`.
The rest are boot, Pod or loading windows, or transition windows with fewer.

- **Display buffers:** the addresses given to `sceVideoOutRegisterBuffers`. There are two, used
  alternately (`160800000`, `160ff8000`).
- **HUD-less colour:** the display image right after **the first draw in the frame into a
  display buffer that has a pixel shader**.
  - That is the composite. Its shader varies: `5ed16706` in levels and cutscenes,
    `3a961ff2`/`7af25469` in the Pod and menus, `4d3d6de4` on some in-level frames. So match it
    by role, not by hash.
  - The draw before it has no pixel shader and changes nothing visible. In the first version of
    the tool, "first display draw" captured the buffer's previous contents.
- **Velocity:** the target the masked fill clears with value **`007f7f00`** over ≥1920×1080×4
  bytes. Copy it **in the fill hook, before the clear**.
  - Its vectors are the frame's own: the velocity pass and the motion blur that reads them run
    earlier in the same frame.
  - At the composite it is already cleared (every texel `00 7f 7f 00`), as the tool's first
    version saw [measured].
- **Depth:** the 1920×1080 depth with the most depth-tested draws in the frame, not counting draws
  into the velocity target: always `171478000` in levels and cutscenes [measured]. Copy it at the
  same point as the velocity.
- **No velocity in the Pod and menus:** no `007f7f00` fill there [measured, `[fg]` lines], so they
  would need optical flow alone or no generation.
- **Cost:** one function call per draw (a few register reads and an address compare) and one per
  masked fill [inferred, not timed]. The camera block costs 64 bytes read at submit.
- **Open:**
  - The fills of `171fe8000` and `17b018000` are processed before the composite that samples them,
    yet the composite's output is the correct scene [measured].
  - So the images the fill clears and the ones the composite samples are presumably different
    texture-cache images at the same address (the rt-fills fix clears images inside the fill
    range) [inferred]. Not investigated, but a generator that reads those intermediate images
    rather than the display buffer would have to care.

## The vector encoding

**Verdict [measured]:** the pixel at **p** in frame k was at **p − m** in frame k−1, with

- m.x = (B − 127) × 0.941 px (= (B/255 − 0.498) × 240)
- m.y = (G − 127) × 0.529 px (= (G/255 − 0.498) × 135), image y down
- R = coverage (255 = has a vector, 0 = none); A = 255 where written.

The velocity is from frame k itself, not k−1. That matches `fps60_render_re.md`'s
G = 0.498 − 4·Δy_ndc, B = 0.498 + 4·Δx_ndc with Δ = current − old and NDC y up.

**The reprojection test** (`fg_analyse.py encoding np_walk 5 15 25 35`): warp the HUD-less
frame k−1 by frame k's vectors and compare with frame k. The figure is the mean absolute luma
error over covered pixels, over 4 pairs:

| Variant | Error |
|---|---|
| **sx+1 sy+1 ×1.0 (the verdict)** | **3.44** |
| the previous frame's vectors, same decode | 5.22 |
| best global translation (search ±40 px) | 7.43 |
| ×0.5 | 8.65 |
| x/y swapped | 8.99 |
| y negated | 10.08 |
| no warp | 11.57 |
| any x negation or ×2 | 11.7–15.9 |

That is −70% against no warp and −54% against the best global pan. Scale sweeps on other pairs
peak at ×1.0 too:

| Pair | Error at ×0.75 / ×1.0 / ×1.25 |
|---|---|
| `bk_b` 19 | 3.44 / 2.44 / 3.40 |
| `np_walk` 20 | 6.40 / 3.19 / 6.48 |

The Bunkum sets average 1.8–2.7 warped against 3.7–5.2 unwarped (`cuts_bunkum.txt`).

**Precision and range [measured]:**

- One code step is 0.94 px horizontally and 0.53 px vertically.
- The vectors clamp at ±120 px (x) and ±67 px (y) per frame. In walking play the p99 was within
  range.
- **The map's camera swoop saturates:** 33% of covered texels at k=20, 70% at k=22, 93% at k=24.
  A generator must treat saturated vectors as unknown.

**The residual after warping** (`png\warperr_*`) sits on:

- silhouette edges (disocclusion, and the game's own motion blur baked into the colour);
- the jam jar's sparkles;
- the score bubble's changing number;
- the letterbox edge rows.

## What carries no vectors

- **The screen HUD:** hint banners, subtitles, the cinematic letterbox bars and the thin top/bottom
  bars. They are drawn after the composite, so they are absent from the HUD-less image
  [measured].
  - **HUD mask** (final − HUD-less, >8/255):
    - in a level, 1.7% (the bars) plus up to 3% for a hint banner;
    - in cutscenes, 6–31% (the large bars and subtitles).
  - The cutscene bars are static [measured, `fg_analyse.py hud`; masks in `png\hudmask_*`].
- **In-world UI:** the popit, the score bubble, the map's "Loading..." text. It is drawn into the
  scene before the composite, so it **is in the HUD-less image but has no vectors of its own**: it
  carries the vectors of whatever is behind it.
  - While the popit opened, the warp error in its box was 9.7–13.4, against 3.6–4.0 elsewhere
    (2.5–3.8×) [measured, `np_popit` k=6–8].
  - With the camera panning ~17 px and the popit opening, the error in its box was 5.1–5.8
    against 3.5–4.2 [measured, k=2–4].
- **The Prologue map's floor planes** (the gingham and wicker): 50–57% of the screen had R=0
  during the swoop [measured, `np_load`, k≤20].
- **Particles** (Bunkum confetti): no silhouettes in the velocity image [inferred from one frame,
  `bk_b` k=30].
- **What is beyond the backdrop**, in a cutscene's top corner: R=0 [measured, `bk_b`].
- **Fluid:** not passed by these routes; unknown.
- In Needlepoint, coverage is 99.7–100% [measured]. Characters, ropes, pennants, rotating signs
  and the backdrop all have per-object vectors.

## Steps, cuts and loads

- **1-step flips:** the normal case. Every walk flip was 1 step [measured].
- **0-step flips:** the game re-renders the same state. The image changes by 0.44 luma (vs 1.4–1.6
  at the neighbouring 1-step flips), and the vectors are near zero (mean |code| 0.06/0.15)
  [measured, `bk_a` k=29].
- **2-step flips:** the vectors cover the whole displacement since the last *rendered* frame, not
  one step. Scale ×1.0 is still the best fit:
  - `bk_b` k=20: 3.81 at ×1.0 vs 6.90 at ×0.5 and 10.07 at ×2;
  - `bk_a` k=59: best at ×0.75 (4.82, vs 5.45 at ×1.0 and 12.38 at ×2), as at its 1-step
    neighbour k=58 (3.96 at ×0.75, 4.12 at ×1.0).

  The camera moved 144 units against 84/60 at the neighbours [measured]. So the per-vertex
  "old" positions are per render, and a generator needs no step-aware scaling [inferred from
  that].
- **Camera cut** (`bk_a` k=46, `png\bk_a_cut_44_49.png`):
  - the camera translation jumped 5,753 units (vs ≤250 per frame otherwise);
  - **100% of covered vectors were saturated**;
  - warping makes it worse (66 vs 48 unwarped);
  - the game's own motion blur smears that frame.
  - `ClearMotionBlur` shows only on the **next** frame: the previous camera equals the current at
    k=47 [measured].
  - So at the cut frame itself, the flag is not yet set; the jump and the saturation are.
- **Cheap cut detectors, in order:**
  1. The camera matrix's translation jump at submit, from 64 bytes of `DAT_013a2a60+0x20`, against
     the last frame's [measured: 5,753 vs ≤250].
  2. The share of saturated vectors over 50% [measured: 100% at the cut; 0–3% in normal play;
     33–93% in the map swoop, which is not a cut but where vectors are also useless].
  3. The `ClearMotionBlur` sign, previous camera == current, one frame late [measured].

  It also fires on a still camera, so it needs the jump test too.
- **Level load:** `np_load` k=43–59. No composite and no display draws in those flips (1 draw).
  The sim counter jumps (−220,928 at the load, then 0 steps), and the camera shows
  `ClearMotionBlur` [measured]. "No composite this frame" is a clean pass-through signal.
- **Depth convention:** conventional Z, not reversed. Scene depth is 0.995–0.9999 and the far
  plane is 1.0. Letterbox and cinematic-bar pixels are 0.0 (1.5% in levels, 34% in a cutscene)
  [measured]. Near and far values in world units are not derived yet; the projection is not in
  the first 0x200 bytes of the view block [not checked].

## What this means for the generator (lane B)

These points are inferred from the facts above.

- **Inputs:** HUD-less colour after the composite; vectors and depth copied at the `007f7f00`
  fill.
- **Overlay:** take the screen HUD from the real frame, where final differs from HUD-less, rather
  than interpolating it.
- **Missing vectors:** R=0 and saturated texels count as missing.
- **Skip generation** (repeat the real frame instead) when:
  - the frame has no composite;
  - there is no velocity fill (the Pod, menus);
  - the camera jumped;
  - most vectors are saturated.
- **Quality risks:** the popit and score bubbles (vectors from behind them) and the map's floor
  planes (no vectors).

## Not done

- No fluid, Create mode or Pod capture.
- Camera near/far and FOV are not decoded.
- The cost of the hooks and copies was not timed.
- The odd ordering of fills before the composite (above) is not explained.
