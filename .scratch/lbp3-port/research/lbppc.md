# LBPPC: licence, shader compilation, architecture (2026-11-13)

The user pointed at [emurphy04/LBPPC](https://github.com/emurphy04/LBPPC/tree/main): "tremendous work has been done here, perhaps you can take inspiration from this". It's another native PC port of LBP3 CUSA00473 1.28, at public beta 0.1.2.

**Read at:** commit `aad6a91bb7035e242899b223a16b49d91fb194ea` (2026-11-12, "0.1.2 hotfix"). There's a shallow, read-only clone in `D:\Recomps\lbp3-wt\_ref\LBPPC`; nothing in it was built or run.

**Companion files:** the feature comparison is split into [lbppc_graphics.md](lbppc_graphics.md) (frame generation, resolution, performance, rendering fixes, brightness) and [lbppc_platform.md](lbppc_platform.md) (DLC, online, input, audio, saves, debug and UX).

## Licence

- **The repository as a whole is GPL-3.0-or-later.** `LICENSE` is the GPL v3 text. The README says "GPL-3.0-or-later; see LICENSE. Code taken from shadPS4 and the Bloodborne PC port keeps its own notices." `LICENSE-GPL-2.0` is included for those parts. The GitHub API reports `gpl-3.0`.
- **Per-file markings** (SPDX lines across `src`, `gpu/shim`, `gpu/bbnet`, `gpu/dlss_bridge`, `scripts`, `launcher` and the vendored shadPS4 tree):
  - **GPL-2.0-or-later:** 156 C/C++ files, 2 C files and 2 Python scripts. These are the vendored shadPS4 video core and recompiler, including the Bloodborne-port ("bbport:") changes inside them, e.g. `vk_pipeline_cache.cpp`, `vk_pipeline_serialization.cpp`, `vk_draw_prep.cpp`, and `scripts/quit_menu.py`.
  - **MIT:** 3 files, e.g. `gpu/dlss_bridge/bbport_dlss_bridge.cpp` ("Copyright 2026 IFreemz (shadps4_dlss bridge), bbport contributors").
  - **GPL-3.0-or-later:** 1 file.
  - **No SPDX line:** the LBPPC-original runtime (`src/runtime*.c`, `compat_win.c`, `probe.c`), `gpu/shim/bbport_toggles.h`, the launcher. These fall under the repository's GPL-3.0-or-later.
- **What we may do.** Our fork is GPL-2.0-or-later (`LICENSE` is GPL v2; README "GPL-2.0-or-later (see LICENSE), the same as shadPS4"; runtime files carry `SPDX-License-Identifier: GPL-2.0-or-later`).
  - **Files marked GPL-2.0-or-later:** we can copy code from them as GPL-2.0-or-later, keeping their copyright lines and crediting LBPPC and the Bloodborne PC port. This covers everything shader- and pipeline-related below.
  - **MIT files:** reusable, keeping the MIT notice.
  - **Unmarked or GPL-3.0 files** (their C runtime, launcher, motion and DLC code): GPL-3.0 code can't go into a GPL-2.0-only work. Because ours is "or later", we *could* take it, but then the combined fork has to be distributed under GPL-3.0. That's a licence change for the user's public fork, so it's their call. Without that decision: ideas only, reimplemented.
  - Inferred, not legal advice: this rests on the per-file SPDX markings as the authors present them.

## Shader compilation (the user's question: first-visit freezes and a "compile step")

### What they found (their `docs/shader-precompilation.md`, research note of 2026-11-04)

Measured on a Ryzen 7 7800X3D with an RTX 4090.

- **Scale:** 13,555 materials (`GMTb`, 10 pixel-shader slots each) and 135,180 unique shaders. Our scan counted 135,428, a different method that agrees in scale. One cache that had seen 888 shaders held 1,288 pipelines.
  - Slot use: 2 (555) and 8 (400) dominate, then 0 (133), 1 (50), 3 and 9 (9 each); slots 4–7 are unused.
  - The 1,147 material pipelines reduce to 770 pixel shaders, 46 vertex shaders and 14 pipeline-state sets.
- **Where the time goes, cold** (empty caches, including NVIDIA's `__GL_SHADER_DISK_CACHE_PATH`):
  - SPIR-V translation: 450 at a median 1.0 ms (0.5 s total);
  - driver pipeline builds: 441 at a median 28.6 ms (11.6 s);
  - **the driver is about 96% of the cost.** So translating shaders ahead of time alone buys little.
- **Building everything up front isn't practical:** one pipeline per shader at about 29 ms is over an hour of CPU, before counting state combinations.
- **Reading ahead from archives:** the port can identify materials by their `.farc` read offsets. In one run, 476 materials were read, and all 333 material shaders compiled came from them. Median lead time was about 20 s before first use; the minimum was 0.2–0.35 s.
- **Predicting whole pipelines** from a material: 75% of pipelines needs about 10 builds per material, about 4,800 pipelines and roughly 35 s on 4 threads. They rejected that as too costly for the accuracy.
- **`VK_EXT_graphics_pipeline_library`:** the 46 vertex shaders and 14 states could be prebuilt, leaving only pixel shaders. Link time is unmeasured, and 68 of 1,147 pipelines needed a second pixel-shader variant.
- **Their recommended order:**
  1. background builds on by default, after checking pop-in and co-op lockstep;
  2. translate a material's shaders when it's read (about 2 s of CPU for 476 materials);
  3. **ship a list of known pipelines (shader hashes and state, no game code)**, built from the player's own dump on first start, est. about 1,300 pipelines in about 5 s on 7 threads, unmeasured;
  4. pipeline libraries only if pop-in remains.
- **Their own caveat:** a tester's slow stretches in the second level showed almost no compiling, so shader work fixes only hitches caused by new shaders.

### What their code actually does (`gpu/shadps4/video_core/renderer_vulkan/`, all GPL-2.0-or-later)

1. **The driver's `VkPipelineCache` is saved to disk** (`vk_pipeline_cache.cpp`: `CreateDriverCache`, `SaveDriverCache`, `DataBase::DriverCachePath`).
   - Loaded before warm-up, and checked against the vendor, device and `pipelineCacheUUID` header, so a cache from another GPU or driver is discarded.
   - Saved every 20 s while pipelines are being made, and at exit, by writing a temp file and renaming it, so a killed game keeps the old file.
   - **We don't do this.** perf/first_visit.md found our `VkPipelineCache` never reaches disk, and that we rely on NVIDIA's own disk cache.
2. **Warm-up builds the cached pipelines on all cores** (`vk_pipeline_serialization.cpp`: `PreloadJob`, `BuildPreloaded`).
   - The keys are read serially, then the pipelines are built in parallel on `hardware_concurrency` minus 1 threads (at most 16; `BB_PRELOAD_THREADS` overrides). The driver cache is thread-safe, and these builds touch no guest memory.
   - A pipeline that fails to build is left to compile at first use.
   - A cache written by a different shader build is cleared and rebuilt (`BB_SHADER_BUILD` stamp).
   - **Ours warms up serially.** That's why our merged ~4,000-key cache costs about +0.65 s at 1440p and +1.6–2 s at 1080p before the Pod (perf/shared_cache.md).
3. **Background builds, opt-in** (`BB_ASYNC_PIPELINES=1`, off by default; `run.py` doesn't set it).
   - A new graphics pipeline is set up on the GPU thread, built by the driver on `cores/4` worker threads (1–4), and its draws are skipped until it's ready.
   - Translation still happens on the GPU thread. Compute pipelines are never deferred.
   - **Their numbers, off against on:** worst frames 4,567/3,004/2,208 ms against 342/346 ms; pipeline work on the GPU thread 11.6 s against 0.07 s; lowest 5-second FPS 8 against 27.
   - They keep it off: how skipped draws look, and whether skipping can affect simulation (online co-op lockstep), are unverified.
4. **Not compilation:** `vk_draw_prep.*` ("PreparedDraw") is speculative draw preparation on idle cores, a CPU-recording optimisation that never compiles anything. `run.py` disables it for LBP3 (`BB_DRAW_PIPE=0`, `BB_PREP_WORKERS=0`): "corrupts memory under LBP3".
- **Not found:** no shipped pipeline list, no "compiling shaders" screen, no per-level cache. The README says "new content may stutter once while shaders compile". Their release notes say the same.

### Applicability to our port

Our shader cache already persists, and warm-up rebuilds every key at boot: first-visit freezes are once per level, ever (perf/first_visit.md).

| Idea | What it gives the user | Effort | Code |
|---|---|---|---|
| **Parallel warm-up** (`BuildPreloaded`) | Boot stays fast however big the cache grows. That's the precondition for any "compile everything" step: a full-game cache of about 15–20k keys serially at our ~0.9 ms/key would be 15–20 s of boot, in parallel on the 5900X's 23 spare threads about 1–2 s (estimate, driver cache warm). It also wins back the merged cache's 0.65 s now | small: one function, same shadPS4 lineage | reusable, GPL-2.0-or-later, with attribution |
| **Persistent `VkPipelineCache`** | Faster cold builds when NVIDIA's own disk cache is evicted, e.g. after a driver update; makes the warm-up's builds cheap. Gain on the user's NVIDIA card is likely modest, since the driver cache already covers most of it (inferred) | small | reusable, GPL-2.0-or-later |
| **A pipeline list or warm-up tour = the "compile step"** | No freeze on first visits. Their plan 3 ships hashes and state (no game code); ours can produce the list by an automated tour of every level from a test save, or from the user's own play. The list feeds the cache; parallel warm-up builds it | medium: a level-jump hook and a scripted tour, or just a list format plus a builder | idea only; nothing like it is in their code |
| **Background builds** (`BB_ASYNC_PIPELINES`) | No freeze for anything the list misses; objects appear a frame or two late. Our extra constraints (perf/first_visit.md): never skip the lighting-volume slice builds, atlases or photo capture, never skip the display-buffer composite (frame generation reads "no composite" as a load), no lockstep issue for us (local co-op only) | small to medium | reusable, GPL-2.0-or-later; still the user's visual call |
| Translate on material read | About 4% of the cost; low value alone | medium | idea |
| Pipeline libraries | Only if pop-in remains | large | idea |

**Recommendation, in order:**
1. Port the parallel warm-up and the persistent driver cache together: low risk, no visual change.
2. Build the compile step as a warm-up tour producing the list.
3. Offer background builds as an opt-in safety net.

## Architecture

- **Lineage:** the runtime and graphics layer come from the Bloodborne PC port 1.9.5 (commit `ff42e28`; "bbport" by deadinside28 and the Bloodborne PC port by Supermedo, nick24051998 and others).
  - **Graphics:** vendored shadPS4 (`gpu/VENDOR.txt`: commit `b4e7ae77`, 2026-10-02, plus backported recompiler fixes #5087, #5126, #5031, #5030, #5114, #5185, #5200, #5194, #5186; local changes marked `bbport:`).
  - **Online:** shadPS4's NP and network code with a shadNet patch (`gpu/bbnet`); the Refresh community server holds levels, profiles and photos.
  - **Frame generation:** a patched FSR-Vulkan (FSR 3.1, an FSR 4 provider) and an MIT DLSS bridge.
  - Our shadPS4 fork's upstream base is newer (#5317, 2026-10-09) and already contains all nine of those recompiler fixes, so there's nothing to backport there.
- **Their runtime:** C, about 12k lines in `src/` ("narrow, explicit PS4 libc contracts, no automatic success stubs"), with separate files per area (kernel, memory, threads, mutexes, files, savedata, pad, motion, mouse-as-touch, audio, AJM, content/DLC, services).
  - The graphics layer sits behind a shim (`gpu/shim`, `bbgpu.h`) instead of shadPS4's core.
  - **Ours:** a C++ HLE runtime from PurpleGitty's LBP3 Recompiled (`runtime/src/hle/*`), with the shadPS4 fork as a submodule.
- **Pre-launch Python pipeline** (`run.py`, `scripts/`), where ours does this at load time in C++:
  - **Module linking:** `link_libc.py` and `link_modules.py` link the eboot with the game's bundled modules into one image.
  - **In-memory file edits:** `quit_menu.py` produces `file_edits.bin`, which the runtime applies to game files as they're read; nothing on disk changes.
  - **Other steps:** `content_profile.py`, `cpu_compat.py`, `game_setup.py` (copies the 1.28 update into the game folder).
- **Launcher:** Python/PyInstaller. It covers setup, resolution, frame generation, upscaling, controls (`input.ini`), accounts and friends, and a report zip without the password or user name. An in-game ImGui menu opens on Insert or L3+R3.
- **Shared findings:** the eboot loaded at 0x400000 because `spu.prx` keeps 32-bit pointers to eboot globals (their `LBPPC_LOW_BASE`, our loader); and the compute shader `5c5e1be6`'s buffer writes over render targets (their `BufferWriteImageSync`, our masked-fill and render-target-fill work).
- **Theirs only:**
  - **Lockstep online co-op:** CPU-specific math shortcuts replaced with exact equivalents (`exact_math_sites.inc`).
  - **Frame generation's camera:** recovered from the draws' matrices (`lbp_camera.*`).
  - **Latency:** Reflex or Anti-Lag 2, with the 30 fps limit applied at frame start instead of a vblank wait ("84 → 29 ms" start to present).
  - **Frame statistics** in the log every 5 s.
  - **Live optimisation toggles** in `user/toggles.txt`.
