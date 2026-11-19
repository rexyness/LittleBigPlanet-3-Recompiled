# Lane (sethlikepie study): the GPU core (shadPS4) and performance, seth's port against ours (2026-11-19)

Part of [sethlikepie_vs_open_items.md](sethlikepie_vs_open_items.md). The comparison is sethlikepie/littlebigplanet3-pc at `07cc7e7`: one commit, "0.1.1", 2026-11-18. Its vendored shadPS4 is `gpu/shadps4`. Ours is the shadPS4 fork `lbp3` at `37508fd9` and the runtime `lbp3-recompiled`.

Everything here is read-only. Nothing was built or run, and none of seth's scripts were executed.

## Labels

- **read:** from code, or from our own logs and notes.
- **seth's claim:** from their comments or docs, not measured by us.
- **sgdc3 msg:** sgdc3's own commit message, fetched from the GitHub API (`api.github.com/repos/sgdc3/shadPS4/commits/<hash>`) and summarised by a fetch tool. It is paraphrased, not quoted.
- **inferred:** our reasoning.

## Context and cautions

- **Most of seth's performance numbers aren't LBP3 numbers.** `docs/parallel_gpu.md` measures "the base port's original game": God of War III (`seth:gpu/shadps4/video_core/texture_cache/texture_cache.h:205` "War III's exposure", Gnmx, copy shader `fefebf9f`). It ran on Linux (perf, taskset, userfaultfd, radv) with an RX 7800 XT and 16 threads. So the +22%, +11.5%, +19% and similar figures are **seth's claim on another game and OS**. The LBP3-specific claims are the ones in code comments: F79 VRS, F80/F81 fills, ATLAS, F110 and F91.
- **"Separable" means hunks we would cut out by hand.** Their history is a single commit, so there is nothing to cherry-pick.
- **Licence:** their shadPS4 files are GPL-2.0-or-later, the same as our fork, so code can be taken with credit.
- **Any recompiler fix we port needs a cache invalidation.** Our SPIR-V cache is keyed by program hash (`shadPS4:src/video_core/renderer_vulkan/vk_pipeline_serialization.cpp:24`, `ShaderBinaryVersion = 7`). Without a bump, a warm cache keeps serving the old translation (sgdc3 `174a8f4`'s lesson).
  - **A global bump isn't free.** compile_stalls.md calls it a hazard: our whole cache is dropped once, so every first sight translates again (2–3 ms each). Shaders whose SPIR-V didn't change should still mostly hit NVIDIA's driver cache [inferred].
  - **The cheaper route:** salt only the affected programs, through the mechanism we already have (`Lbp3Res::StoredHash`, shared_cache.md), once a scan has named them [inferred].
  - Any A/B for ticket 17 must run on an empty copy of the cache (`LBP3_SHADER_CACHE_DIR`).

## Ranked by expected impact on our open items

| # | Item | Seth / sgdc3 | Ours | Verdict | Effort | Risk | Open items |
|---|---|---|---|---|---|---|---|
| 1 | Write-fault window | A fault also unprotects the clean, buffer-tracked pages of the aligned 256 KiB around it | One page per fault; our gfx_wait "fix 2" planned exactly this | Portable with small rework | S | Medium (coherence; dark-Pod area) | Diner / fluid frames |
| 2 | Async-pipeline wait policy | Some draws wait for their pipeline: full-screen, indirect, one-time targets, loading screens, photos and thumbnails (F110, F111) | `84e058af` skips every draw | Portable with rework | S–M | Low | First-visit pauses (makes async safe to default on) |
| 3 | Recompiler correctness bundle | F91 `s_and_b64 exec` scope; `#5346` rsq/log legacy and clamp; `eb51f01` rcp/rsq clamp and WRITELANE; P6 SNORM −1 clamp | All missing | Portable as-is, after an opcode scan | S each | Low (code); one cache rebuild | General correctness; ticket 17 unscanned, so the scan decides |
| 4 | Stacked targets vs seth's ATLAS | The same fix, **off** by default: "Manglewood 21–26% darker" | `37508fd9` **on** | **Keep ours.** ON is what the PS4 does; seth's darker measurement corroborates the direction | S (an optional check) | — | Ticket 17, fog |
| 5 | Masked fill applied to the image (F81) | `mask_fill.comp` over an R32UI view, on by default | Our F3 (reuploads.md), not done: the alpha fill still pre-syncs and re-uploads | Portable with rework | M | Med (dark-Pod path) | 4K and 1440p GPU cost |
| 6 | Staging pool retention and prewarm | Blocks live 3,000 frames idle; keeps 512 MiB; 8 × 16 MiB prewarmed | 300 ring / 120 large *submits* (~10 s / ~4 s at our 30 fps), no prewarm | Prewarm plus longer large-buffer life; measure first | S | Low (memory) | Streaming and first-visit stutter (small) |
| 7 | `7184f52` preload state reset | Stage state cleared on entry to `LoadGraphicsPipeline` | Missing: no reset on entry or on the failure path | Portable as-is | S | None | Correctness after cache damage |
| 8 | `80ffa53` per-dimension extent compare | `SubresourceExtent::Contains` | Missing: lexicographic `<`/`>` at 3 sites | Portable as-is | S | Very low | Texture-cache correctness |
| 9 | VRS on Sackboy fabric and fuzz (F79) | 2×2 shading rate on 2 pixel shaders, ≥2560 wide | None | Portable with rework | M | Visual trade-off | 4K cost |
| 10 | `72fa5ae` GPU-write invalidation by overlap | Any overlapping image is marked dirty, after resolving readbacks | Start-match or fully contained only | Portable with care | S–M | Med | Lighting volumes and atlas (speculative) |
| 11 | `553bcf9` stencil colour aliases | A staged stencil copy, or a zeroed depth-only view | An invalid RGBA8 view with DEPTH\|STENCIL | Add a detector first | S (detector), M (port) | Low | Not seen in our traces |
| 12 | Garlic heap scaling | +320 MiB per extra 1080p frame of pixels | Only by hand (`LBP3_RES_POOL_MB`) | Portable as-is | S | Low | 4K safety |
| 13 | Background warm-up and pipeline binaries | Boot doesn't wait for the preload; `VK_KHR_pipeline_binary` on demand | Blocking parallel preload, about 5 s | Portable with rework | M–L | Med | Boot time only |
| 14 | Per-draw CPU memos | Texture description cache, render-state memo, texture-set memo, lock-free `UpdateImage` | None | Entangled | M–L | Med | Possibly fluid frames, via the processor (unproven) |
| 15 | Draw-prep workers, two-stage draw pipe, constant ring | Seth's architecture | — | Not worth it | L | High | Our GPU thread isn't the limit |
| 16 | `6f4b3cd` + `3a4d9b0` refresh deferral | Mid-frame refreshes deferred a frame, plus two follow-ups | We refresh in stream order | Not applicable / harmful | — | — | — |
| 17 | `64abeb2` equeue slack during compiles | Timeouts ≥ 100 ms stretched to 10 s after a compile | Our runtime's equeue honours timeouts; no known symptom | Not needed unless one shows | S | Low | — |
| 18 | Readback queue, deferred/stale readbacks | Opt-in in the code; the launcher turns it on | Readbacks off; captures only when queued | Not worth it | M | Med | — |

Already have: `c90754b` (GS vertex clamp), `9d38194` (LineLoop), `2966581` (clear swizzle, nearly), `1f45940` (slice-built volumes, our own design), `174a8f4` (policy only).

### By open item

- **Brightness (ticket 17):**
  - **Nothing in seth names a fix for it.** No brightness or exposure work appears in their GPU code or docs. [read]
  - **Seth's ATLAS measurement supports our `37508fd9`:** the faithful lighting and fog volume is darker. That makes the volume path (other scenes, other volumes) still the most concrete lead (§5.1) [inferred].
  - **Recompiler gaps** (F91, `#5346`, `eb51f01`, P6) are unscanned. A scan says whether LBP3's lighting shaders use them (§5.2–3).
  - **Ruled out on ours:**
    - scaled MIN/MAX blends (§5.4);
    - `553bcf9`'s stencil alias, in the traced frames (§1).
- **Sky colour:**
  - `2966581` is already in (`876508ce`). Only the format-remap residue is left (one line).
  - Nothing else in seth touches the sky.
- **Fog:**
  - The volumetric light and fog volume is ATLAS/stacked targets (ours is on and correct).
  - Slice-built volumes: `1f45940` and seth's F44, our equivalent.
  - Our Introduction fog white-out fix has no counterpart to take.
  - `72fa5ae` could matter for partial GPU writes into volume memory (speculative).
- **Dark Pod:**
  - F81 (masked fill on the image) is a faster way to apply the very fill behind the dark Pod. A perf item that runs through that path, so it needs Pod screenshots.
  - The fault window and `72fa5ae` touch the same coherence area: they're risks to check, not fixes.
  - F110: under skip-all async, the Pod's decoration comes up as bare cardboard. That applies only if `LBP3_ASYNC_PIPELINES` is on, and it is the reason to take the wait policy.
- **Flicker:**
  - **Nothing found.** Seth's "UI flicker" was caused by their own deferred copies (`docs/parallel_gpu.md:84-87`). Their TAA and overlay flicker work belongs to their upscalers.
- **First-visit shader pauses:** the async wait policy (2e), plus staging prewarm (small).
- **Diner and fluid frames:** the fault window (2a).
- **4K cost:**
  - F81 (~1 ms of GPU at 4K per seth);
  - F79 VRS;
  - heap scaling for safety;
  - "render above output", which needs seth's six display-global redirects (§3).

---

## 1. sgdc3's 13 LBP3 fixes

Seth's code names 7 of the 13 in comments. The other 6 (`80ffa53`, `1f45940`, `9d38194`, `eb51f01`, `72fa5ae`, `2966581`) appear only in `seth:gpu/VENDOR.txt:13-14`, so they were identified from the sgdc3 message, then found by content in seth's tree (cited) and searched for by content in ours.

### `6f4b3cd`: defer mid-frame CPU re-uploads of GPU-written render targets
- **What it does:**
  - A frame epoch advances on patched guest flips (`seth:gpu/shadps4/video_core/amdgpu/liverpool.cpp:474-477`, `texture_cache/texture_cache.h:318-325`).
  - `RefreshImage` skips any image the GPU wrote this epoch and refreshes it at the next frame's first use (`texture_cache/texture_cache.cpp:2584-2597`). [read]
  - The point, per sgdc3: "a pipelined game re-initializes a render target from the CPU while this frame still draws into it". [sgdc3 msg]
- **Ours:** none. `RefreshImage` refreshes in stream order (`shadPS4:src/video_core/texture_cache/texture_cache.cpp:1159`). Our masked fills turn into clears (`d5af2ae7`) or a pre-sync plus re-upload (the alpha fill). [read]
- **The cost to seth:**
  - It needed `3a4d9b0` (below) and their own F85 FILLNOW/F85b. The deferral had kept a UI-panel clear for the next frame, "the post (bloom, 1.3x, vignette) switched off while menus move", and the reveal circles glitched (`texture_cache/texture_cache.cpp:661-676`). [seth's claim]
  - Ticket 17 proved the PS4 runs LBP3's fills strictly in stream order. Deferring is a step away from that.
- **Verdict:** not applicable, and likely harmful for us: we have no symptom it fixes.
- **Open items:** none. For dark Pod, fog and flicker it is a risk, not a fix.

### `80ffa53`: compare subresource extents per dimension
- **What it does:** adds `SubresourceExtent::Contains` (levels and layers separately) and uses it where the cache asks "can this image serve the request?" (`seth:gpu/shadps4/video_core/texture_cache/types.h:70`; `texture_cache.cpp:1549,1694,2007,2015`). [read]
- **Ours:** the defaulted `operator<=>`, which is lexicographic (`shadPS4:src/video_core/texture_cache/types.h:63-68`), at `texture_cache.cpp:371` (ResolveDepthOverlap), `:499` (expand) and `:920` (FindImage). [read]
  - An image with more mips but fewer layers counts as "big enough", so it is reused short of layers.
- **Verdict:** portable as-is. S, very low risk.
- **Open items:** none known. Mipped arrays in LBP3 aren't confirmed [inferred].

### `1f45940`: slice-built arrays and volumes without thrashing
- **sgdc3's version:**
  - Single-mip arrays grow to the next power of two.
  - A stale 3D volume is dropped when a slice build restarts, and the new array is sized for every slice.
  - `SanitizeCopyLayers` gets quieter.
  - Code: `seth:.../texture_cache.cpp:1694-1720` and `:2016-2045`. [read]
- **Seth went further, back to our strategy:** their F44 `VolumeServesSliceTargets` renders slices through a 2D-array view of the volume instead of dropping it (`texture_cache.cpp:2007-2014`). [read]
- **Ours:**
  - `7eab2b0d` grows geometrically and uploads only the new layers.
  - `d2fdcafe` adds the layers-only path.
  - The FindImage exception keeps the volume and renders into its slices (`shadPS4:src/video_core/texture_cache/texture_cache.cpp:920-930`).
  - The same end state as seth's F44. [read]
- **Verdict:** already have, by a different route.
  - Only the quieter warning is left: `SanitizeCopyLayers` produced 15,904 warning lines in our logs (`lbp3-recompiled:analysis/*_stderr.txt`). [read] That's cosmetic, S.

### `c90754b`: clamp the GS `OutputVertices` to the copy shader's count
- **Seth:** `seth:.../shader_recompiler/backend/spirv/emit_spirv.cpp:472-476`, `info.h:112`, `ring_access_elimination.cpp`.
- **Ours:** `5b57642d` does the same in BuildRuntimeInfo (in the permutation key) and adds a device-limit guard.
- **Verdict:** already have, and ours is more complete.

### `553bcf9`: serve stencil colour aliases from a staged stencil copy
- **What it does:**
  - When a colour T# (RG8 or RGBA8) reads a D32S8 image, the read is redirected to a host-only colour copy of the stencil plane (`seth:.../texture_cache/texture_cache.cpp:2465-2571`, `renderer_vulkan/vk_rasterizer.cpp:4090-4135`).
  - Anything else becomes a zeroed depth-only view (`texture_cache/image_view.cpp:121-145`). [read]
  - The reason given: an RGBA8 view with DEPTH|STENCIL aspect is invalid, and "NVIDIA hangs the GPU (TDR) or loses the device". [seth's claim]
- **Ours:**
  - The view keeps `image.aspect_mask` when the format matches neither aspect (`shadPS4:src/video_core/texture_cache/image_view.cpp:106-118`).
  - ResolveDepthOverlap leaves colour-over-depth texture binds "to the view" (`texture_cache.cpp:368-376`, `:427`). So we would create the invalid view. [read]
- **Does LBP3 hit it on ours?** Not in the traced frames.
  - In `analysis/fg_dump/bk_b_trace.txt` and `fg_np_stderr.txt`, every logged T# at a depth address (`170038000`, `171478000`, `178938000`, `1794a8000`, `181138000`) is `dfmt=4 nfmt=7` (R32F), a valid depth view. No `dfmt=10` T# lands on a depth address. [read]
  - The "[draw] image" lines don't cover every binding.
  - 0 `nvlddmkm` events across our sessions (issue 15) also argue against it.
- **Verdict:** add a one-line warning first (S), for a colour view requested over a D/S image.
  - Port the copy (M) only if the warning fires somewhere (Create mode or unvisited levels).
- **Open items:** possibly ticket 17 if it fires in a lit scene. Not seen so far [inferred].

### `9d38194`: line loops drawn as line strips
- **Seth:** `seth:.../renderer_vulkan/liverpool_to_vk.cpp:101,117`.
- **Ours:** `24e4e494` (`liverpool_to_vk.cpp:127-131`).
- **Verdict:** already have.

### `7184f52`: reset the shared stage state before loading a cached pipeline
- **What it does:** clears `fetch_shader`, `infos` and `modules` on entry (`seth:.../vk_pipeline_serialization.cpp:766-771`). [read]
- **Ours:**
  - They're cleared only after a successful build or job (`shadPS4:src/video_core/renderer_vulkan/vk_pipeline_serialization.cpp:248-300`).
  - An early `return false` (a missing meta or a failed stage) leaves them set. The next preloaded pipeline then inherits stale stages for every stage it lacks, and the old fetch shader.
  - The caller doesn't reset them either: `WarmUp` only counts `result` (`vk_pipeline_serialization.cpp:467-476`). [read]
- **When it bites:** only with a damaged or pruned cache.
  - That's more likely now with salted keys, held shaders and cache seeding. No "stale pipelines" warning has appeared in our logs yet [read: first_visit.md].
- **Verdict:** portable as-is. S (3 lines), no risk.

### `64abeb2`: GPU event-queue waits get slack during compiles
- **What it does:** after a compile, a wait of ≥ 100 ms on an equeue carrying GraphicsCore or VideoOut events is stretched to ≥ 10 s for 2 s. [sgdc3 msg; read `seth:.../vk_pipeline_cache.cpp:1451-1453`, `core/libraries/kernel/equeue.h:217-220`]
- **Ours:** the equeue is our runtime's own (`lbp3-recompiled:runtime/src/hle/kernel_sync.cpp:339-349`, which honours timeouts). [read]
  - Nothing in our notes ties a symptom to an equeue timeout during compiles [inferred from first_visit.md and compile_stalls.md].
  - The "WaitRegMem stuck" lines in our logs (89 across all logs) are something else: our command processor's own stuck-label reporter (`liverpool.cpp` `ReportStuckWait`), not guest equeue timeouts. [read]
- **Verdict:** not needed unless an equeue-timeout symptom shows.

### `eb51f01`: clamped rcp/rsq, and a real `V_WRITELANE_B32`
- **What it does:**
  - `V_RCP_CLAMP_F32` and `V_RSQ_CLAMP_F32` return ±FLT_MAX instead of infinity.
  - `V_WRITELANE_B32` writes one lane through a select on the subgroup lane id (`seth:.../backend/spirv/emit_spirv_warp.cpp:41-48`, `frontend/translate/vector_alu.cpp:186-189`, `shader_info_collection_pass.cpp`). [read]
- **Ours:**
  - Both clamp opcodes map to the plain op (`shadPS4:src/shader_recompiler/frontend/translate/vector_alu.cpp:184-187`).
  - `EmitWriteLane` returns **zero** for the whole register (`backend/spirv/emit_spirv_warp.cpp:41-43`). So any SGPR spilled through writelane and restored with readlane comes back as 0. [read]
- **Does LBP3 use them?** Unknown.
  - Our 156 IR dumps contain no lane ops, but the IR collapses the opcode variants, and 156 is a small sample of 135k shaders.
  - Extending `tools/res1440_fullscan.py`'s GCN decoder to count these opcodes would settle it (S).
- **Verdict:** portable as-is. S, low code risk, plus the cache rebuild (see Context).
- **Open items:** general correctness. A WRITELANE hit in a lighting shader could be anything, and none is shown [inferred].

### `72fa5ae`: invalidate every image a GPU write covers
- **What it does:** a GPU buffer write marks every overlapping image GpuDirty, not only one that starts at the write's address (`seth:.../texture_cache/texture_cache.cpp:803-813`).
  - Seth's version calls `ResolveReadbacks` first, "partial buffer writes still need the image's untouched bytes". [read]
- **Ours:** start-match or fully contained (`shadPS4:src/video_core/texture_cache/texture_cache.cpp:293-307`).
  - An image that begins before the write, or runs past it, isn't marked, so it keeps stale contents. [read]
- **Why it needs care in ours:** marking a partly overwritten render target dirty re-uploads the whole thing from the arena. Its unwritten bytes are only correct if the arena was synced from the image first: the dark-Pod mechanism (`lbp3-recompiled:NOTES.md:427-431`) [inferred].
- **Verdict:** portable with care. S–M, medium risk.
- **Open items:** possibly the lighting volume and atlas paths (ticket 17's suspect area). Speculative, so do it after the checks in §5.

### `2966581`: stop swizzling the colour-buffer clear value
- **sgdc3's version:** keeps only the remap that our own Vulkan format choice implies, `RemapSwizzle(format, IdentityMapping)` (`seth:.../liverpool_to_vk.cpp:833-837,1162`).
- **Ours:** `876508ce` drops the swizzle entirely (`shadPS4:src/video_core/renderer_vulkan/liverpool_to_vk.cpp:1163-1166`, `(void)comp_swizzle`). [read]
- **The difference:** only formats `RemapSwizzle` remaps (1_5_5_5, 11_11_10, 10_10_10_2, 4_4_4_4), and only with a non-zero clear colour.
- **Verdict:** already have, nearly. Take the one-line remap for exactness. S.

### `3a4d9b0`: mirror full-surface overlay-reset fills into aliased images
- **What it does:** an exception to `6f4b3cd`.
  - A graphics-ring surface fill with a full mask and a zero-alpha, non-zero colour refreshes the exactly aliased image at once (`seth:.../vk_rasterizer.cpp:2779-2799`, `texture_cache.cpp:632-648`, `MatchComputeSurfaceFill` at `vk_rasterizer.cpp:3514`). [read]
- **Verdict:** not applicable: it only patches the deferral we don't have.

### `174a8f4`: bump the shader-binary cache version
- **What it does:** a version bump plus the rule "a codegen change needs a bump" (`seth:.../vk_pipeline_serialization.cpp:39-46`; seth is at 20 now, F91 included). [read]
- **Verdict:** policy only. Apply it to every recompiler port in §3 (one bump per release that carries them).

**Also in seth's tree under sgdc3's name but not in the 13:**
- `403d8d4` and `4b1f2a6` (equeue lifetime and locking: `seth:.../core/libraries/kernel/equeue.cpp:83,262,314`; `gnmdriver.cpp:178`).
- They belong to the runtime lane. Our equeue is our own.

---

## 2. Seth's own "lbp3:" changes

There are about 1,000 markers. The heaviest files: `videoout/driver.cpp` 139, `vk_rasterizer.cpp` 122, `vk_pipeline_cache.cpp` 61, `vk_presenter.cpp` 45, `texture_cache.cpp` 40, `vk_temporal_upscaler.cpp` 38.

### Inventory, grouped

- **Shipped architecture (CPU side of the GPU thread):**
  - draw-prep scanner and workers (`vk_draw_prep.*`);
  - two-stage draw pipe (`vk_draw_pipe.h`), constant ring, recorder fences, copy threads (`lbp3_copy.cpp`);
  - per-draw memos (texture description cache, `FindImage`/`FindView` memo, render-state memo, texture-set memo);
  - lock-free `UpdateImage`;
  - toggles for each (`seth:gpu/shim/lbp3_toggles.h:23-81`).
- **Shaders and pipelines:**
  - async compiler with translation on workers (`vk_async_compiler.h`) and the wait policy (F110/F111);
  - stage cache (F77);
  - background warm-up (`vk_warmup_inbox.h`) and the `VK_KHR_pipeline_binary` cache;
  - F91 exec scope, P6 recompiler fixes;
  - early-Z hint (F78, off) and VRS (F79, on).
- **Fills and refreshes:**
  - F36 fill → clear (= ours `d5af2ae7`);
  - F80 FILLKEEP (arena uniform-value model);
  - F81 FILLMASKED (= our unfinished F3);
  - F85 FILLNOW, F103/F113 skips;
  - ATLAS (B1, off).
- **Resolution, upscalers and the presenter:** scene proxies, FSR 3/4, DLSS, XeSS, TAA, object and camera motion, sharp UI, render-scale literals, bloom anti-aliasing at non-1080p sizes (B1/B3), direct Vulkan presentation. Not ours to port: a different output architecture.
- **Diagnostics:** "Stall:" lines, `LBP3_FRAME_STATS`, a GPU profiler, readback-source stats, F83/F116 audits.

### Focus items

**2a. Write-fault window (toggle 65536, `LBP3_FAULT_WINDOW`)**
- **Seth:**
  - After a guest write fault, `BufferCache::ExtendWriteFault` → `RegionManager::ExtendWriteFault` marks every page of the aligned window CPU-modified and untracks it, but only pages the buffer cache protects for CPU writes and that hold **no GPU-modified data** (`cpu_mask = mask & ~gpu[index]`).
  - Code: `seth:gpu/shadps4/video_core/buffer_cache/region_manager.h:178-198`, `buffer_cache.cpp:192-202`; called from `Rasterizer::OnWriteFault` (`vk_rasterizer.cpp:5216-5223`). [read]
  - The default is now **256 KiB** (4–1024, a power of two), not 64. [read]
- **Seth's numbers (base game, Linux) [seth's claim]:**
  - 64 KiB: 115k → 16k faults/s, 66.4 → 81.0 FPS (`docs/parallel_gpu.md:64-69`).
  - 256 KiB "halves them again" (`:94`).
- **What keeps it safe in seth's tree [read]:**
  - The texture cache shares the per-page watcher counts with the buffer cache (`shadPS4:src/video_core/texture_cache/texture_cache.cpp:1315-1337`, `page_manager.cpp:41-87`; the same in ours). So a page an image also watches stays protected when the buffer's watcher goes.
  - GPU-modified pages are excluded.
- **What would make it unsafe in ours:**
  - The danger isn't uploading too little. It is stale guest bytes uploaded over arena data that came from an image (the dark-Pod failure, `lbp3-recompiled:NOTES.md:427-431`).
  - In ours, a written `ObtainBuffer` marks its whole range GPU-modified (`ForEachUploadRange<…, Clear, Set>`, `shadPS4:src/video_core/buffer_cache/memory_tracker.h:80-92`). That covers the masked-fill pre-sync (`buffer_cache.cpp:355-371`), so those ranges are excluded. [read]
  - **Not excluded:** the read-only texel-buffer path, which copies an image into the arena without marking it (`buffer_cache.cpp:351-352`). It re-syncs on every `ObtainBuffer`, so a stale upload there would be overwritten before use [read; the "before use" is inferred].
  - Not every path was audited, so the risk is rated **medium**.
- **Ours:**
  - Each fault ends in `BufferCache::InvalidateMemory` → `MemoryTracker::InvalidateRegion`, which with readbacks off marks just the faulting page (`shadPS4:src/video_core/buffer_cache/memory_tracker.h:59-62`; entry at `page_manager.cpp:439-440`).
  - Our `ChangeRegionState`/`UpdateProtection` (`region_manager.h:84-110,205-229`) is the same code seth's helper builds on, so it's about 40 lines plus a switch. [read]
  - Fault cost is already cheaper since `fix/protect-fast` (`0570df8`).
- **Fit to our problem:**
  - gfx_wait.md's "fix 2: fewer faults … unprotect a large block on the first write fault" is exactly this design.
  - Our numbers: ~1,380 faults per fluid frame on the `Spu` job threads, on the serial fluid chain (862 before the block). [read: perf/gfx_wait.md]
  - The Diner's heavy 1440p fluid windows had 40–57k `[protect]` calls against ~13k elsewhere. [read: perf/diner_1440.md]
- **Unknown:** how many of those faults are sequential writes to clean, buffer-only pages. gfx_wait.md's step 1 (log the fault addresses) answers it.
- **Negative result to keep:** seth tried and rejected "hot pages" (never re-protect pages written often, upload them at every bind): the set grew to ~14k pages and the frame rate fell to 33 FPS with a ring timeout (`docs/parallel_gpu.md:71-73`, `:201`). That was our fix-2 alternative ("leave such ranges untracked and upload them at each GPU use"). [seth's claim]
- **Port it with seth's mask** (`mask & ~gpu[index]`), not with our `MarkRegionAsCpuModified`, which ignores GPU state.
- **Verdict:** portable with small rework. S, medium risk.
- **Tests:**
  - an A/B of fault counts and the fluid blocked median on `m5_np_route`;
  - the `[protect]` counters, "walked" included: a 256 KiB range spans allocations more often;
  - screenshots of the fluid scenes, the Pod, the Introduction fog and Needlepoint;
  - the Thruster recipe (ticket 10 timing).

**2b. Draw-prep workers (+11.5%) and the two-stage draw pipe (+19–32%)**
- **Seth:**
  - A scanner replays register writes per command buffer.
  - Workers pre-select pipelines and sharps.
  - A stage B thread owns the caches and records.
  - It needs fault-recovering speculative reads (`runtime_fault_recover`, `seth:src/runtime_memory.c`), thread-local register and user-data snapshots, drains, and recorder fences.
  - The launcher turns it on by default with 8+ threads (`seth:launcher/lbp3_launcher_win.py:215,844`). [read]
  - It once corrupted the guest heap (the GPU-idle IRQ came before deferred fences); fixed 2026-10-01 (`docs/parallel_gpu.md:549-555`). [seth's claim]
  - LBPPC, another lineage, turned its own draw pipe off for LBP3: "corrupts memory" [read: research/lbppc_graphics.md §3].
- **Ours:** our GPU thread isn't the limit. In fluid frames the processor reaches the wait 7.6 ms into a 33 ms frame. [read: gfx_wait.md]
  - The one coupling is the ~1.3 ms per fluid frame that armed compute waits behind graphics recording, and whether faster recording shortens fluid frames is open.
- **Verdict:** not worth it. L effort and high risk for an unproven gain.
- **Worth borrowing:** the metric "GPU thread µs per draw" (`docs/parallel_gpu.md:244-256`), for when we profile the processor.

**2c. Texture description cache and the other per-draw memos**
- **Seth:**
  - A 4096-entry direct-mapped cache of `ImageDesc` keyed by T# plus 3 flags, with memoized `FindImage`/`FindView` (`seth:.../vk_rasterizer.h:424-445`).
  - Render-state memo, +9%; texture-set memo, +5.6%. [seth's claim, base game]
  - They're woven into seth's `BindTextures` (the stencil redirect, scene proxies, upscaler redirects, prepared sharps).
- **Verdict:** entangled. M–L to rework onto ours. Only worth it if we profile the processor and find binding hot.
- **Open items:** possibly fluid frames, via the processor's recording. Unproven.

**2d. Pipeline binary cache, background warm-up, stage cache**
- **Seth:**
  - **On-demand mode:** `VK_KHR_pipeline_binary` with no bulk warm-up (`seth:.../vk_pipeline_serialization.cpp:51-190,1011-1016`; `vk_presenter.cpp:169-171`).
  - **Otherwise a background warm-up:** one reader, then builds on `min(cores−2, 6)` threads. Results go to the GPU thread 64 at a time every ≥ 4 ms, and the first pipeline ready for a key wins (`vk_warmup_inbox.h`; `vk_pipeline_serialization.cpp:1603-1790`). [read]
  - F77 stage cache: translated stages stored as self-checked records (`:1018-1060`).
- **Ours:**
  - A blocking parallel preload, then the driver `VkPipelineCache` file.
  - Boot to the Pod is ~5.0 s at 1440p, 3 s of it the serial read [read: pipeline_warmup.md]; `fb501343` reads ahead.
- **Verdict:**
  - Background warm-up: portable with rework (M). It hides ~3–5 s of boot.
  - Pipeline binaries: M–L. They need `VK_KHR_pipeline_binary` on the user's NVIDIA driver (not checked).
  - Stage cache: not needed. Ours already keeps SPIR-V.
- **Open items:** boot time only, not first-visit pauses.

**2e. Async compiler vs our LBPPC-derived async pipelines**
- **Seth:**
  - Translation and the pipeline build both run on workers.
  - **The policy is the valuable part** (`seth:.../vk_pipeline_cache.cpp:838-934`):
    - **perframe** (the default via F110): these draws **wait** for their pipeline:
      - full-screen passes (≤ 12 indices) and indirect draws;
      - passes into targets not drawn in each of the last 8 frames;
      - any frame with < 300 draws (loading screens, menus).
    - **F110 BAKEWAIT:** draws that must wait also wait for their shader stages. Under "skip all", "the Pod's decoration (orange walls, floor scale) came up as bare cardboard after some boots" (`:847-862`). [seth's claim, LBP3]
    - **F111 THUMBWAIT:** everything waits while the Pod save thumbnail or a photo is rendered (`vk_rasterizer.cpp:2595`, `vk_pipeline_cache.cpp:916-920`). [read]
- **Ours:**
  - `84e058af` sets up the pipeline on the GPU thread and builds it on 1–4 workers, but **skips every draw** until it's ready (`shadPS4:src/video_core/renderer_vulkan/vk_pipeline_cache.cpp:607-720`). [read]
  - Our own design note asked for exactly seth's block list (compile_stalls.md table row 1; first_visit.md "draw-skipping option"), and it was never built.
- **Verdict:** portable with rework. S–M.
  - Add `NoteDrawTarget` + `AsyncPolicyWaits` at our `is_new` branch: build synchronously when the draw must wait, and block while `g_lbp3CaptureQueued`.
  - Translation stays on the GPU thread: 2–3 ms, not worth moving [read: compile_stalls.md].
- **Risk:** low. It only waits more often than today's async.
- **Open items:** first-visit shader pauses. It makes `LBP3_ASYNC_PIPELINES` safe enough to offer by default.
- **Test:** the Pod right after a boot on an empty cache, against seth's cardboard report.

**2f. Warm-up inbox:** covered by 2d. A small header (`seth:.../vk_warmup_inbox.h`, 64 lines, plus `tests/test_warmup_inbox.cpp`). Portable with 2d, not alone.

**2g. Streaming stutter ("Stall:" lines)**
- **Seth's findings [seth's claim, base game, Linux]** (`docs/parallel_gpu.md:79-104`), item by item:
  1. Guest-to-staging copies on copy threads.
     - Ours: not applicable as-is (their recording architecture).
  2. **Staging blocks were trimmed between bursts**, and the first CPU touch of a new block cost ~2 ms per 16 MiB.
     - Their fix: 3,000 frames idle, keep 512 MiB, prewarm 8 blocks (`seth:.../vk_staging_buffer_pool.cpp:24-56`). The "3 s" they saw was 300 frames at the base game's ~100 fps.
     - Ours trims after 300 ring / 120 large-buffer ticks with no prewarm (`shadPS4:src/video_core/renderer_vulkan/vk_staging_buffer_pool.cpp:21-22,155-185`).
       - A tick is one guest `SubmitDone` (`Rasterizer::OnSubmit`, `amdgpu/liverpool.cpp:265-269`), i.e. one 30 fps game frame.
       - So ring blocks already live ~10 s and large buffers ~4 s. [read]
     - **Verdict:** prewarm plus a longer large-buffer life, and measure first (stall counts across a first level entry). S, low risk (+≤ 512 MiB host-visible); likely a small gain for us [inferred].
     - Windows also zero-fills newly committed pages on first touch [inferred].
  3. A 256 KiB fault window: 2a.
  4. File reads into write-protected pages failed.
     - Ours already reads through bounce buffers (`lbp3-recompiled:runtime/src/hle/kernel_fs.cpp:229,262`). **Already have.**
- **The "Stall:" diagnostic line** (`seth:.../core/libraries/videoout/driver.cpp:539-551`): per frame over 40 ms, it splits the waits into readback, GPU ticks, recorder, host copies, compiles and GC, plus fault-handler time. Worth copying the idea into our `[perf]` line (S).

**2h. Readback queue (`texture_cache/readback_queue.h`, `readback_sources.h`)**
- **Seth:** deferred image readbacks retire in order. A pending readback page denies all access, because "read denied with write allowed is an invalid protection on Windows" (`seth:.../page_state.h:29-33`, `tests/test_page_state.cpp`).
  - It's opt-in in the code (`LBP3_DEFERRED_READBACK`), but seth's launcher defaults turn on deferred and stale readbacks (`seth:launcher/lbp3_launcher_win.py:146`). [read]
- **Ours:**
  - Readbacks are off (reuploads.md); captures download only while one is queued (`50b4720a`).
  - Our `PageState::Perms` would give write-only on a read watch (`shadPS4:src/video_core/page_manager.cpp:55-57`), with no live consumer since readbacks are off. [read]
- **Verdict:** not worth it. Note the write-only trap if readbacks are ever turned on.

**2i. Other LBP3-specific items worth knowing**
- **F81 FILLMASKED (on by default):**
  - A partial-mask fill (the scene-alpha fill `ff000000`) is applied directly to the image with `host_shaders/mask_fill.comp` over an R32UI view (`seth:.../vk_rasterizer.cpp:2628-2650`). [read]
  - It is our reuploads.md "F3" (not done): it removes the last per-frame re-upload (~0.3 ms at 1080p) and its pre-sync, and the cost grows with resolution.
  - F80 FILLKEEP's comment puts the 4K cost of two such re-uploads at "~1 ms GPU" (`texture_cache.cpp:894-900`). [seth's claim, LBP3 4K]
  - **Portable with rework, M.** The dark-Pod fill path needs the Pod screenshots and regression.
- **F79 VRS:**
  - At ≥ 2560-wide targets, Sackboy's G-buffer pixel shader `4bf69d6a` and fuzz shells `4299ba39` shade at 2×2: "72.7 M fragments / 3.9 ms" and "197 M / 1.8 ms" at 3840×2160 close up (`seth:.../renderer_vulkan/lbp3_vrs.h:1-45`). [seth's claim, LBP3 4K]
  - It is a pipeline-key bit plus `VK_KHR_fragment_shading_rate`. Texture detail and alpha-test edges go per block.
  - **Portable with rework, M, opt-in.** Touches 4K cost. At 1440p, 2×2 is likely too coarse [inferred].
- **F78 early-Z:** off by default; seth found visible stacked layers, not hidden pixels. **Not worth it.**
- **B1/B3 bloom at non-1080p:** at render sizes other than 1080p, the bloom source's 480/270 dither falls off the pixel grid, giving vertical stripes (the Introduction's blue thought bubble) (`seth:.../shader_recompiler/frontend/render_scale.h:43-84`, `backend/spirv/emit_spirv_image.cpp`). [seth's claim]
  - Ours scales the 480×270 targets with the size and rescales `33c89263`'s literals (`shadPS4:src/common/lbp3_res.h:172`), so the dither stays 4×2 pixels [inferred].
  - **Check:** look at the Introduction's blue bubble at 1440p (a minute of game time).

---

## 3. Render and output resolution template vs `res_patches.cpp`

- **Seth's template:**
  - The base `0x400000` is added to eboot offsets (`seth:patches/LittleBigPlanet3.xml`).
  - Scale rules in `seth:scripts/patches.py:266-346`: `w`/`h` floor, `area` ×9/8 in MiB steps, `heap:N` +N MiB per extra 1080p of pixels in 2 MiB steps. Width must be a multiple of 8, height even.
  - The display buffer stays at 1920×1080 unless "Output Resolution" is applied: the game's last pass supersamples into it. [read]
- **Covered the same by both:**
  - the target declarations in `0x427940`, including the stacked 1920×2160 pair;
  - `0x426dc0` (SMAA) and `0x424790` (full-screen pair), both excluding the `0x1e00` stencil-op immediates;
  - the region `0x15180`;
  - the display width, height and pitch (`0x5bf85/93/a0`, ours `0x5bf83/90/9e`, the same instructions).
  - Ours: `lbp3-recompiled:runtime/src/core/res_patches.cpp:75-156`. [read]
- **In seth, not in ours:**
  - **Heap scaling** `0x45bf56` (eboot `0x5bf56`, `heap:320`): Garlic 1,698 MB +320 MiB per extra 1080p frame of pixels, so ~1,947 MiB at 1440p and 2,658 MiB at 4K.
    - Ours resizes the pool only with `LBP3_RES_POOL_MB` (`res_patches.cpp:393-421`).
    - Our 4K route loaded with no allocation failure [read: resolutions.md], and 2.5 GB of direct memory is unused [read: res1440.md].
    - **Portable as-is (S):** a safety margin for long 4K sessions.
  - **Screen-space refraction pixel size** (`0x42e829`, `0x42e840`, `0x430ce3`, `0x430cfe`) and **ScriptRenderToTexture** viewport and scissor (`0x46d2ae`, `0x46d2ba`): these redirect reads of the display-size globals to the render-size immediates.
    - They're needed only because seth keeps the display at 1080p.
    - **Checked:** each site's address + 4 + its `Original` displacement lands on `0x1202b38` (width) or `0x1202b3c` (height), the display globals. Each new value lands on `0x427a14` or `0x427a19`, the immediates of the target declarations at `0x427a13`/`0x427a18`. [read, computed]
    - Our display globals follow the render size (`0x5bf83/90` patched); `0x46d250` is listed in res1440.md's table A as a reader. So in ours these reads already give the render size. [read]
    - **Not applicable** while display = render.
    - **Needed if** we ever add "render above output": e.g. render 4K, output 1440p, which would cut our per-frame readback and upload, the 4K cost lever in lbppc_vs_open_items.md §4.
- **In ours, not in seth:**
  - the float pairs (distortion texel scale, UI snap grid, NDC→pixels);
  - aspect, canvas and centres (ultrawide);
  - the 480×270 targets growing with size;
  - the capture-chain rule.
  - Seth leaves the 1280×720 chain and the 480×270 targets at PS4 sizes at every size.
- **Photo-streak fix (v0.1.4):** seth also never scales the 1280×720 / 640×360 / 320×180 chain, so no 16-pixel row lesson is needed there. That agrees with our rule (capture chain at PS4 sizes; multiples of 16 only when widened on ultrawide). Nothing to take.
- **Effect constants:** seth relies on render-scale literal swaps for 6 programs (SMAA ×5, `fdf2b5d5`) plus `33c89263` and `104fa781` (`render_scale.h:86-104`). All but `104fa781` are already in our 17 (`shadPS4:src/common/lbp3_res.h:172-176`). Nothing new for 1440p.

## 4. Intel 12th-gen and newer crashes

- **Nothing on the GPU side.** I grepped `gpu/shim`, `gpu/shadps4/{common,video_core}` and `gpu/CMakeLists.txt` for AVX, affinity, CPU sets, hybrid, E-/P-core, Alder or Raptor. [read]
  - The only hits are `lbp3_threads.h` (counting the affinity mask; `THREAD_PRIORITY_IDLE` helpers on Windows) and Intel GPU-driver workarounds in `vk_instance.cpp:207,981`.
  - Seth's docs say "no AVX-512" and "no -march" (`docs/parallel_gpu.md:56`; LTO notes), which is about portability, not a crash fix.
- **Verdict:** nothing to take for this item from this lane.

## 5. Conflicts and cheap checks (ticket 17 first)

1. **Stacked targets ON (ours) is right; seth's OFF isn't the PS4's behaviour.**
   - **Seth's own comment:**
     - on the PS4 the 16 tiles and the 512×4096 texture "is just memory";
     - with the copy off, "the volume held the first 4 of its 64 slices (the rest stale), and the lit fog/smoke ended up at one end of the volume" (`seth:.../texture_cache.cpp:2244-2256`). [read]
   - **So ON is faithful by construction.** No reading of the code makes OFF what the PS4 does [inferred from read].
   - **Seth turned it off for the look:** with ON, Manglewood walls are "21–26% darker", the neon, lamp and orb glow gone (`:2257-2258`). [seth's claim]
     - That is a third scene, after our WELCOME and Needlepoint (`37508fd9`), where the faithful volume is darker. It corroborates ours.
   - **Implementation differences:**
     - Seth's eligibility is stricter: tiles after the first only, synced only when written since the last sync, rows a multiple of 64, tile and array modes equal (`seth:.../texture_cache.cpp:2268-2350`).
     - Ours copies every eligible part on every bind, the first included (`shadPS4:src/video_core/texture_cache/texture_cache.cpp:994-1050`).
     - Seth's dirty-since-last-sync rule would cut our per-bind copy cost (8 MB per bind) [inferred].
   - **Optional confirmation:** `LBP3_STACKED_TARGETS=0/1` at the Manglewood hub against ticket 17's PS4 still `hq2` (Thejollymage).
   - **For ticket 17:** the volume path is where a faithful fix moved brightness, so other volumes and scenes stay the most concrete lead [inferred].
2. **F91: `s_and_b64 exec` opens no divergence scope in ours. Unscanned; the scan decides.**
   - Upstream treats only SAVEEXEC, ANDN2 and CMPX as scope openers. So VALU code after `s_and_b64 exec, s[..], s[..]` runs unmasked.
   - **The evidence is one UI shader:** LBP3's power-up wheel `79f2fc54` (a sign lost under an unmasked `atan2` fix-up) (`seth:.../shader_recompiler/frontend/control_flow_graph.cpp:164-250`). [read; seth's claim on the shader]
   - Seth deliberately leaves out the common discard sequence (`s_and_b64 exec, exec, s[live]; s_wqm_b64 exec, exec`), so most pixel shaders don't change.
   - Ours: `shadPS4:src/shader_recompiler/frontend/control_flow_graph.cpp:162`, the upstream logic. [read]
   - It matters for ticket 17 only if lighting or material shaders use the non-discard form [inferred].
   - **Check:**
     - count the pattern (dst EXEC, sources not EXEC, no following `s_wqm_b64 exec`) across all 135k shaders with the `tools/res1440_fullscan.py` decoder (S);
     - if lit-scene shaders have it, run the WELCOME A/B on a fresh cache copy.
3. **The P6, `#5346` and `eb51f01` fixes** (rsq legacy 0→0, log, rcp and rsq clamps, SNORM `max(−1)`):
   - These fix ∞, NaN and −1.002/−2.0 edge values, which don't predict a uniform 1.3× mid-tone lift.
   - Port them as one cheap correctness bundle with one cache rebuild (salted to the affected programs where the scan finds them). They're not a probable cause.
   - SNORM 2_10_10_10 W = −2 (a bitangent sign) matters only where the shader unpacks it itself. Hardware vertex fetch clamps [inferred].
   - Cited: `seth:.../emit_spirv_bitwise_conversion.cpp:234-239`, `vector_alu.cpp:1056-1095`; ours `shadPS4:src/shader_recompiler/backend/spirv/emit_spirv_bitwise_conversion.cpp:229-235`, `vector_alu.cpp:172-191`. `#5346` is the only upstream PR in seth's list we lack (our fork has all of `#5030`–`#5285` they cite).
4. **Scaled MIN/MAX blends** are ruled out on ours.
   - Seth replaced upstream `#4768`'s squared-output emulation, which isn't exact for repeated blends, with `vk_blend_rewrite.h` (from cuesta4/eltutz).
   - But our logs never print upstream's "Emulating scaled min/max blend" warning (0 in `lbp3-recompiled:analysis/*_stderr.txt`, where shadPS4 warnings do appear). So LBP3 doesn't hit that path. [read]
5. **553bcf9 detector** (§1). S.
6. **A second render of the same eboot.**
   - Seth's build runs the byte-identical eboot. A WELCOME shot from it, with `LBP3_B1RENDER_ATLAS=1` to match ours, would show whether the brightness is shared shadPS4 behaviour or ours.
   - That means running their binary: the user's call (untrusted).
   - Seth's F81 changes nothing in alpha semantics. Their 6f4b3cd deferral may still change when fills land, so read any difference with that in mind [inferred].

## Suggested order

| Step | What | Effort |
|---|---|---|
| 1 | The checks in §5: the F91 and opcode scan, the 553bcf9 detector, the 1440p blue bubble; optionally the Manglewood stacked-targets confirmation | about an hour offline plus ~30 min in game |
| 2 | The fault window (2a), with the fault-address log first; `7184f52`; `80ffa53`; the clear remap; staging prewarm only after a measurement | S each, one branch |
| 3 | The async policy (2e) on `LBP3_ASYNC_PIPELINES` | S–M |
| 4 | The recompiler bundle (F91 if the scan shows hits; P6, `#5346`, `eb51f01`) with one cache rebuild, salted to the affected programs if possible | S |
| 5 | F81 masked fill (our F3) | M |
| 6 | The heap scaling | S |
| 7 | VRS, opt-in, for 4K | M |
| 8 | Later or never: background warm-up, the memos, the draw pipe | — |
