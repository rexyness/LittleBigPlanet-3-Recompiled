# The first frame of a new scene: where the time goes (2026-11-12)

Measured for [ticket 15](../issues/15-milestone-5-performance.md), following [intro_stalls.md](intro_stalls.md) section B: the first full frame of a new scene took 180–330 ms, with 69–122 ms of command-processor recording and 26–200 ms of GPU, and no compiles. Nothing here changes behaviour. The counters are measurement-only and off unless `LBP3_FRAME_SPLIT=1` or `LBP3_FIRST_FRAME=1` is set.

- **Builds:** branch `perf/first-frame` in the worktrees `D:\Recomps\lbp3-wt\ffs` (runtime) and `D:\Recomps\lbp3-wt\ffs-shadps4` (shadPS4), on top of `port` `2d92d65` / `31071685`. `port` has since gained the geometry-shader fix (`d84eae8` / `5b57642d`); it is not in these builds.
- **Route:** `tools/m5_np_route.sh` with `M5_EXE=` the worktree's exe, from the test copy of the story save. It runs boot → Pod → Prologue map → Needlepoint Peaks walk, on the 165 Hz display.
- **Data:** the clone's `analysis\first_frame\`. For each run: `<tag>_split.csv`, `m5_<tag>_stderr.txt` and `<tag>_out.txt`. `runs.txt` lists every run with its build and the clean-state checks.

## Answer

**Prologue map, first frame (recording 90.7–95.1 ms, GPU Finish 81–86 ms):**
- **The largest counter is image expansions: 144 of them, 45.7–48.7 ms.**
  - The game's three lighting volumes are render-target arrays filled one slice at a time. The texture cache grows each array by one layer per slice.
  - Every growth creates a new image, re-uploads all of it from guest memory, copies the old layers over the upload, and frees the old image.
  - The other image uploads add ~16 ms and a 52 MB buffer sync adds 14.5 ms.
  - Under all of these, the guest-to-staging memcpy is 47.6 ms for 383 MB, and **90% of those bytes are all-zero**.
  - 269 MB of that memcpy is the expansions re-reading the volumes, and 261 MB of those re-reads are layers overwritten straight after.
- **The GPU time is our transfers, not the game's draws.**
  - Timestamped transfers total 80–85 ms: image uploads 35–38, detile 34–35, the expansions' image copies 7–8, buffer copies 2–3.
  - The game's 481 draws are ~6 ms.
  - About 60 ms of the GPU time belongs to the expansions.

**Pod, first frame (two flips of 69–73 + 118–178 ms; `ff1` 50 + 100):**
- **The counter that holds it is the guest-to-staging memcpy: 72 ms of the ~97 ms of recording,** for 264 MB in 243 copies.
  - It sits inside buffer sync (41 ms, 139 MB) and image refresh (35 ms, 229 MB).
  - **82% of the copied bytes are all-zero.**
  - The window has **110,000 page faults**, against ~2 per frame in steady play.
- **The GPU time is again our transfers,** 100–127 ms of them in five of six runs: buffer copies 28–38, image uploads 30–34, detile 49–56. They run while the GPU is still clocked down from the loading screen (P5: ~460 MHz, PCIe gen 2; section C). One run did the same transfers in 33 ms.
- **On this route the flip fires after the first of the frame's 8 buffers, in every run.**
  - The command processor stops for that early Finish (33–36 ms) and the readback, in the middle of the frame.
  - It is intermittent elsewhere: 3 of the 9 intro_stalls runs on `port` have it, the other 6 don't.
  - Same-build runs without it are not faster (section B), so it is a correctness issue with no shown cost.
  - In runs that have it, the old `prep_us`/`record_us` split doesn't apply: `record_us` goes negative.

**Ranked fix candidates** (section D):
1. Upload only the new layers when an image grows, and grow by powers of two. The map; small and certain.
2. Don't upload memory the game never wrote. Mainly the Pod; the largest lever there, with a bigger design risk.
3. Honour only this submit's flip. Correctness; no time gain shown.
4. Then GPU power state, detile throughput and allocation churn.

## A. The Prologue map's first frame

**CPU, recording of the 14 buffers.** Times are in ms. Clean runs (`ffc`, `ff6`, `LBP3_FRAME_SPLIT=1`) are listed first, then `ff1`/`ff2`. Counters nest, as noted in the table.

| Counter | ffc | ff6 | ff1 / ff2 | What it is |
|---|---|---|---|---|
| recording (first hand-over to flip) | 90.7 | 92.1 | 95.1 / 91.1 | |
| **image expansions** (144) | **45.7** | **47.4** | 48.7 / 46.5 | `TextureCache::ExpandImage`, see below |
| image refresh (259, 720 MB) | 38.4 | 38.9 | 40.4 / 39.1 | uploads; 537 MB of it is the expansions' |
| memcpy guest → staging (181, 383 MB) | – | 47.6 | – | inside the refreshes and syncs; 90% all-zero (`ff7`) |
| buffer sync (10, 52 MB) | 14.5 | 14.5 | 14.7 / 14.4 | one 48 MB buffer made resident |
| image create (vmaCreateImage, 240) | 13.5 | 13.9 | 13.8 / 12.9 | 120–126 new device allocations, ~1 GB |
| new images (95–98) | 1.6 | 1.3 | 1.3 / 1.2 | |
| staging requests | 2.2 | 2.3 | 3.1 / 2.3 | 13–19 new blocks, 250–344 MB |
| resource binding | 25.8 | 25.6 | 27.0 / 25.6 | per draw; nests texture refreshes and syncs |
| page faults in the window | – | 86,943 | – | |

The scene's steady recording is 3.6–3.8 ms a frame.

**The expansions.**
- With `LBP3_FIRST_FRAME=1`, `ExpandImage` logs every expansion (`ff3`, `ff4`; the warning stops after 20). All 144 happen in this one frame and never again in the session.
- The three images:

  | Guest address | Size and format | Growth | Then |
  |---|---|---|---|
  | `0x17e422000` | 512×256 R8Unorm | 1 → 64 layers, 63 expansions | 3D, 1 expansion |
  | `0x174930800` | 256×128 R8G8B8A8Unorm | 1 → 64 layers, 63 expansions | 3D, 1 expansion |
  | `0x18993d000` | 64×32 R8G8B8A8Unorm | 1 → 16 layers, 15 expansions | 3D, 1 expansion |

- **Which binding asks:** 141 expansions come from a render-target binding (ResolveOverlap's "size and resources greater" path, slice *k* asking for *k*+1 layers). 3 come from a texture binding, the equal-size 3D path, when the volume is sampled.
- On later frames the 3D image exists, and slices bind as views through the existing LBP3 `slices_of_volume` path. Only the first frame takes the 2D-array route, because the slices come before the 3D view.
- **Where the expansions' 47 ms goes** (summed from the per-expansion lines, `ff3` / `ff4`):
  - create and register the new image: 12.9 / 12.5 ms. Each gets its own device allocation.
  - **re-upload the whole new image: 23.3 / 23.6 ms.**
  - record the copy of the old layers: 3.6 / 3.3 ms.
  - untrack and unregister the old image: 7.6 / 7.5 ms.
- **Bytes:** the re-uploads total 537 MB. 521 MB of that is overwritten by the copy that follows, so only 16 MB is needed. By upload path (`ff3` and `ff4` identical):
  - 80 come through the memcpy: 269 MB, of which 261 MB is old layers.
  - 64 come from GPU-modified memory, a buffer copy with no memcpy: 267 MB, of which 260 MB is old layers.

  So of the map's 383 MB memcpy, 269 MB is the expansions'. The other 114 MB is new images and the buffer sync.
- **Logging cost:** the per-expansion line adds ~15 ms to the frame (recording 109–112 ms in `ff3`/`ff4`). Read times from the `LBP3_FRAME_SPLIT=1` runs.

**GPU** (timestamp pairs, `LBP3_FIRST_FRAME=1`):

| Run | Finish | image uploads | detile | image copies (expansions) | buffer copies | sum |
|---|---|---|---|---|---|---|
| ff3 | 73.7 | 35.6 | 34.7 | 7.0 | 2.4 | 79.6 |
| ff4 | 73.7 | 35.2 | 34.3 | 8.1 | 3.2 | 80.8 |
| ff5 | 66.3 | 38.2 | 35.2 | 8.4 | 3.5 | 85.4 |
| ff1 / ff2 (no image-copy pair) | 83.1 / 82.7 | 35.4 / 36.8 | 34.6 / 31.8 | – | 3.7 / 3.0 | 73.7 / 71.5 |

- **The sum can exceed Finish.** Each command buffer's work goes to the GPU as soon as it is recorded, so part of the GPU work runs during recording.
- The game's own 481 draws cost about what the scene's steady Finish costs: 5.5–6.4 ms two frames later.
- **The expansions' share:**
  - 537 of the 720 MB uploaded and detiled is the expansions', which is ≈52 ms by bytes.
  - The 7–8 ms of image copies is all theirs.
  - Total ≈60 ms of the frame's GPU time.
- **Overhead:** without timestamps (`ffc`), Finish is 81.0 ms against 82.7–83.1 with them, so the pairs cost ~2 ms here.

**The frame after.**
- It destroys 39–95 of the replaced images in 6.0–8.1 ms (`vmaDestroyImage`, 31–48 `vkFreeMemory`).
- Its recording is 11.5–17 ms in five runs. In `ff2` and `ffc` it is 40–43 ms, and no counter holds the difference. That stays unattributed.

## B. The Pod's first frame

**The early flip.**
- The flip interrupt comes after buffer 1 of the flip submit's 8 (`ff_bufs_at_flip` 1 of 8, all runs).
- The interrupt handler then runs Finish (33–36 ms of GPU wait for buffer 1), the display readback and the slot wait. All of that is on the command-processor thread, before buffers 2–8 are recorded.
- The frame is presented with 1 of its 8 buffers executed, and the flip event reaches the game before the frame is drawn.
- In shadPS4, a `PatchedFlip` NOP in **any** buffer raises the flip interrupt (`liverpool.cpp:454`).
- Hypothesis, not checked: buffer 1 is command memory the game reuses from the loading screen's one-buffer flips (frames 62–68), and still holds the NOP that `PatchFlipRequest` patched then.
- The real CP doesn't flip on NOPs, so on the console a stale patched NOP is harmless.
- **How often:**
  - Every Pod frame on this route: the ten `first_frame` runs and all twelve earlier `m5_*_split.csv` runs, from the story save.
  - 3 of 9 intro_stalls runs on `port` `2d92d65`: `is_f3`, `is_s1` and `is_s2` have it; `is_f1`, `is_f2`, `is_h1`, `is_w1`, `is_w2` and `is_w4` don't. The save is the Thruster baseline either way, so it isn't the save alone.
  - Without it, the Pod frame is one 180–195 ms frame: prep 69–86, a positive `record_us` of 21–23 (the 8th buffer), Finish 81–89. That is what intro_stalls.md reported.
- **Cost:** none shown. On the same build, the Pod's first two flips together took:
  - without the early flip: 226–262 ms (`is_f1` 235, `is_f2` 230, `is_h1` 262, `is_w1` 226, `is_w2` 245, `is_w4` 233);
  - with it: 183–233 ms (`is_f3` 218, `is_s1` 233, `is_s2` 183).

  The next frame's Finish already waits for the transfers still in flight, so serialising buffer 1's GPU work doesn't lengthen the total.
- The window columns now end at the later of Flipped and Released, so the whole frame is counted:

| | ff6 (clean) | ff5 | ff3 / ff4 |
|---|---|---|---|
| window (first hand-over → release) | 132.1 | 132.2 | 136.8 / 137.7 |
| of which the early Finish + readback | 34.4 + 0.7 | 33.0 + 0.6 | 34.7 + 0.7 / 35.8 + 0.9 |
| hand-overs of buffers 1–8 | 110.7 | 111.1 | 114.7 / 116.5 |
| **memcpy guest → staging** (243 copies, 264 MB) | **72.2** | **73.1** | – |
| buffer sync (49–50, 139 MB) | 41.3 | 41.4 | 41.8 / 40.0 |
| image refresh (185, 229 MB) | 35.0 | 36.3 | 38.0 / 36.3 |
| sparse residency (37, 139 MB) | 6.0 | 6.2 | 6.1 / 6.6 |
| new images (184), of which create | 3.5, 3.1 | 3.6, 3.2 | 4.0 / 3.8 |
| staging requests (12 new blocks, 288 MB) | 2.0 | 2.1 | 2.1 / 2.0 |
| resource binding (nests most of the above) | 55.9 | 55.2 | 57.2 / 54.1 |
| page faults in the window | 109,961 | 109,830 | – |

- **Recording is about 97 ms,** of which the memcpy is 72 ms.
- **82% of the copied bytes are all-zero** (`ff7`: 215 of 264 MB), in 4 KB chunks of the source.
- **The copy is slow.** It runs at 3.7 GB/s, against 8 GB/s for the map's copy and Needlepoint's first frame (110 MB in 13.6–14.9 ms, 3.6–3.9 thousand faults, 14% zero).
- **The faults** fit first reads of untouched guest pages plus first writes into new staging blocks, counted in distinct pages:
  - **Pod:** 215 MB zero ≈ 55,000 pages, plus 288 MB of new staging ≈ 72,000. 110,000 faults were seen; some zero chunks are unmapped tails, which don't fault, or pages already touched.
  - **Map:** 264 MB of new staging ≈ 68,000 pages; the volumes it re-reads are only ~16 MB (4,000 pages). Its 87,000 faults are mostly staging writes, and its memcpy still runs at 8 GB/s.
  - **Needlepoint's first frame,** with no new staging: 15 MB zero ≈ 3,800 pages, against 3,600–3,900 faults.
- **The likely reason for the Pod's slow copy** is its ~55,000 first reads of never-touched guest memory (a demand-zero fault each), not the staging writes. This fits the numbers but is not proven.
- The all-zero count is in 4 KB chunks of the source. It includes unmapped tail bytes, which are zero-filled.

**GPU** (timestamps for the Pod frame plus the next frame, where the rest are collected):

| Run | periods | Finishes | buffer copies | image uploads | detile | sum |
|---|---|---|---|---|---|---|
| ff2 | 68.5 + 177.6 | 32.5 + 84.4 | 33.1 | 31.3 | 62.4 | 126.8 |
| ff3 | 71.2 + 145.4 | 34.7 + 44.1 | 28.1 | 29.7 | 49.2 | 107.0 |
| ff4 | 71.8 + 165.1 | 35.8 + 68.4 | 37.8 | 33.8 | 50.7 | 122.3 |
| ff5 | 69.0 + 170.7 | 33.0 + 73.1 | 35.5 | 33.4 | 55.7 | 124.7 |
| ff1 | 50.0 + 100.0 | 7.0 + 5.1 | 7.4 | 10.1 | 15.2 | 32.7 |

- The game draws only 182–208 times in these frames, so the GPU time is ours.
- The 229 MB of the Pod's textures take 49–56 ms of detile: ≈4.3 GB/s, against ≈21 GB/s for the map's 720 MB.
- `ff1` did the same bytes 3–4× faster, so the GPU's state at that moment matters (section C). `ff8` (P5 during the frame) had 100.6 ms of timestamped transfers. Its periods are inflated by the zero scan, like `ff7`'s.

## C. GPU power state

- **Method:** `ff8` logged `nvidia-smi` (P-state, clocks, PCIe link generation) every 25 ms. The driver refreshes these values only about every 250 ms. Rows were lined up with the frame split through a QPC ↔ wall-clock pair taken just before the run (`ff8_clockmap.txt`, `ff8_nvsmi.csv`).
- **The bursts arrive while the GPU is clocked down.** Light steady load before each scene leaves it in a low state, and it reaches P0 only after the burst:

  | First frame | GPU state during the frame | P0 reached | GPU timestamps (2 frames) |
  |---|---|---|---|
  | Pod | **P5: 450–480 MHz graphics, 810 MHz memory, PCIe gen 2** | ~370 ms after it began | 100.6 ms |
  | Prologue map | P3: 1005–1035 MHz, memory 5001 MHz, gen 4 | ~390 ms after | 88.6 ms |
  | Needlepoint | P3: 990–1020 MHz | | 33.1 ms |

  Idle, before the game: P8, 210 MHz, gen 1. At full load: P0, 1695–1935 MHz, memory 9751 MHz, gen 4.
- **This is consistent with the Pod's GPU spread.** `ff1` did the same transfers in 33 ms against 100–127 ms in the other runs, which fits that run catching the GPU in a higher state; `ff1` has no clock log. The upload and buffer-copy rates at the Pod (~5–8 GB/s) also sit near a gen 2 link's limit.
- **It isn't ours to fix in code.** Less data (fixes 1 and 2) is the real cure. A per-application "prefer maximum performance" driver setting would show how much of the Pod's GPU time is clocks, as a test, not a fix.

## D. Fix candidates, ranked

Gains are given alone and as an increment after the other fix, because fixes 1 and 2 share bytes at the map.

1. **`ExpandImage` re-uploads the whole grown image for every slice** (the map; once a session).
   - **Evidence:** 141 one-layer growths of three lighting volumes. Of the 537 MB they re-upload, 521 MB is overwritten by the copy straight after. The expansions cost ~47 ms CPU and ~60 ms GPU (section A).
   - **Fix, in `texture_cache.cpp` `ExpandImage`:**
     - (a) Upload only the new layers (`RefreshImage` over a layer range), or skip the refresh when the old image's copy covers it. This removes the expansions' 23.5 ms of refresh CPU, including 261 MB of the 383 MB memcpy, and ~50 ms of GPU upload + detile (521 of 720 MB).
     - (b) Grow layer arrays by powers of two: 6 growths a volume instead of 63. This also removes most of the 12.5 ms of image creation, the 7.5 ms of untracking, the 7–8 ms of GPU image copies, the next frame's 6–8 ms of destroys and ~120 device allocations.
   - **Gain at the map:** ~23 ms CPU and ~50 ms GPU from (a); ~43 ms CPU and ~58 ms GPU from (a) and (b) together. That is out of a 91–95 ms recording and an 80–85 ms GPU burst.
   - **Risk:** small, local change. (b) makes an image bigger than the guest asked for, so it can overlap neighbouring guest memory; these volumes are 64 and 16 slices, so powers of two land exactly. Regress the Prologue map's lighting and Needlepoint.
2. **Don't upload memory the game never wrote** (mainly the Pod).
   - **Evidence:** 82% of the Pod's first-frame memcpy is all-zero (215 of 264 MB), and its likely cost is ~55,000 demand-zero faults. At the map, 90% is zero (343 of 383 MB), but 261 MB of that is the expansions' redundant re-reads. Needlepoint's first frame, real textures, is 14% zero.
   - **Gain at the Pod:** most of the 72 ms memcpy (82% of the bytes, and the slow, faulting part), plus the zero share of the 100–127 ms of GPU transfers. If proportional to bytes, that is ~80–100 ms of GPU time, which then runs as clears and fills instead.
   - **Gain at the map, alone:** ~43 ms of memcpy, but not the 64 GPU-modified expansions' 267 MB, which the GPU wrote. **After fix 1:** only ~74–82 MB of zero memcpy is left (~10 ms CPU, ~7 ms GPU).
   - **How:** keep a "never written" bit per guest page. Set it when the HLE maps fresh memory, and clear it on the first CPU write, which the page tracker already catches. Turn uploads of such ranges into a GPU fill or image clear: no memcpy, no detile.
   - **The bit has to come from tracking.** Scanning for zeros reads the pages, and the read is what faults.
   - **Risk:** every write path into guest memory must clear the bit (file reads into guest buffers, HLE memcpys, DMA). Otherwise a texture uploads as black. Regress the Pod, the Prologue map, Needlepoint and milestone 1.
   - **Cheaper partial:** detect zero 4 KB chunks during the existing copy and clear on the GPU instead of upload + detile. This saves the GPU side and the staging writes, not the source faults.
3. **The Pod's flip fires after buffer 1 of 8** (every run on this route, 3 of 9 elsewhere). Correctness; no time gain shown.
   - **Evidence:** the frame is presented, and the flip event sent to the game, before 7 of its 8 buffers ran. Same-build runs without the early flip are not faster (section B).
   - **Fix:** confirm which NOP raises the interrupt (log the NOP's address and the buffer index). Then honour only the flip that this submit's `PatchFlipRequest` patched; for example, remember its address, or restore the placeholder after the interrupt.
4. **Smaller:**
   - **GPU power state** (section C): the Pod's burst runs at P5 (≈¼ graphics clock, PCIe gen 2), and the map's at P3. Fixes 1 and 2 shrink what has to run at those clocks. Testing with the NVIDIA "prefer maximum performance" setting for `lbp3.exe` would size the effect; that setting is the human's choice, not a code change.
   - **Detile throughput:** the Pod's textures detile at ≈4.3 GB/s against ≈21 GB/s at the map, depending on tile mode and clocks. Most of it goes with fix 2.
   - **The frame after the map:** in 2 of 6 runs it recorded 40–43 ms that no counter holds. Still unattributed.

## E. Also seen

- **Steady-state re-uploads in Needlepoint Peaks.**
  - Every frame re-uploads 26 images, 106 MB, from GPU-modified memory: 0.15 ms CPU, but **3.6 ms of GPU upload + 0.5 ms detile per frame** (`ff1`, `ff3`, `ff4`, 1,634–1,649 level frames each). Finish there is 5.5 ms.
  - That is 4.1 ms of timestamped transfers against a 5.5 ms Finish (draws aren't timestamped). It is worth its own look for the walk's work: which images, and whether the buffer → image copy each frame is needed.
- **Instrumentation cost.**
  - Walk game work across the ten runs is 22.52–23.34 ms (`ffoff`, counters compiled in but off: 22.84). `port`'s own runs were 22.15–22.54. There is no measurable cost.
  - The zero scan (`ff7`) and the per-expansion line inflate the map's recording. Use `ff6`/`ffc` for times.
- **The CSV now has whole-frame window columns.** The `[first-frame]` line also fires for an early-flip frame whose window exceeds 50 ms. `tools/frame_split.py --ff 50` prints the new groups and flags early flips.

## Instrumentation (branch `perf/first-frame`, local, not pushed)

| Repo | Commit | What |
|---|---|---|
| shadPS4 | `e8b9c168` | `video_core/lbp3_perf`: CPU scopes (buffer sync, staging, residency, image create/refresh/detile/expand, binding), VMA allocation callback, GPU timestamp pairs (buffer copies, image uploads, detile) |
| shadPS4 | `6bc4898f` | image destroys, VMA frees, image-copy timestamp pair; per-expansion `[first-frame] expand` line (`LBP3_FIRST_FRAME`) |
| shadPS4 | `8c68ea23` | memcpy (`CopySparseMemory`) scope in buffer sync and image upload |
| shadPS4 | `84a41422` | ZeroCopy kind |
| runtime | `361ee6d` | `ff_*` columns over the whole frame, `[first-frame]` line, GPU collection after Finish |
| runtime | `edf7a38` | window to the later of Flipped/Released, `ff_bufs_at_flip`, new columns |
| runtime | `df0e47b` | memcpy columns, process page faults per window |
| runtime | `c758aab` | all-zero 4 KB chunks of each upload (`ff_memcpy_zero_kb`, `LBP3_FIRST_FRAME`) |

| Runs | Build (runtime / shadPS4) | Environment |
|---|---|---|
| ff1, ff2 / ffc / ffoff | `361ee6d` / `e8b9c168` | `LBP3_FIRST_FRAME=1` / `LBP3_FRAME_SPLIT=1` / none |
| ff3, ff4 | `edf7a38` / `6bc4898f` | `LBP3_FIRST_FRAME=1` |
| ff5 / ff6 | `df0e47b` / `8c68ea23` | `LBP3_FIRST_FRAME=1` / `LBP3_FRAME_SPLIT=1` |
| ff7, ff8 | `c758aab` / `84a41422` | `LBP3_FIRST_FRAME=1` (ff8 also logs `nvidia-smi` every 25 ms) |
