# Parallel pipeline warm-up and a persistent driver cache (fix/pipeline-warmup)

Ported from LBPPC (emurphy04, https://github.com/emurphy04/LBPPC, commit `aad6a91`), files marked GPL-2.0-or-later (shadPS4-derived, with their "bbport:" changes). Credited in the commit messages and in a comment at each ported block. Nothing was taken from their GPL-3.0 or unmarked files. Background: [research/lbppc.md](../research/lbppc.md) (licence and their shader approach) and [shared_cache.md](shared_cache.md) (our boot grew to about 5.3 s at 1440p with about 4,000 cached keys).

## What changed

Branches: shadPS4 `fix/pipeline-warmup` (from `port` `553be3ba`, worktree `D:\Recomps\lbp3-wt\pwarm-shadps4`) and runtime `fix/pipeline-warmup` (from `port` `34e3e8d`, worktree `D:\Recomps\lbp3-wt\pwarm`).

1. **Parallel preload.** `PipelineCache::WarmUp` still reads every cached key and its stages one after the other: that part loads SPIR-V into shader modules and fills `program_cache`. Each pipeline then goes into a `PreloadJob` instead of being built on the spot, and `BuildPreloaded` builds the jobs on `cores − 1` threads (at most 16). That's where nearly all of the preload's time goes: per LBPPC's measurements, the driver's pipeline build is about 96% of a pipeline's cost.
   - `LBP3_PIPELINE_THREADS=n` sets the thread count; `1` runs the old serial preload, the exact old code path.
   - Log line: `[gpu] pipeline preload: N of M cached pipelines in X s on T threads`.
2. **The driver's `VkPipelineCache` is saved to disk** as `user\shader_cache\<serial>\driver.vkcache` (shared by 1080p and 1440p, like the shader cache).
   - **Order:** it is created and loaded before `WarmUp`, so the preload builds through it. Before, it was created after the preload (upstream's order), so the preload built every pipeline with no driver cache at all.
   - **Checks:** the file starts with our own header (magic, format version, vendor, device, driver version, `pipelineCacheUUID`, data size), followed by the driver's data unchanged (`vk_driver_cache_file.h`). A cut, padded or foreign file, another GPU, another driver version, or a driver header that doesn't match is rejected with a `[gpu] driver pipeline cache: the saved one was …` line, and a new cache starts. If the driver refuses the data anyway, it retries with an empty cache.
   - **When it's saved:**
     - every 20 s while new pipelines have been made, by a background thread;
     - when the game ends: the runtime's window-close and normal-exit paths call `video::RunExitHook()` → `Vulkan::SaveDriverPipelineCacheNow()`, because `ExitProcess` runs no destructors;
     - once after the first boot that built the preload into a new cache.
   - **Writing:** it's written as `driver.vkcache.new` and then renamed, so a game killed mid-write keeps the previous file. The `.vkcache` extension matches no blob type, so `ForEachBlob` never reads it.
   - **Off switch:** `LBP3_VK_PIPELINE_CACHE=off` restores the old behaviour exactly (no file, the cache created after the preload).
3. **Async pipelines stay out.** LBPPC's `BB_ASYNC_PIPELINES=1` builds new pipelines in the background and skips the draw until one is ready: a visual trade-off, and the user's call. If wanted, it would plug in at `GetGraphicsPipeline`'s `is_new` branch. That branch would queue a job built with the same `PreloadJob`/`BuildPreloaded` machinery and return null until it's done, with the draw-skip constraints from [first_visit.md](first_visit.md).

## Thread safety and ordering

- **Ordering:** `WarmUp` runs in the `PipelineCache` constructor, before the rasterizer draws anything, and `BuildPreloaded` joins every thread before it returns. The `graphics_pipelines`/`compute_pipelines` maps are written only after the join, on the constructing thread; between the read phase and the join they hold empty entries nobody looks up.
- **What the workers share:**
  - they call only device functions (`vkCreateDescriptorSetLayout`, `vkCreatePipelineLayout`, `vkCreateShaderModule` for the aux tess/discard shaders, and `vkCreateGraphicsPipelines`/`vkCreateComputePipelines`), which are thread-safe;
  - the `VkPipelineCache` is synchronized internally by the driver: it's created without `EXTERNALLY_SYNCHRONIZED`;
  - they read `Shader::Info` objects owned by `program_cache`'s heap-allocated Programs, which nothing writes during `WarmUp`;
  - the constructors take the scheduler, descriptor heap and profile only as references, and don't use them while preloading.
- **The fetch shader:** ours is a pointer into a Program's permutation list, which a later read can grow and reallocate ("beware of realloc"). So a job keeps a **copy** of the fetch-shader data, unlike the serial code, which used the pointer at once.
- **Concurrent saves:** `SaveDriverCache` takes a mutex (saver thread against exit hook). `vkGetPipelineCacheData` may run while the GPU thread creates pipelines through the same cache; the driver's internal synchronization covers that.
- **Failed builds:** a pipeline the driver refuses still asserts, as in the serial preload. LBPPC's non-fatal `IsValid` path isn't ported, because our pipeline classes have no such state.

## Offline checks

- **Build:** clean.
- **All 12 offline test programs pass with 0 failed:**
  - `cooptest` 81, `dlgtest` 154, `osktest` 263, `dialogtest` 368, `jpegtest` 157, `imetest` 403;
  - `framegentest` 103, `audiotest` 32, `addconttest` 55, `rescachetest` 68, `unlocktest` 21;
  - the new **`vkcachetest` 25**: round trip; cut, padded and foreign files; another GPU, driver version or UUID; a bad driver header.
- **Commits:**
  - shadPS4 `9cad02b7`, then `e1d0d978` (the log line splits reading from building);
  - runtime `caaf656`, then `a30fb18`;
  - both `fix/pipeline-warmup`, pushed to the forks.

## In-game results (2026-11-13 03:00–03:10, game lock `warmup-port`)

Run on the story test save (`saves.sh`) from the clone's folder with the worktree's exe, on shader-cache copies (`analysis\pwarm\scratch\A` and `B`, copied from the shared `user\shader_cache\CUSA00473`, 4,020 keys), 165 Hz display. Each game was closed through the window (WM_CLOSE), so the exit hook ran. Log: `analysis\pwarm\runs\progress.txt` and `*_stderr.txt`.

**Boot to the Pod** (to the profile save mount, in ms; the preload in s):

| Run | Settings | Driver cache file | Pod | Preload |
|---|---|---|---|---|
| S0, S1, S5 | 1440p, old (serial, `LBP3_VK_PIPELINE_CACHE=off`) | none | 5,764 / 5,757 / 5,500 | 3.93 / 3.94 / 3.89 |
| P1, P2, P5, P6 | 1440p, new (16 threads) | loaded, 50.7 MB | 5,106 / 5,119 / 5,032 / 4,680 | 3.28 / 3.30 / 3.30 / 3.01 |
| S2, S6 | 1440p, serial with the file | loaded | 5,707 / 5,323 | 3.76 / 3.56 |
| S3, S4 | 1080p, old | none | 5,374 / 5,434 | 3.82 / 3.88 |
| P3, P4 | 1080p, new | loaded | 4,697 / 5,046 | 3.10 / 3.24 |
| P0 | 1440p, new, **first boot** | none, so a new one | 7,790 | 5.98 |
| X1, X2, X3 | 1440p, new, file **rejected**, so a new one | corrupt / cut / driver version | 13,308 / 14,744 / 16,474 | 9.86 / 10.79 / 11.66 |

- **Steady state:** about 0.5–0.75 s faster to the Pod at 1440p (5.0 s against 5.7 s) and about 0.5 s at 1080p. Most of it comes from the parallel build; the file alone, serial (S2/S6), gives about 0.2–0.3 s.
- **Where the preload's time goes** (P5/P6 against S5): with the file loaded, building the 4,020 pipelines takes **0.10–0.11 s on 16 threads**; **reading takes 2.9–3.2 s** and is serial (keys, metas, SPIR-V, shader modules). Reading is the next lever: each pipeline reads its stages' meta and SPIR-V files again, even when another pipeline already read the same stage. Memoizing those files during WarmUp, or reading them on several threads, is the obvious next step; it isn't done here.
- **First boot with a new driver cache is slower, once:** 6–12 s of preload against 3.9 s with no cache object (P0, X1–X3).
  - *Inferred:* with an empty VkPipelineCache attached, NVIDIA compiles every pipeline instead of serving it from its own internal shader cache.
  - It happens on the first start after this lands, and after a driver update, which the driver version check turns into a rejection. The cache is saved at that session's exit, and every later boot is the fast case.
  - The X runs' preload grew from run to run (9.9 → 10.8 → 11.7 s); not explained.
- **The file:**
  - written at exit on the first run (`saved (50681 KiB, 4020 new pipelines)`) and reloaded on every later run (`loaded (50681 KiB)`);
  - nothing saved when no new pipelines were made;
  - the three damaged copies were each rejected with the right reason: "not a driver cache file", "cut short or padded", "made by another driver version";
  - each of those games booted normally and saved a new file at exit.
- **No compiles and no warnings** in any run: 0 pipelines compiled, 0 "conflicts with index", 0 invalid-sharp, device-lost or stuck lines.
- **`user\shader_cache` was never written:** the guard checked after every run.

**Screenshots** (`pw_ingame.sh shots`; captures in `analysis\fg_dump\PW{A,B,S}_{pod,np}`, scored by `D:\Recomps\lbp3-wt\_tools\pw_score.py`). Frame generation was off; 1440p; the Pod and the start of Needlepoint Peaks; best of 3×3 frames:

| Pair | Pod: mad / p8 / edge | Level start: mad / p8 / edge |
|---|---|---|
| New (PWA) against the old behaviour (PWS) | 2.79 / 4.28% / 30.1 | 2.94 / 11.6% / 11.9 |
| Noise: new (PWB) against new (PWA) | 3.37 / 6.15% / 31.9 | 4.44 / 18.4% / 14.0 |
| New (PWB) against old (PWS) | 1.25 / 3.50% / 15.2 | 4.74 / 17.3% / 15.3 |

New against old falls inside new against new's run-to-run noise (Sackboy's idle animation, particles), so there's no visible change. PWA and PWS each compiled the same 4 pipelines on the way to the level; PWB, the second run on the same copy, compiled none.

## Regression at `play.bat`'s settings (03:15–03:35)

The `pwarm` exe was swapped into the clone and restored by md5 (`d5d335ad…`) afterwards, with `LBP3_RES=1440 LBP3_RES_SHADERS=1 LBP3_FRAMEGEN=1 LBP3_FRAMEGEN_MODE=warp`, DLC on, and the shader cache a copy (`analysis\pwarm\reg\`; script `D:\Recomps\lbp3-wt\_tools\pw_reg.sh`).

- **`m1_checks.sh`:**
  - Introduction steps ≠ 1: 0.00% and 0.26%, frame averages 33.3–35.6 ms, 3 and 3 frames over 40 ms after the level opened (the same as `port`'s night runs);
  - Thruster recipe 3 of 3;
  - Bunkum 16 of 16, Needlepoint loads;
  - sky cyan.
- **`t_m2v_photo.sh`:** PASS (encoder, capture readback, 1 JPEG).
- **`t13_coop.sh`:** PASS, with no GPU warnings.
- **The driver cache file** was saved by the periodic saver during the run (52 MB), even though the test scripts force-kill the game.
- **State afterwards:** no game, marker or kept copy; the human's saves identical to `human_play_20261113_0046`; `user\shader_cache` untouched; the clone's exe restored. The scratch cache copies were removed, and the game lock released.

## Verdict

Merge-ready for `bestofboth`. It's no visual change (screenshots within noise), it has two off switches, and the full regression passes. It cuts boot to the Pod by about 0.5–0.75 s in steady state, but costs one slow first boot (6–12 s of preload) after it lands and after each driver update. The bigger remaining cost is the serial read phase (about 3 s for 4,020 pipelines); memoizing or parallelizing it is the next step for a bigger "compile everything" cache.
