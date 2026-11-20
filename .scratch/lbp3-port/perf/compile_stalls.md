# First-sight shader and pipeline compiles (read-only), 2026-11-12

Code read at `D:\Recomps\lbp3-recompiled`: branch `m5/verify` `2d92d65`, shadPS4 submodule `31071685`. Line numbers are at those commits; the audit's are shifted by the instrumentation commit. `video_core/`, `shader_recompiler/` and `common/` are under `third_party\shadPS4\src\`; `gpu/`, `hle/` and `shim/` are under `runtime\`.

Data:
- the cache folder `user\shader_cache\CUSA00473\`, with NTFS creation and modification times;
- `analysis\play_logs_20261111\`;
- `analysis\m5_*_stderr.txt` and `m5_*_split.csv`;
- `%LOCALAPPDATA%\NVIDIA\DXCache`.

Parsed with throwaway `python -I` scripts. Nothing was built, run or edited.

## Answers in brief

- **What stalls is the driver building the pipeline, not our translation.**
  - Translating a new GCN shader to SPIR-V and creating its module costs **2–3 ms**. This is measured, on the `[perf]` lines.
  - The Vulkan pipeline that first uses a new module costs **~46 ms** (median; p90 59 ms) in `vkCreateGraphicsPipelines`. This is inferred from file timestamps.
  - A new pipeline built only from modules already compiled (a new state combination) costs **~0.5 ms** (median).
  - A 0.5–1.4 s freeze is a burst of 10–50 such pipelines in one or two frames, on the command-processor thread, while the game's main thread waits in its submit.
- **Caches:**
  - Our cache (`user\shader_cache\CUSA00473\`) stores, per pipeline, the key, the SPIR-V and the metadata. It holds **2,155 pipelines** (2,143 graphics, 12 compute), **1,804 SPIR-V modules** from **1,534 shader programs**, 92 MB.
  - The Vulkan `VkPipelineCache` is **not** persisted.
  - NVIDIA's own disk cache (`DXCache`) holds the compiled machine code. It is what makes the warm-up fast.
- **Boot warm-up:**
  - Before the first frame, the warm-up rebuilds every cached pipeline, single-threaded, inside `sceVideoOutOpen`.
  - With NVIDIA's cache warm it takes **under 5 s** (a hard bound from relaunch gaps; my estimate is ~3 s; it isn't timed).
  - If NVIDIA's cache misses (after a driver update or a cache cleanup), the same loop would take an estimated **~75–100 s**.
- **A second visit has no compile stalls.**
  - 13 warm-cache runs of the Needlepoint route compiled **0 pipelines**, and their worst level-entry frames were 170 ms (the game's own main thread) and 100–137 ms (GPU and uploads).
  - Revisits in the play logs wrote no cache files, and none of their windows reached 300 ms.
  - Level-entry hitches of 0.15–0.33 s with no compiles remain. They come from other causes (the audit's C2).
- **Coverage:** 10 of the 46 story level archives have been played (all of the Prologue, plus the Manglewood hub and Diner). Each first visit today added **44–191 pipelines**.
  - At 30–150 per remaining archive, the 36 left would add ~1,100–5,400 pipelines. So **roughly 30–65%** of the story's pipelines are cached. The denominator is a guess, and the 2,155 also includes the Pod, menus and Create.
  - The remaining story's one-time freezes then add up to **~40 s–3 min**, spread over the rest of the game (~77% of new pipelines bring a new module, at ~46 ms each).
  - Create mode and community levels are open-ended: the game's archives hold about 13,200 material files.
- **Ranked fix:**
  1. Asynchronous pipeline creation on worker threads, skipping a graphics draw until its pipeline is ready (but blocking for compute, captures and persistent targets). Nothing else removes first-visit stalls on this machine.
  2. Cheap insurance, plus the timers needed to measure: a parallel warm-up and a persisted `VkPipelineCache`.
  3. The rest is small, or not for this machine.

## 1. What compiles on first sight, and where the time goes

**Path** (all on `shadPS4:GpuCommandProcessor`, the thread that records every queue):

- `Rasterizer::Draw` → `PipelineCache::GetGraphicsPipeline` (`video_core/renderer_vulkan/vk_rasterizer.cpp:243`). A null result skips the draw (`:244-246`). Dispatches go through `GetComputePipeline` (`:379`).
- `RefreshGraphicsKey` → `RefreshGraphicsStages` (`vk_pipeline_cache.cpp:418-636`).
  - For each enabled stage, `GetProgram` (`:688-738`) looks up the program by GCN code hash, then a **permutation** whose `StageSpecialization` matches the live state (`:714-721`). The specialization covers the runtime info (render-target formats, vertex-shader outputs, …), the buffer, image and sampler sharps, vertex formats, and the starting binding index (`shader_recompiler/specialization.h:79-243`).
  - On a miss, `CompileModule` (`:647-686`) runs `TranslateProgram` (`:662`) and `EmitSPIRV` (`:663`), then `vkCreateShaderModule` (`CompileSPV`, `:674`; `vk_shader_util.cpp:9-19`). It then queues the SPIR-V and metadata for the disk cache (`:677`, `:701`/`:726`). It also does a filesystem check for a shader patch each time (`:668`, `:792-810`), which is cheap.
  - The fragment stage is bound first (`:550`), and the vertex stage's binding numbers start after the fragment shader's.
- New key → `try_emplace` (`:363`) → the `GraphicsPipeline` constructor (`vk_graphics_pipeline.cpp:30-423`):
  - the descriptor-set layout and pipeline layout, which read guest sharps (`:44`, `:480-534`, `:66-73`);
  - auxiliary tessellation or discard modules for rect and quad lists (`:197-246`);
  - then **`createGraphicsPipelineUnique`** (`:417-418`), with the in-memory `VkPipelineCache`.
  
  Afterwards the key is queued for the disk cache (`vk_pipeline_cache.cpp:374`). Compute pipelines follow the same pattern (`:391-416`, `vk_compute_pipeline.cpp:109-110`).
- Disk writes run on `shadPS4:PipelineCacheIO`, one file per blob, written as soon as it is queued (`video_core/cache_storage.cpp:37-69`, `:140-167`). They don't block the command processor.

**Timing:**

| Step | Cost | How known |
|---|---|---|
| GCN → SPIR-V + `vkCreateShaderModule`, per new permutation | **2–3 ms** | Measured: `[perf]` lines on the instrumented builds (`hle/videoout.cpp:149`, counters at `vk_pipeline_cache.cpp:31-34`, `:650-657`). Every warm run shows 1–5 such compiles at 2–3 ms (`m5_*_stderr.txt`, `pace_m5_m1_intro*_stderr.txt`). |
| Pipeline whose module is new (driver compiles native code) | **~46 ms** median, p10 21, p90 59 (n = 1,669) | Inferred: the gap between a new `.spv`/`.meta` file and the next `.key` file in the cache folder. The I/O thread writes each file within ~0.5 ms of its queueing (`.spv`→`.meta` pairs are 0–0.5 ms apart). No instrumented run has hit a first-sight pipeline yet: all of them ran with a warm cache. |
| Pipeline from modules already compiled (new state only) | **~0.5 ms** median, p90 20 (n = 378) | Inferred the same way (`.key`→`.key` with no new shader between). NVIDIA reuses its compiled code across state. |
| Gap between a key and the next new shader | 2.5 ms median | Translation plus recording between the draws. |

**A stall is a burst.**
- `play_log.txt` 21:01:46.36–49.06 is one continuous run of **45 pipelines and 54 modules in 2.7 s** (~60 ms each). Keys come every 45–80 ms. It produced the 1,365 ms and 698 ms frames (`play_log.txt:6040`, `:6063`).
- Matched to `[video]` windows (window times summed backwards from each log's mtime, writes counted within ±2 s), the stalls had these new pipelines/shaders nearby:

  | Stall | Log line | New pipelines/shaders |
  |---|---|---|
  | 1,413 ms | `play_log.txt:2281` | 51/37 |
  | 1,365 ms | `play_log.txt:6040` | 46/55 |
  | 1,126 ms | `play_log.1.txt:9608` | 29/29 |
  | 1,067 ms | `play_log.txt:1702` | 23/21 |
  | 612 ms | `play_log.txt:1433` | 41/22 |

- At ~46 ms each, 10 new pipelines in a frame make about 0.5 s.

**Permutation churn.**
- 99 programs have more than one permutation, for 270 extra modules (15%). Six programs have 141 permutations between them (135 beyond their first): `0xd44ad72f3a6cfdca` has 35, `0x8af210a5…` 31, `0x828ca39e…` 27, `0x9d4e1cf1…` 22, `0x8957ec95…` 16, `0x03101c0e…` 10. The first five are vertex shaders (`analysis\shaders\dumps\vs_*`); the sixth isn't in the dumps.
- Their metadata shows 7–16 distinct starting bindings per shader. That is partly the fragment shader's resource count leaking into the vertex shader's key (`:550` binds the fragment stage first; `specialization.h` compares `start`). Other runtime-info fields differ too.
- Each new permutation is a full driver compile. The 21:01 burst includes `0x8af210a5e06f1b3e_25`…`_29`.

## 2. Caches on disk

| Cache | Where | What it stores | Persisted? |
|---|---|---|---|
| shadPS4 pipeline cache (the port's shim turns it **on**; upstream's default is off: `main:src/core/emulator_settings.h:486`) | `<cwd>\user\shader_cache\CUSA00473\`, i.e. `D:\Recomps\lbp3-recompiled\user\shader_cache\CUSA00473\` (`gpu/shadps4_glue.cpp:71-77`; folder named by the serial set at `gpu/renderer.cpp:104`; `video_core/cache_storage.cpp:113`) | <ul><li>`profile.bin` (64 B, device profile)</li><li>`g_*.key` / `c_*.key`: the pipeline key plus vertex-input, multisample and auxiliary tessellation SPIR-V (`vk_pipeline_serialization.cpp:22-58`)</li><li>`<perm hash>.meta`: the specialization and shader info (`:60-82`)</li><li>`<hash>_<perm>.spv`: the SPIR-V (`:84-92`)</li></ul>Loose files: the zip mode is off (`shim/core/emulator_settings.h:55`). | Yes. Written immediately, so it survives `taskkill /f` (an in-flight write at the kill could leave a truncated file; unseen). `PipelineCache::Sync`/`DataBase::Close` (`:380-382`) are never called. `LBP3_NO_SHADER_CACHE=1` turns off both load and save (`shim/core/emulator_settings.h:54`, `vk_pipeline_serialization.cpp:305-307`). |
| Vulkan `VkPipelineCache` | in memory | created empty at `vk_pipeline_cache.cpp:350-353`, **after** `WarmUp()` at `:348`, so warm-up pipelines are created with a null cache handle | **No.** Nothing calls `getPipelineCacheData` anywhere in the tree. |
| NVIDIA driver shader cache | `%LOCALAPPDATA%\NVIDIA\DXCache` (40 GB in total). The `e1efa9dd*.nvph` group (59 files, 1.08 GB, last writes 21:03 and 23:41, when lbp3 runs ended) is most likely lbp3's. | native code for each pipeline compiled | Yes, by the driver. A driver update invalidates it (and Disk Cleanup's "DirectX Shader Cache" probably empties it; unverified). Its size setting wasn't read. |

**Cache state at 2026-11-12 00:00:**
- 5,764 files.
- Created by day: 11-09: 586 modules / 631 keys; 11-10: 268/294 (00h), then smaller batches; 11-11: 13h 232/334, 20h 331/418, 21h 75/65.
- No `stale pipelines`, `conflicts with index` or profile-mismatch warning in any log, so every key preloads.
- **Version hazard:** upstream `main` has since bumped `ShaderBinaryVersion` 7→11 and added two `Profile` fields (`fast_shader_math`, `support_bda`). Rebasing the submodule would make every cached entry stale, or ignore the folder (profile size check, `vk_pipeline_serialization.cpp:323-338`), and bring every compile stall back once. The port hasn't changed the recompiler since the cache was created (11-09), so the cache is consistent now.
- **Retranslated every run, explained:**
  - 18 `.meta` files are referenced by no `.key`. All 11 `.spv` files whose modification time moves on each run are among them (e.g. `0x221ff5dcbbbe9151_1`, `0x4299ba393ac46b7d_0`; fragment shaders per the dumps).
  - These are shaders translated for draws that never get a pipeline. The fragment stage is bound, then one of the silent `return false` paths in `RefreshGraphicsStages` (`:528-539`, `:566-611`) drops the draw.
  - So they are never preloaded, and they are retranslated once per run: the 1–5 compiles of 2–3 ms with 0 pipelines on the `[perf]` lines.
  - Harmless for stalls. Which draws these are, and whether they're visibly missing, is unknown.

## 3. The boot warm-up

**What it does** (`PipelineCache::WarmUp`, `vk_pipeline_serialization.cpp:304-378`; called from the `PipelineCache` constructor, `vk_pipeline_cache.cpp:348`, inside `new Vulkan::Rasterizer` at `gpu/renderer.cpp:110`, from `gpu::Init`, from `sceVideoOutOpen`, `hle/videoout.cpp:204-212`). It runs **on the game's calling thread, synchronously, before the first frame**:
1. Opens the folder and starts the I/O thread (`cache_storage.cpp:90-121`).
2. Compares `profile.bin` with the live `Shader::Profile`, byte for byte. A mismatch ignores the whole cache (`:312-338`).
3. Iterates the folder and reads every `.key` (`cache_storage.cpp:231-260`).
4. For each key, per stage:
   - loads `.meta` and `.spv`;
   - calls `vkCreateShaderModule` once per program or permutation (`:248-302`);
   - inserts the permutation at its index (`vk_pipeline_cache.h:59-62`).
5. Builds the layouts and calls `vkCreateGraphicsPipelines`/`vkCreateComputePipelines` with `preloading=true` (`:140-170`, `:208-246`). This is one at a time, with a null `VkPipelineCache`, relying on NVIDIA's disk cache.
6. Logs "Preloaded N pipelines" (`:371`), at Info, which our log filter drops.

**How long it takes: not timed.**
- Bound: in the route runs launched back to back, the last frame of one run is followed by the first frame of the next after **6.99–7.89 s** (7 pairs in `m5_ab_*`/`m5_wg_*_split.csv`; the other four gaps were 27–1,913 s, pauses between batches).
- That interval includes `taskkill`, two scripted 1 s sleeps, the save restore, PowerShell start, process and game start, and Vulkan device creation.
- So the warm-up of 2,155 pipelines takes **under 5 s** (hard bound, ≤2.3 ms per pipeline), and probably ~3 s (estimate). That is only possible if the driver's cache hits.
- **With a cold driver cache:** ~1,670 pipelines × ~46 ms ≈ 77 s, plus layouts and I/O. That is **~1.5 minutes of black window** on the first boot after an NVIDIA driver update. This is an estimate, never observed.

## 4. Does a second visit stall?

- **Same session:** pipelines and modules live in `graphics_pipelines` and `program_cache` and are never evicted (`vk_pipeline_cache.h:131-133`). A revisit compiles nothing.
- **Later sessions:** the warm-up preloads every key, so a level compiles only what it has never shown before. Evidence:
  - 13 warm runs of the Needlepoint route (`m5_split1`, `hyp1`, `flush_smoke`, `ab_*`, `wg_*`): 0 pipelines per run.
    - Entering the level, the worst frames are 168–171 ms with the main thread at 166–169 ms (the game's own load) and 102–137 ms with the GPU wait at 48–86 ms (first-frame GPU and uploads). These are frames 1613–1631. They are tied to the level by the `needlepoint_peaks.farc` open between the 1590 and 1620 status lines (`m5_ab_off1_stderr.txt:777-811`).
    - The boot's own 3 s, 2 s and 2 s frames are main-thread game work (`main_cpu_us` 3,010 ms).
  - `play_log.1.txt`: Needlepoint Peaks (3.9 min) and Stitchem Gate (1.4 min) had been seen before. They wrote no cache files. Worst window 168 ms; no window reached 300 ms.
  - `play_log.3.txt`: Needlepoint Peaks, mostly cached (28 new pipelines in 8 min). Its entry windows of 154–328 ms had **no** cache writes, so they aren't compiles. Its only compile windows were 170–233 ms.
- **What remains on a warm visit:** level-entry frames of 0.15–0.33 s from the game's load on main, image creation, uploads and lighting-volume expansion (audit C2). Those are separate from this ticket.

## 5. How much of the game is cached

- **Numerator:** 2,155 pipelines, 1,804 modules, 1,534 programs.
- **New pipelines per first visit** (new modules in brackets):

  | Level (log) | New pipelines |
  |---|---|
  | Stitchem Manor (`play_log.1`) | 47 (43) |
  | Stitchem Boss | 44 (32) |
  | Titan Released | 102 (80) |
  | Manglewood hub (`play_log.txt`) | 140 (121) |
  | Diner | 191 (167, over 28 min, with mid-level bursts at minute 25) |

  A new world brings new shaders: the Manglewood hub needed more than the Prologue levels did.
- **Played:** 10 of the 46 story level archives (`gamedata\lbp3\official_adventure_levels`): all 8 of the Prologue's, and the hub and Diner of Manglewood's 12. Ziggurat (13) and Bunkum (13) are untouched. Many of the 36 left are cutscenes (`cNN`) or side levels (`qNN`).
- **Estimate:** 30–150 new pipelines per remaining archive gives ~1,100–5,400 more, so **roughly 30–65% of the story is cached**. The denominator is a guess, and the numerator also counts the Pod, menus and Create mode.
- **What that costs:** about 77% of new pipelines bring a new module (1,669 of 2,155 so far), at ~46 ms each. That makes **~40 s–3 min of one-time freezes** over the rest of the story.
- **Open-ended content:** Create mode and community levels draw from about 13,200 `.gmat` material files in the game's archives (`output\orbisguids.map`).

## 6. What upstream shadPS4 and other emulators do

**shadPS4.**
- Local `main` (`0cd6e4f2`, 2026-10-28) has exactly what the port runs:
  - synchronous compile on the command-processor thread;
  - the key, metadata and SPIR-V disk cache with the boot warm-up (#3816, 2025-11-29, `pipelineCacheEnable`/`pipelineCacheArchive`, both off by default);
  - no asynchronous compile, no `VK_EXT_graphics_pipeline_library`, no shader objects, no persisted `VkPipelineCache` (`git grep` on `main`).
- The open pull requests on GitHub, checked 2026-11-12, include none for asynchronous or background compilation. Related open ones:
  - #5583: don't preload pipelines whose layout depends on live image descriptors (the dynamic-index mip fallback). It says this "can lose the device on a warm cache". The fallback applies on NVIDIA, which lacks `VK_AMD_shader_image_load_store_lod`. Whether LBP3 uses it is unknown.
  - #5155: preloaded permutations' user data.
  - #5577: make `num_requests` atomic. It is a plain `u32` touched from two threads in `cache_storage.cpp:25,50-66,164`.
  - #5654 and #4929: profile-mismatch handling.

**Other emulators** (prior art, from memory; not fetched or verified):
- **Asynchronous compile with draw skipping:** yuzu-family, Cemu, Xenia, Dolphin's "skip drawing" and RPCS3's asynchronous mode. New pipelines compile on worker threads, and draws that need them are skipped until they're ready. This is the standard cure for first-sight stutter, at the cost of objects missing for a few frames.
- **Interpreters while compiling:** RPCS3's shader interpreter, Dolphin's ubershaders. Not feasible for arbitrary GCN code.
- **Parallel boot preload with a progress screen, and a persisted `VkPipelineCache`:** yuzu, Ryujinx, RPCS3, Cemu.
- **DXVK 2.x:** `VK_EXT_graphics_pipeline_library`, compiling shader stages when the game creates them and fast-linking at draw time. On PS4 there is no shader-creation call to hook: a shader is just memory that a draw points at, and its SPIR-V depends on draw-time state.

## 7. Options, ranked

0. **Instrument first** (needed by every measurement below):
   - time the warm-up (count and ms on a `[gpu]` line);
   - log each first-sight compile with frame number, stage, hash and ms, split into translation and pipeline;
   - add a skipped-draw counter.
   
   Small, no risk.

| # | Option | Gain | Risk | Effort | Measurement that shows it worked |
|---|---|---|---|---|---|
| 1 | **Asynchronous pipeline creation, skipping graphics draws.** Keep translation (2–3 ms) on the command-processor thread, since it reads live registers and guest memory. Build the create-info there too (`vk_graphics_pipeline.cpp:30-416` reads sharps, `infos` and `runtime_infos`). Move only `createGraphicsPipelineUnique` (`:417`) to a pool of 4–6 workers (12 cores here). Keep a "pending" state in `graphics_pipelines`. Return null while pending; the existing early-out skips the draw (`vk_rasterizer.cpp:244-246`). Write the key when the pipeline is done. **Block instead of skipping** for:<ul><li>compute dispatches (12 keys, rare);</li><li>draws while `g_lbp3CaptureQueued` is set (photos and icons go to saves; `texture_cache.cpp:35,87`);</li><li>full-screen, composite and post passes, and draws into the display buffers;</li><li>passes into targets that persist across frames.</li></ul>Optionally wait up to a few ms before skipping. | Removes the ~46 ms per pipeline from the critical path, the bulk of every 0.5–1.4 s stall. Bursts become 1–3 frames with some draws missing, plus 2–3 ms per new shader still in-frame. What's at stake: ~40 s–3 min of one-time freezes over the remaining story, plus every first sight in Create and community levels. | **Visual.** A skipped draw is not always an object. Fast-clear eliminates and resolves bypass pipelines (`FilterDraw`, `vk_rasterizer.cpp:60-75`), but the game's full-screen passes (post, composite, shader clears) are ordinary draws through `GetGraphicsPipeline`. Skipping one gives a frame without post-processing, a stale or black frame, or stale depth under later draws. Hence the block list; which LBP3 draws fall in it is unknown. Persistence across frames needs checking too: the lighting accumulators are zeroed each frame per NOTES, shadow maps are per frame, captures are blocked. **Determinism:** game logic doesn't read GPU output except captures (blocked), so steps per frame and saves are unaffected. **Threading:** Vulkan pipeline creation is thread-safe; the cache I/O ordering must move with it. | Medium: ~200–400 lines in the shadPS4 fork (`vk_pipeline_cache.cpp:358-416`, the pipeline constructors split into prepare and create, a worker pool), behind an env switch for A/B. | On a level never seen by **both** caches (see "How to measure"): worst frame at entry and mid-level falls from 0.5–1.4 s to ≲0.2 s (the residual entry hitches). `[perf]` shows pipelines compiled on workers, the skipped-draw count, and how many frames skipped. Screenshots during the first 3 s after entry, to judge artifacts. Then milestone 1's checks, the photo checks and the Thruster recipe. |
| 2 | **Parallel warm-up**: create modules serially, then pipelines on N threads. Optionally start it at process start rather than inside `sceVideoOutOpen`. | None while the driver cache is warm (under 5 s now, ~3 s estimated). After a driver update or cache wipe, ~1.5 min → ~10–15 s. | Low. Preload order already tolerates any order; keep `InsertPermut` (`vk_pipeline_cache.h:59-62`) on one thread. | Small. | Warm-up timer with the driver cache warm, and with it cold (NVIDIA's shader cache disabled for `lbp3.exe`, or after a driver update). |
| 3 | **Persist `VkPipelineCache`**: create it before `WarmUp` (swap `vk_pipeline_cache.cpp:348`/`:350`), load it from disk, save it periodically (runs end with `taskkill /f`). | ~0 on NVIDIA with its disk cache on. Insurance if that cache is disabled, evicted or size-limited. A driver update invalidates it too. | Low. Reject it on a header mismatch, which Vulkan reports. | Small. | Warm-up timer with NVIDIA's cache disabled: with the file vs without. |
| 4 | **Fewer vertex-shader permutations**: number the vertex shader's bindings independently of the fragment shader's, or bind the vertex stage first. | At most ~15% fewer driver compiles (270 of 1,804 modules are extra permutations). The six worst programs have 10–35 permutations each, and the starting binding is only one of the fields that differ. | Medium: the descriptor layout changes, and every cached entry goes stale once. Correctness needs a full visual pass. | Medium-high. | The same first visit: count new permutations per program and pipelines compiled, before and after. |
| 5 | **Harvest ahead**: scripted visits to levels before the human plays them, as with `m5_np_route.sh`. | Converts the human's first visit to a warm one, for what the script actually draws. | None visually. Coverage is partial, and later levels need a save that unlocks them (the story test save reaches the Prologue and Manglewood). | High per level. | That level's first human visit: `[perf]` compiles near 0. |
| 6 | **`VK_EXT_graphics_pipeline_library` or shader objects** | Low: state-only pipelines already cost ~0.5 ms. The 46 ms is new native code per new SPIR-V, which these don't avoid, and the SPIR-V itself depends on draw-time state. | Medium. | High (not in shadPS4). | Not recommended. |

Also not worth it: caching translation further (2–3 ms per module, already cached), or shipping a pre-built cache. That doesn't help this machine, whose cache already persists. Elsewhere it needs a matching `profile.bin` and turns into the cold-driver warm-up of option 2.

## How to measure (the confound)

**NVIDIA's disk cache confounds any first-visit vs second-visit test.** Deleting or disabling our cache (`LBP3_NO_SHADER_CACHE=1`) does **not** recreate a first visit: the driver still has the native code, so each pipeline costs ~0.5–1.5 ms, not ~46 ms. That run is still useful: it isolates translation cost. To get a real first sight, use one of:
- **A never-visited level.** 36 story archives are left, but each one is "first" only once per machine. That's poor for alternating A/B runs; use it for the final human-facing check.
- **NVIDIA's shader cache disabled for `lbp3.exe`**, in its driver profile, plus `LBP3_NO_SHADER_CACHE=1`. Every pipeline is then cold on any level, every run, so the Needlepoint route works for A/B. This changes a setting on the human's machine; ask first.
- **Possibly:** an env switch that salts every SPIR-V module (e.g. a debug string), so the driver's cache misses while ours is off. Unverified whether NVIDIA's cache key changes with it.

**Success for option 1:** on cold pipelines, the entry window's worst frame falls from 0.5–1.4 s to the warm-cache level of ~0.17–0.2 s, with a skipped-draw count above 0 and no capture-time skips. Then a never-visited story level with the human.

## What stays unknown

- The warm-up's real duration: under 5 s (hard bound), ~3 s (estimate), not timed. Its duration with a cold driver cache is an estimate (~1.5 min).
- Which of LBP3's draws are full-screen, composite or display-buffer passes (option 1's block list), and how a skipped one would look.
- The per-pipeline cost is inferred from file creation times, not timed in code. It assumes the I/O thread writes promptly, which the 0–0.5 ms `.spv`→`.meta` spacing supports.
- Which `DXCache` files are lbp3's, and NVIDIA's cache size limit on this machine.
- The total number of pipelines in the story (the denominator), and so the cached share.
- Which LBP3 passes write targets that persist across frames: these must block, not skip, under option 1.
- Which draws the 18 unreferenced shaders belong to, and whether those draws are visibly missing every run.
- Whether LBP3 hits shadPS4 #5583's preload layout issue (the dynamic-index mip fallback on NVIDIA).
- Upstream after 2026-10-28: only the pull-request list was checked; nothing was fetched into the clone.
