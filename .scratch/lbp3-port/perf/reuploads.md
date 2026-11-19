# The per-frame image re-uploads in Needlepoint Peaks: what writes them (2026-11-12)

Follows [first_frame.md](first_frame.md) section E. This is read-only work: code reading plus the existing data, with no new runs.

**Update, 2026-11-12 morning:** measured and fixed (F1 + F2, with depth, volumes and sRGB). See [Outcome](#outcome-2026-11-12-fixrt-fills) at the end.

- **Data:** `analysis\first_frame\*_split.csv`, and the trace of an earlier build, `analysis\t07_run1_stderr.txt` (Nov 10).
- **Code:** line numbers are for the port tree, `third_party\shadPS4` at `5b57642d`. Its texture and buffer cache logic is the same as `perf/first-frame` (both sit on `31071685`), so only the line numbers differ.

## Answer

**These images are not CPU-modified.** All ~26 refreshes per frame re-upload a whole image from the buffer cache's GPU-written copy of its memory.
- The counters prove that source, and that no DMA copy is involved.
- That the dirty flag is `GpuDirty` follows by elimination; no log shows the flag itself.

- **What writes them: the game, on the GPU.**
  - Its compute "masked fill" `cs 5c5e1be6` clears render targets by writing their memory as a raw buffer. The traced frame has 9 of these dispatches.
  - For each fill, the texture cache flags every cached image that starts at the fill's address or lies inside its range (`vk_rasterizer.cpp:916-917` → `texture_cache.cpp:189-200`).
  - The image's next bind then detiles the whole image and uploads it from the arena (`texture_cache.cpp:786-857`).
- **Which candidate this is:** the first one, "the game really writes them each frame", but as GPU clears, not CPU or streaming writes.
  - 8 of the 9 traced fills overwrite with a constant (mask `ffffffff`).
  - Detile + buffer→image copy is the costly way of delivering that constant. An image clear gives the same result.
- **It isn't specific to Needlepoint:**

  | Scene | Refreshes a frame | Bytes |
  |---|---|---|
  | Pod | 7 | 57,152 KB |
  | Prologue map | 26 | 107,328 KB |
  | Needlepoint | 26 | 107,328 KB |
  | Loading screen | 1 | 8,160 KB |

  The map and Needlepoint upload exactly the same bytes, so the set is the level renderer's fixed render targets, not level content.
- **The measured cost is a lower bound:** 4.3 ms of timestamped GPU a frame in Needlepoint. On top of that, an untimed image→buffer copy (`TileImage`) runs before about 5–7 of the fills each frame.

**Ranked:**
1. **Real GPU writes.** The game's fills clear its render targets each frame, and the texture cache re-uploads each target whole. This fits all the counters, and the byte total matches to the KB (the last term of the fit is guessed).
2. **A hidden extra copy before each fill.** An LBP3 change in buffer sync (`buffer_cache.cpp:354-362`) copies the target into the buffer before the fill, even when the fill's mask throws the old contents away.
3. **Ruled out by the counters:**
   - CPU writes that share pages with the images;
   - CPU-written or streamed textures;
   - CP DMA copies;
   - render-target readbacks (off by default).

## A. Evidence

### Measured in Needlepoint

Frames ≥ 1700 with 26 refreshes, runs `ff1`–`ff5`, `ff7`, `ff8`. Pod frames 300–1000 are given for comparison.

| | Needlepoint | Pod |
|---|---|---|
| refreshes, KB | 26, 107,328 (18 / 27 / 28 in a few frames) | 7, 57,152 (8 / 9 in a few) |
| all detiled (tiled images) | 26 | 7 |
| GPU upload + detile | 3.66–3.78 + 0.59–0.63 = **4.3 ms** | 2.31–2.44 + 0.40–0.46 = **2.8 ms** |
| CPU, refresh scope (nests the detile) | 0.12–0.15 ms | 0.05 ms |
| memcpy guest → staging | 6 copies, = buffer-sync KB (~1.5 MB) | = buffer-sync KB |
| GPU buffer copies vs buffer syncs | equal (4 / 5 / 6 / 7 per frame, same distribution) | equal |
| staging requests: total − syncs − detiles | 37 − 4 − 26 = **7** | 23 − 11 − 7 = **5** (map: 35 − 4 − 26 = 5) |
| dispatches per frame | 13 (47 in ~291 frames, with 28 refreshes) | 12 |

What the counters show:

- **No refresh copies from guest memory.** All memcpy bytes belong to buffer sync.
  - So `ObtainBufferForImage` (`buffer_cache.cpp:190`) always takes its "region GPU-modified" branch.
  - CPU-dirty images in untouched memory would show as about 106 MB of memcpy a frame. There is none.
- **There are no CP DMA copies.** Every GPU buffer copy is a buffer sync, so `Rasterizer::CopyBuffer` (`vk_rasterizer.cpp:1307`, which flags images at `:1316`) never runs on the GPU path.
- **That leaves one source of `GpuDirty`:** a shader binding a buffer it writes to (`vk_rasterizer.cpp:904-917`).
- **Something else requests staging 5–7 times a frame.**
  - The candidate that runs every frame is `TileImage` (`tile_manager.cpp:280`), called from the buffer-sync step that copies an image into the buffer before a fill (`buffer_cache.cpp:354-362` → `:413`).
  - It has no counter and no GPU timestamp pair.
- **`ff_page_faults` (0) is not evidence either way.** intro_stalls.md sees write-tracking faults on `main` in every walking window, while this column reads 0 in steady frames. So `PageFaultCount` most likely doesn't count protection faults.

### Inferred: which images and which fills

**Caveat:** this comes from another scene on the Nov 10 build. It is not measured in Needlepoint.

**The fills.** `t07_run1_stderr.txt` has one fully traced frame (`LBP3_TRACE_DRAWS` plus the target dump, lines 1597–2751). It has 412 draws and 10 dispatches, and 9 of the dispatches are `cs 5c5e1be6`:

| # | Destination, size | Mask | Value | Covers |
|---|---|---|---|---|
| 3 | `180940000` + 64 MB | ffffffff | 0 | the pool at 0x180940000 (below) |
| 124, 374 | `180940000` + 8,160 KB | ffffffff | 0 | 1920×1080 RGBA8 at the pool base |
| 376 | `181a38000` + 8,160 KB | ffffffff | 0 | 1920×1080 RGBA8 |
| 385 | `171fe8000` + 8,160 KB | **ff000000** | 0 | the scene buffer's alpha (the "Dark Pod" fill, NOTES.md:427) |
| 386 | `17a018000` + 2,304 KB | ffffffff | 0 | 960×540 RGBA8. Also flags the 1920×1080 that shares its base |
| 387 | `17b018000` + 8,160 KB | ffffffff | 0 | 1920×1080 |
| 388 | `17b818000` + 8,160 KB | ffffffff | `00ffffff` | 1920×1080 |
| 389 | `17a818000` + 8,160 KB | ffffffff | `007f7f00` | 1920×1080 RGBA8 |

**What the shader does** (`analysis\shaders\dumps\cs_0x5c5e1be6e0ada38c.*irprogram.txt`):

- For `i < min(cb[0], dst.num_records)`: `dst[i] = (dst[i] & ~mask[i & cb[1]]) | (value[i & cb[1]] & mask[i & cb[1]])`.
- The inputs come from user SGPRs:
  - SGPR2–3: pointer to the constant buffer;
  - SGPR4–7: value;
  - SGPR8–11: mask;
  - SGPR12–15: destination.
- `cb[1]` is 0 in all nine fills, so each fill writes one 32-bit pattern. That makes it independent of position, and therefore of tiling.
- `cb[0]·4` equals the destination buffer's size in every fill, so the flagged range is not wider than what is written.

**The 64 MB pool at 0x180940000.** It holds these targets, from the frame's dump and `tex=` lines:
- 1920×1080 RGBA8 at the base;
- sixteen 512×256 RGBA8 MRT slices at base + k·512 KB, drawn additively in draws #4–#25;
- the same memory viewed as one 512×4096 RGBA8 texture;
- two 256×8192 RGBA16F accumulators at `180940000` and `181940000`;
- a 1920×1080 D32S8 at `181138000`;
- a 1920×1080 RGBA8 at `181a38000`.

**The byte totals fit to the KB:**

| Term | KB |
|---|---|
| 6 full-screen 32-bit targets with their own fills, 1920×1088×4 = 8,160 KB each | 48,960 |
| one 8 MB image (512×4096 RGBA8 fits) | 8,192 |
| *subtotal = the Pod's 7 refreshes* | *57,152* |
| 16 × 512×256 RGBA8 slices | 8,192 |
| 2 × 256×8192 RGBA16F accumulators | 32,768 |
| one more image (guessed below) | 9,216 |
| **= the map's and Needlepoint's 26** | **107,328** |

- **The last term is a guess.** 9,216 KB is the gap from the depth target at `181138000` to the next target (0x900000). The image's real guest size comes from `GetDepthSliceSize` (`amdgpu/regs_depth.h:275`), which no log records.
- **The odd frames:**
  - The 18-refresh frames are 8 × 512 KB fewer: half the slices not bound.
  - The 28-refresh frames add 8,160 + 2,304 KB, the sizes of fill #386's two targets.
    - Those frames are exactly the 291–292 frames with 47 dispatches instead of 13 (frames ~2530–2970 of the walk, `ff1` and `ff7`).
    - So another workload (34 more dispatches) may write those two images instead.
- **Count refreshes, not images.** Four images share base `0x180940000`, and three fills per frame hit it. One image can therefore be refreshed twice, and the number of distinct images is unknown.

## B. What the code does on a write

**CPU write to tracked memory.** None of the 26 take this path.

1. Each guest page is 4 KB, both in the page manager (`page_manager.h:28`) and the buffer cache's region bits (`region_definitions.h:14`).
2. The fault goes from the runtime's exception dispatcher (`runtime/src/gpu/shadps4_glue.cpp:106`) to `GuestFaultSignalHandler` (`page_manager.cpp:434`), then `Rasterizer::InvalidateMemory` (`vk_rasterizer.cpp:1345`).
3. The buffer cache's `MemoryTracker::InvalidateRegion` (`memory_tracker.h:59`) runs with readbacks off (the default; `runtime/shim/core/emulator_settings.h:19-24`). It marks the page CPU-modified and unprotects it. Nothing is downloaded.
4. `TextureCache::InvalidateMemory` (`texture_cache.cpp:151-187`) handles the images on that page:
   - an image that the write overlaps → `CpuDirty`, and its whole range is untracked (`:165-168`);
   - an image that only shares the page → only that page is untracked (`:175`, `:180`);
   - an image that fits in one page → `MaybeCpuDirty`, which a hash checks later (`:184`).

**GPU write.**
1. A shader binds a buffer it writes (`vk_rasterizer.cpp:904`) → `ObtainBuffer(is_written)` (`buffer_cache.cpp:170`).
2. That marks the buffer's whole range GPU-modified (`ForEachUploadRange`, `memory_tracker.h:81`; `buffer_cache.cpp:185`).
3. LBP3 pre-sync (`buffer_cache.cpp:354-362`): if a cached image starts at exactly that address and is safe to download, `TileImage` copies the image into the buffer first.
4. `InvalidateMemoryFromGPU` (`vk_rasterizer.cpp:916-917` → `texture_cache.cpp:189-200`) flags as `GpuDirty`:
   - images at the same base (upstream's rule);
   - images lying wholly inside the range (LBP3's rule, `4640726e`, added for the Introduction fog).

**The re-upload.**
1. It happens at the image's next bind: `FindTexture`, `FindRenderTarget` or `FindDepthTarget` → `UpdateImage` (`texture_cache.h:138`) → `RefreshImage` (`texture_cache.cpp:786`).
2. For any dirty bit, it uploads **every mip and layer of the whole image**:
   - `ObtainBufferForImage` (`:851`) takes the arena if any page is GPU-modified, else copies guest memory to staging;
   - `DetileImage` (`:852`, `tile_manager.cpp:196`);
   - `UploadImage` (`:857`).
3. Nothing in this path works per page or per region.

## C. Confirming it in one run

**1. With no code change.**
- **How:** F9 during the walk, or `LBP3_DUMP_TARGETS=<a walk flip>` with `LBP3_TRACE_DRAWS=1` on the port exe (`runtime/src/gpu/renderer.cpp:433-445`; dispatch lines from `vk_rasterizer.cpp:392`).
- **What it prints:**
  - every fill's `bufW=` range and its constant buffer, mask and value words;
  - every draw's targets and textures, in order;
  - every cached target with size, format and address.
- **Offline step:** intersect the fill ranges with what is bound after each fill. That gives the predicted refresh list, to compare with 26 refreshes and 107,328 KB.
- **Limits:** it doesn't print guest sizes or the dirty bits.

**2. A small patch for measurement only**, behind `LBP3_FIRST_FRAME` or its own variable. Log each image once, keyed on address, size and format; the per-expansion line showed that a line per event costs about 15 ms a frame.
- **`InvalidateMemoryFromGPU`:** store on the image the writer's `pgm_hash` (set thread-locally in `BindBuffers`), the buffer's base and size, and which rule matched (base or contained).
- **`RefreshImage`:**
  - log the image's address, dimensions, format, tile mode and guest size;
  - its dirty bits, the arena vs memcpy branch, and the binding that asked;
  - the stored writer.
  - Add per-frame columns that split refreshes into GPU-dirty and CPU-dirty.
- **`TileImage`:** a count, its bytes and a GPU timestamp pair. This sizes the hidden copy.
- **`TextureCache::InvalidateMemory`, in case any CPU-dirty refresh appears:** log, once per image, the faulting address, the thread and `ContextRecord->Rip` as an eboot offset. The handler already gets the `EXCEPTION_POINTERS`.

**Expected result:** 25 of the 26 refreshes come from `5c5e1be6` with mask `ffffffff`, and one (`171fe8000`) from the alpha fill.

## D. Fixes

The fill's mask word decides the fix:
- **Mask `ffffffff` with `cb[1] == 0`:** a constant overwrite that never needs the old contents and doesn't depend on tiling, so an image clear is equivalent.
- **Any other mask:** the fill needs the image's current contents (read, modify, write).

| Fix | Gain | Risk |
|---|---|---|
| **F1.** Skip the LBP3 pre-sync (`buffer_cache.cpp:354-362`) when all of these hold: the writer is `5c5e1be6`, the mask is `ffffffff`, `cb[1] == 0`, **and the image lies wholly inside the written range**. The last condition matters because the pre-sync tiles the whole image, not just the written range (`SynchronizeMemoryFromImage`, `:367-413`). For a 2,304 KB fill at the base of a 1920×1080, the pre-sync is what keeps the image's other 5,856 KB correct. | The untimed `TileImage` copies, ~5–6 a frame of mostly 8,160 KB images. GPU time unmeasured. | Low. One shader hash, and its parameters are read from guest memory as the trace already does. |
| **F2.** Clear instead of upload for those fills: for each color image wholly inside the written range, record a pending clear instead of `GpuDirty`. Use `Runtime::ClearImage` (`vk_runtime.cpp:578`) at the next refresh, or upload as before if `CpuDirty` has been set since. | Up to ~25 of 26 refreshes, 92% of the bytes (90% if the depth image stays on upload): ~4.0 of 4.3 ms GPU a frame in the map and Needlepoint, ~2.4 of 2.8 ms in the Pod, plus ~0.1 ms CPU. Clears cost far less (untimed). | Low to medium. See the list below. |
| **F3.** The alpha fill (mask `ff000000`, one 1920×1080 a frame): apply it to the image. Either a small compute pass over an R32_UINT storage view, or a full-screen pass with write mask A. | The last refresh (~0.3 ms) and its pre-sync. | Medium: a new pipeline. Optional. |
| **F4.** General form of F2 and F3: one pass that applies `(x & ~m) \| (v & m)` to the image's raw bits. A one-word pattern doesn't depend on position for 32-, 64- and 128-bit texels. | Covers every `5c5e1be6` fill with `cb[1] == 0`. | Medium. |

**F2's gain assumes** that the dirty-bit log (section C, item 2) shows no `CpuDirty` refreshes.

**Risks to handle in F2:**
- **A pending clear has to stay correct until it runs.** With the lazy form:
  - a later write must drop it: `CpuDirty`, and also `GpuDirty` from any other writer;
  - `SafeToDownload()` (`image.h:104`) must count it as dirty. Otherwise the pre-sync, the texel-buffer sync or a capture download reads the pixels from before the clear.
  - The simpler alternative is to clear eagerly, at the fill's binding. The fill only writes the buffer, so recording the clear ahead of the dispatch is safe.
- **Non-zero values** (`00ffffff`, `007f7f00`): the clear must reproduce raw bytes. That is exact for UNORM8 (byte/255), but an sRGB image would convert the value; fall back there.
- **The depth image:** use `clearDepthStencilImage` with value 0 only, or leave it on the upload path.
- **Partial ranges:** a 2,304 KB fill flags the 1920×1080 at the same base. Keep upload for anything not wholly covered.
- **Recognising the shader:** match on its hash, and fall back if its parameter memory is GPU-modified or `cb[1] != 0`.
- **Regression scenes:**
  - the Introduction fog (the accumulators must still start at zero);
  - the Pod's alpha;
  - the map and Needlepoint lighting;
  - the photo captures.

**Not options:**
- Narrowing the "contained" rule brings back the Introduction white-out (NOTES.md:450).
- Removing the pre-sync brings back the dark Pod (NOTES.md:427).
- Skipping the fill dispatch itself would also need the buffer side kept consistent. Its ~122 MB of buffer writes (64 MB, seven fills of 8,160 KB and one of 2,304 KB) are the game's own work. Leave it.

**The CPU-side fixes from the brief** don't apply unless the log shows `CpuDirty` refreshes:
- finer tracking granularity;
- excluding known non-texture writes;
- uploading only dirty pages (a full region for linear images, macro-tile rows for tiled ones).

If they ever do, those fixes carry a high risk.

## E. Side questions (not performance, unverified)

- **Stale bytes on a partial fill, maybe.** The 1920×1080 at `17a018000` is flagged by a 2,304 KB fill (same-base rule).
  - If the pre-sync tiled that image into the buffer first, its re-upload is correct.
  - If the pre-sync picked the 960×540 instead (with two candidates it takes the one whose size matches, `texture_cache.cpp:656`), the re-upload carries 5,856 KB of arena bytes nobody wrote.
- **A sample that may read zeros.** The 512×4096 view at `180940000` is sampled after the 16 slices are drawn (trace draw #26). It is refreshed from the arena, which holds the fill's zeros, not from the slice images. Whether the game reads the slices' contents there was not checked.

## Unknown

- **Which images.** The 26 in Needlepoint are inferred from another scene's trace and the byte arithmetic. The 9,216 KB term is a guess. How many distinct images (as opposed to refreshes) there are is unknown.
- **Whether Needlepoint's fills match the traced frame's:** count, masks, values and targets.
- **The hidden copy.**
  - Whether the 5–7 extra staging requests a frame are all `TileImage`.
  - What the image→buffer copy and the tile dispatch cost on the GPU.
- **Wall-clock gain.** Transfers overlap recording, the game is capped at 30 fps, and Finish is 5.5 ms. So the ~4 ms of GPU time saved per frame is known, but not how much frame time it saves.
- **Other writers.** Whether any non-fill shader writes buffers over cached images in Needlepoint. The 47-dispatch frames, which carry the two extra refreshes, are the first place to look.

## Outcome (2026-11-12, fix/rt-fills)

Branches, local: shadPS4 `fix/rt-fills` `d5af2ae7` on `020cf1ec`; runtime `fix/rt-fills` `bac46c2` on `64d6252` (the `[fills]` status line `12cddd4`, then the submodule pointer at `d5af2ae7`). Worktrees `D:\Recomps\lbp3-wt\fills` and `fills-shadps4`.

### Measured (section C, item 2, built)

- **Counters, always on:** a `[fills]` line after each `[perf]` line, per frame: refreshes and MB, how many had a CPU-dirty flag, images cleared, pre-syncs (`TileImage`) and MB, pre-syncs skipped.
- **`LBP3_FILL_LOG=1`:** one line per distinct refreshed image (address, size, format, tiling, flags, the last GPU writer's hash) and per distinct fill (each image in range and what was done with it).
- **Runs:** `m5_np_route.sh rtf_meas_old` (`LBP3_RT_FILLS=old LBP3_FILL_LOG=1`), `rtf_meas_new` and `rtf_meas_new2` (the final build). Data in `analysis\m5_rtf_meas_*_stderr.txt`.

**The answers to section C and to the Unknown list:**

| | Pod | map / Needlepoint |
|---|---|---|
| refreshes a frame (old) | 7, 55.8 MB | 26, 104.8 MB (28 in the 47-dispatch frames) |
| CPU-dirty among them | 0.0 | 0.0–0.1 (0.4 for a few windows) |
| pre-syncs (`TileImage`) a frame (old) | 5, 39.8 MB | **7, 56.3 MB** |

- **Every steady-state refresh is the fill's:** all refreshes with a GPU writer name `5c5e1be6`. The others (writer 0) all carry a CPU-dirty flag: new images and loads. No other shader writes over cached images on this route, so the "lazy vs eager clear" question doesn't arise here.
  - The writer log ran only on this route (Pod, Prologue map, Needlepoint). Bunkum, Create and the photo scene passed the regression below, but with no writer log or screenshot A/B.
- **The hidden copy is larger than estimated:** 7 image→buffer copies a frame, 56 MB, not 5–7 small ones.
- **The images, now named** (Needlepoint):
  - seven 1920×1080 RGBA8 UNORM targets: `171fe8000`, `17a018000`, `17a818000`, `17b018000`, `17b818000`, `180940000`, `181a38000`;
  - the 960×540 at `17a018000`;
  - the sixteen 512×256 slices, the 512×4096 view and the two 256×8192 RGBA16F accumulators in the pool;
  - **the D32S8 depth target at `181138000`, guest size `0x900000`.** That confirms the guessed 9,216 KB term;
  - one more, not in the trace: the 256×128×64 RGBA8 lighting volume at `172930800` (thick tiling, 8 MB).
  - In the Pod, also the two 1920×1080 sRGB display buffers `160800000` and `160ff8000` (fills with `ff000000`, `ffffffff`, `ffeeeeee`).

### The change

- **In `DispatchDirect`,** a dispatch of `5c5e1be6` is a full overwrite when all of these hold:
  - its 4 buffers are as expected, and its parameters aren't GPU-modified;
  - the mask is `ffffffff` and `cb[1] == 0`;
  - dst has stride 4 with at least `cb[0]` records;
  - the dispatch is one-dimensional with at least `cb[0]` threads.
- **F1:** for that destination, the buffer cache skips the pre-sync of an image lying wholly inside the written range.
- **F2:** each image wholly inside the range, with no CPU write pending, is cleared to the word just before the dispatch, instead of being flagged `GpuDirty`. This applies only when a clear reproduces the word exactly:
  - zero in any colour format, and as depth 0.0, depth aspect only (the re-upload never writes stencil);
  - another word in RGBA8 or BGRA8 UNORM;
  - sRGB when each colour byte is `00` or `ff`.
  - It covers volumes too (one layer, all of the depth). Block-compressed and MSAA images keep the upload, and so does everything else.
- **Kept as the re-upload left it:** `GpuModified` is restored after the clear (it decides downloads and which image a buffer sync copies from), and the backing is set to the image's own sample count, as `UploadImage` does.
- **Switches:** `LBP3_RT_FILLS=old` gives the old path; `=presync` gives F1 only.

**After** (`rtf_meas_new2`): Needlepoint 26 → **3 refreshes (32 MB)**, 7 → **1 pre-sync (8 MB)**, ~59 clears (138 MB). Pod 7 → 2 refreshes, 5 → 2 pre-syncs.

**What still re-uploads:**
- `171fe8000`, the alpha fill (mask `ff000000`; F3, not done);
- the 1920×1080 at `17a018000`: the 2,304 KB fill covers only its first part;
- the 256×8192 accumulator at `180940000`: flagged by the 8 MB fills at the pool's base, which cover only its first half;
- in the Pod, `160ff8000` filled with `ffeeeeee` (sRGB `ee` isn't exact).

**Cleared vs refreshes saved:** 59 clears a frame against 23 refreshes saved, because each fill clears every image in its range, used later or not. That costs ~138 MB of clear bandwidth a frame (~0.15 ms at the 3090's bandwidth), and the A/B below includes it.

### A/B (`D:\Recomps\lbp3-wt\_tools\fills_ab.sh`, same build, 3 + 3 alternating runs of `m5_np_route.sh` with `LBP3_FRAME_SPLIT=1`)

Data in `analysis\m5_rtf_ab.txt` and `m5_rtf_{old,new}<i>_*`.

| | walk work (the measure) | standing work | GPU wait at the end of the frame (`finish`, mean of the last 1,680 frames) |
|---|---|---|---|
| old | 22.29 / 22.28 / 22.85 ms | 18.95 / 18.98 / 19.63 ms | 5.55 / 5.56 / 5.73 ms |
| new | **21.10 / 21.46 / 20.84 ms** | **18.32 / 18.59 / 17.88 ms** | **4.38 / 4.32 / 4.38 ms** |
| F1 only (`presync`, 1 run) | 21.96 ms | 18.90 ms | 5.51 ms |

- **New vs old:** the walk is 1.3 ms shorter a frame and standing 0.9 ms shorter, with no overlap between the modes. The GPU tail is 1.25 ms shorter.
- **F1 vs F2 is not measured.** The `presync` run (1 run, which skipped 4 of the 7 copies because the images are still dirty there) suggests the copies are the smaller part. `TileImage` has no GPU timestamps; only counts and bytes were measured.
- **Unchanged:** frames over 40 ms (1 per walk in every run, the same frame); the main thread (6.3–6.5 ms); recording (1.1 ms); GPU warnings 0.

### Looks the same

`shot_compare.py`, `shot_cells.py`, and a new `shot_shift.py`, all in `D:\Recomps\lbp3-wt\_tools`.

- **Prologue map** (3 + 3): old vs new 0.56–1.32 mean |diff|, against 0.68–1.31 within a mode. Luminance 93.31–93.35 both.
- **Needlepoint start:** 0.31–0.40 against 0.00–0.59 within a mode.
- **Needlepoint basket** (`heavy`, `heavy_end`): 3.4–4.5 against 0.5–2.2 within a mode. This is the one shot that did not match within noise. The evidence says it is where the walk ends, not rendering:
  - **The pattern:** the differences sit on edges over the whole picture, and the per-cell luminance is unchanged (|Δ| ≤ 1 outside the swinging bunting). That is a sub-pixel change of position, not a change in how things are drawn.
  - **Every `heavy_end` from this build and the expansion lane's** (`shot_shift.py`), against two references, `xab_old1` (expansion build) and `rtf_old1`:
    - **Close to `xab_old1`:** `rtf_new1`, `rtf_new2`, `rtf_new3` (0.70–1.06); **`rtf_meas_old`, an old-mode run (0.78)**; `rtf_presync1` (0.90); `xab_old3` and `xab_new1` (0.81, 1.56).
    - **Close to `rtf_old1`:** `rtf_old2` and `rtf_old3` (0.71–0.76); **`rtf_meas_new`, a new-mode run (0.32)**; `xab_new2` and `xab_new3` (1.00–1.02).
    - **Elsewhere:** `rtf_meas_new2` (2.15 / 5.14) and `rtfr_np` (3.54 / 3.96), so there are more than two end states.
    - Both modes produce pictures in both main groups. In the 3 + 3 A/B the split happened to follow the mode, perhaps because the walk's timing shifts with frame time, but the measurement runs land the other way round.
  - **The static shots agree:** `start`, before any walking, is within noise.
- **Pod Home** (`fills_intro_ab.sh`, 2 + 2, `pace_run_intro.ps1`): old vs new 2.13–5.36 against 4.58–5.49 within a mode (the spinning planet). Luminance 37.58–37.65.
- **Introduction fog:** 0.00–0.96 against 0.59–0.69 within a mode. Luminance 215.02 / 215.09 old, 215.16 / 215.09 new. Steps ≠ 1: 0.33% and 0.00% old, 0.78% and 0.00% new, inside the usual 0–0.9%.

### Regression

The run used `expand_regress.sh rtfr`, which swaps the build into the clone's `build\lbp3.exe` and puts the original back by trap (md5 `5a108abb…` before and after). Log: `analysis\m5_rtf_regress.txt`.

| Check | Result |
|---|---|
| Introduction, steps ≠ 1 | 0.78%, 0.00% |
| Thruster recipe | 3 of 3 |
| Bunkum | 16 of 16 voice lines; Needlepoint loads |
| Create sky | (121, 177, 182), cyan |
| Photo | PASS: encoder, capture readback, 1 JPEG. The photo looks right |
| Backup / import | 6 dialogs on the overlay; the cancels changed nothing |
| `t13_coop.sh` | PASS |
| Heavy route | walk work 20.91 ms, standing 17.78 ms, 1 frame over 40 ms |

- **No faults:** 0 dropped draws, 0 stale flip markers, 0 GPU warnings, and no nvlddmkm or Display event over the whole run.
- **Offline tests:** cooptest 81, dlgtest 154, osktest 263, dialogtest 368, jpegtest 157 and imetest 403 checks, 0 failed.
- **Clean state after:** the human's saves are identical to `human_play_20261111_2104`, and no marker, kept copy or swapped exe was left.

**Verdict: merge** shadPS4 `fix/rt-fills` and runtime `fix/rt-fills`. Each frame in a level gets 1.3 ms shorter (walk) and 0.9 ms shorter (standing), the GPU tail 1.25 ms shorter. No visible difference and no regression.

**Left after this,** ~32 MB a frame:
- the alpha fill (F3: a compute pass applying the mask to the image);
- the two partial-range targets, whose re-upload keeps the old behaviour (section E's question about stale bytes still stands).
