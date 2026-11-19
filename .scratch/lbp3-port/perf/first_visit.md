# First-visit freezes: do pipeline compiles repeat across sessions? (2026-11-12 night)

The question: when a level is entered for the first time, the game freezes for 0.5–2 s while the driver builds pipelines (compile_stalls.md, ticket 15). Is that once per level ever, or every session? If every session, a persisted cache fixes it with no visual trade-off. If once ever, the only remaining fix is compiling in the background and skipping draws until ready, which is a visual trade-off and the human's call.

**Answer: compiles do not repeat.** Each pipeline is compiled once, the first time its content is ever seen, per cache folder. Nothing to build here. Labels: measured = from the human's play logs and the cache files; code = read in shadPS4's source; inferred.

## Evidence

- **Every compile in this evening's logs made a pipeline never seen before (measured).**
  - shadPS4 saves one key file per compiled pipeline (`user\shader_cache\<serial>\g_<hash>.key`), named by the hash of the key and written with truncate, so a repeat would rewrite an existing file.
  - Of the 5,587 key files (2,325 in `CUSA00473`, 3,262 in `CUSA00473_res1440`), none was rewritten after it was created: modification time equals creation time for all of them.
  - Of the 4,491 shader binary and meta files, 13 in the 1080p cache were rewritten. That's a permutation reordering on a handful of shaders, negligible.
- **No pipeline-cache warnings in any session (measured).** shadPS4 warns about stale pipelines, permutation conflicts and incompatible caches at its warning level, which our log handler prints (`[shader warning] …`). There are none in the fg, 1440, 1440a, 19:55, DLC, unlocks and 21:59 logs.
- **What persists (code + measured):**
  - shadPS4's pipeline-key cache: keys, shader meta and SPIR-V in `user\shader_cache\<serial>\`, with `profile.bin` guarding compatibility. At every boot `PipelineCache::WarmUp` rebuilds every cached pipeline (`LoadGraphicsPipeline`/`LoadComputePipeline`) into the in-memory maps, so a later session's draws find them.
  - The SPIR-V shader cache, in the same folder.
  - NVIDIA's own driver disk cache (inferred, standard driver behaviour). It makes WarmUp's thousands of `vkCreateGraphicsPipelines` calls cheap.
  - The `VkPipelineCache` object is created empty each run and never saved (code, `vk_pipeline_cache.cpp:374`). WarmUp plus the driver cache cover for that. It would only matter after a driver update clears NVIDIA's cache: one slow boot, not in-game freezes.
- **Sessions in the Pod compile only new content (measured).** Compiles per level and per session, from `[perf] compiled N pipelines (X ms)` charged to the last level opened (script `tools/compiles_per_level.py`; the "worst window" is the most compile time in one roughly 1 s status window, i.e. the freeze felt):

  | Session | Where | Pipelines | Compile time | Worst window |
  |---|---|---|---|---|
  | 14:58 fg (1080p) | Manglewood quest level (`q01_manglewood_bp`) | 94 | 3.8 s | 0.68 s |
  | 14:58 fg (1080p) | Bite Size | 30 | 1.0 s | 0.39 s |
  | 18:31 1440p | Manglewood hub | 135 | 0.4 s | 0.32 s |
  | 18:31 1440p | Casino | 379 | 9.3 s | **2.19 s** |
  | 18:31 1440p | Space | 112 | 2.0 s | 0.50 s |
  | 18:31 1440p | Crypts | 189 | 1.7 s | 0.30 s |
  | 18:42 1440p+audio | Introduction (`00_craftworld`) | 100 | 1.2 s | 0.40 s |
  | 20:54 DLC | Pod (Customize Character with 256 packs' items) | 1,136 | 33.8 s | 1.84 s |
  | 21:43 unlocks | Pod (the new LBP1/LBP2 items) | 79 | 3.3 s | 0.71 s |
  | 21:59 | Pod | 42 | 2.0 s | 0.86 s |

  The Introduction compiled again at 18:42 after 18:31, but the key files show those were different pipelines, parts of the level not drawn in the earlier session. They weren't repeats.
- **New content compiles once:** the DLC costumes (1,136 pipelines while browsing them) and the imported LBP1/LBP2 items. Browsing them again in a later session won't freeze.

## One real gap: 1440p has its own cache

`LBP3_RES=1440` with `LBP3_RES_SHADERS=1`, which `play.bat` now sets, keeps its cache in `CUSA00473_res1440`, so the 1080p cache's SPIR-V stays clean of rescaled shaders. That folder started at 15:40 today. Everything first seen at 1080p before then compiles once more at 1440p if revisited. That's 8 of the 16 levels visited so far: the Prologue's 6 and 2 in Manglewood.

**Option, no visual trade-off, not built:** seed `CUSA00473_res1440` from `CUSA00473`, leaving out the 17 rescaled shaders and every key that uses one.
- Map a key's stage hashes (permutation hashes) to program hashes through the `.meta` files.
- Copy only programs whose permutation sets don't conflict between the two folders.
- The two `profile.bin` files must match.
- Back up the res1440 folder first.
- About an hour, offline, plus one boot to check WarmUp loads it without warnings.

It only pays off if the human revisits old levels.

## What's left to freeze on

46 story level archives (`gamedata\lbp3\official_adventure_levels`). Every play log this week was checked (`play_logs_20261111\`, `play_logs\`, the `play_*` logs):

| World | Levels | Visited | At 1440p |
|---|---|---|---|
| Prologue | 8 | 8 | 2 |
| Manglewood | 12 | 8 | 6 |
| Ziggurat | 13 | 0 | 0 |
| Bunkum (the third world) | 13 | 0 | 0 |

30 level archives are still unseen, cutscenes included: Manglewood's Titan level, its two cutscenes and the lift quest, then all of Ziggurat and Bunkum. Each will freeze for roughly 0.3–2 s on first entry (Casino was the worst so far at 2.2 s in one window), once.

## The draw-skipping option (a sketch only: the human's call, not built unattended)

Compile new pipelines on worker threads and skip draws that need a pipeline not ready yet, for a frame or two, instead of blocking. It turns a 0.5–2 s freeze into objects popping in a few frames late. Constraints, from this port's history:
- **Never skip draws into targets that aren't redrawn every frame:** the lighting volumes built a slice at a time (the expansion fix), atlases, shadow and blur targets with feedback (the bloom feedback), and photo capture. A skipped draw there stays wrong after the pipeline is ready. Allow skipping only into render targets fully cleared or overwritten each frame (the masked-fill/clear tracking from `fix/rt-fills` can tell).
- **Never skip the composite into the display buffer:** frame generation treats a frame with no composite draw as a loading screen and stops generating (perf/fg_inputs.md).
- **Never skip depth-only or velocity draws in the same frame as their colour pass,** or frame generation's vectors won't match.
- **Compute dispatches can't be skipped** (the fluid and the masked fills). They have to wait as now.
- **Opt-in switch,** with a per-frame budget (e.g. block only after 3 skipped frames for the same pipeline), and screenshot checks at the Pod, the Introduction fog and Needlepoint, as for the render-target fills.

Estimate: 1–2 sessions, medium risk (visual).
