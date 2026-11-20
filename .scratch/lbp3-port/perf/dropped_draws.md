# The 18 unreferenced shaders: draws the video core drops (read-only), 2026-11-12

Code read at `D:\Recomps\lbp3-recompiled`: `m5/verify` `2d92d65`, shadPS4 submodule `31071685`. `video_core/`, `shader_recompiler/` and `core/` are under `third_party\shadPS4\src\`; `hle/` and `gpu/` are under `runtime\src\`.

Data:
- the cache `user\shader_cache\CUSA00473\` (read while tonight's regression was running, so mtimes moved during the read);
- `analysis\*_stderr.txt`, `analysis\play_logs_20261111\`, `analysis\shaders\dumps\`;
- `analysis\ghidra\eboot_etexec.elf`.

Throwaway `python -I` scripts plus `tools\strings_near.py`. Nothing was built, run or edited.

## Answers in brief

- **Cause: gap 15.** One path in LBP3's object ("thing") renderer draws with a geometry-shader stage set: `VGT_SHADER_STAGES_EN = 0xB0` (ES+GS), set at eboot+0x465d40. It is the only code that ever selects a stage set other than plain VS/PS.
  - `sceGnmSetEsShader` and `sceGnmSetGsShader` are NOP fills (`hle/gnm_packets.cpp:82-83`), so the ES and GS program registers stay 0.
  - shadPS4 compiles the pixel shader first, then `bind_stage(Export)` finds no ES program and the draw is dropped silently (`vk_pipeline_cache.cpp:566-568` via `:535-538`).
  - The pixel shader is compiled and written to the cache, but no key ever references it. That is where the orphans come from.
- **Correction:** 07's note ("the video core runs it without that stage") and gap 15's wording are wrong. The **whole draw** is dropped, on every frame that draws it, starting from the Pod home screen.
- **Visible:** probably, but what exactly is missing is unknown.
  - The cache shows only the depth-only and velocity (motion-vector) passes of some alpha-tested materials.
  - A colour-pass draw that reuses an existing permutation, or a draw with no pixel shader, compiles nothing and leaves no trace. So whether whole objects vanish is unknown.
  - They appear in the Pod home screen (more with a second player), the Pod menus, the Introduction, the Legend of Bunkum when the heroes enter, Needlepoint Peaks, the move from the Manglewood hub to the Diner, and the Diner.
- **Next:** add one log line per distinct dropped key and a counter on `[perf]` (small, no risk). Then port the four `sceGnmSet{Es,Gs,Hs,Ls}Shader` register writes behind a switch. The risk is moderate: it is the first time shadPS4's GS path runs in this port.

## 1. Every place a draw is skipped, and whether it logs

The log glue prints Warning and above, the first 20 per call site and then every 1000th (`gpu/shadps4_glue.cpp:28`, `:44`).

**Rasterizer** (`video_core/renderer_vulkan/vk_rasterizer.cpp`):

| Line | Condition | Shader compiled first? | Logs |
|---|---|---|---|
| `:238-239` (`Draw`), `:299-300` (`DrawIndirect`) | `FilterDraw` (`:58-103`) returns false. The draw is replaced by a fast-clear eliminate (`:60-63`), an FMask decompress (`:65-70`), a resolve (`:72-75`) or a depth/stencil copy (`:94-100`), or it has primitive type None (`:77-80`). | no | Trace only (not printed) |
| `:243-245`, `:307-309` | `GetGraphicsPipeline` returns null: `RefreshGraphicsKey` is false (`vk_pipeline_cache.cpp:360-362`, `:472-474`). See the next table. | **yes**: the fragment shader at `:550` | see below |
| `:249-250`, `:313-314` | `BindResources` returns false. That happens only for compute pipelines: the three `IsCompute*` checks return false for graphics (`:683`, `:720`, `:777`). | n/a | — |
| `:379-381`, `:448-450` | `GetComputePipeline` returns null. It never does: `RefreshComputeKey` always returns true (`vk_pipeline_cache.cpp:638-645`). | — | — |
| `:385-386`, `:389-390`, `:453-454` | A dispatch is replaced by HLE: the copy shader, image copy, metadata clear or image clear. The pipeline and its key already exist. | yes, but keyed | no |

**`RefreshGraphicsStages`** (`vk_pipeline_cache.cpp:519-636`) is the only source of a false key. The fragment stage is bound first (`:550`), so a new fragment permutation is compiled before any of these checks.

| Line | Condition | Logs |
|---|---|---|
| `:558-561` | ES+GS, the device lacks `geometryShader` | Warning |
| `:562-565` | ES+GS, `VGT_GS_MODE.onchip` or `VGT_STRMOUT_CONFIG` ≠ 0 | Warning |
| **`:566-568`** | ES+GS, ES program address 0 (`bind_stage`, `:535-538`) | **silent** |
| `:569-571` | ES+GS, GS program address 0. The ES would already be compiled, leaving an orphan vertex-stage meta. | silent |
| `:574-576` | LS+HS, the device lacks `tessellationShader` | silent |
| `:577-585` | LS+HS, HS, VS (as TES) or LS program address 0 | silent |
| `:588-590` | LS+HS+ES+GS, no tessellation | silent |
| `:591-598` | LS+HS+ES+GS, no GS, or on-chip GS, or streamout | Warning |
| `:599-610` | LS+HS+ES+GS, any program address 0 | silent |
| `:615-617` | any other `VGT_SHADER_STAGES_EN` value | Warning |

`:528-539` are not drop sites; they are `bind_stage`'s own returns.
- The fragment result is ignored (`:550`): a draw with no PS goes ahead depth-only.
- So is the `Vs` case's (`:613`): a null VS would build a pipeline with no vertex stage. That is not hit, because all 2,143 cached graphics keys have a VS.

The GS and tessellation features are both present on the RTX 3090 (`vk_instance.h:258-265`).

## 2. What the 18 shaders hit

**The orphans:**
- 18 `.meta` files, 17 programs, all fragment shaders.
- 16 of them are **permutation 1** of a program whose permutation 0 is used by 1–4 keys. Those keys are ordinary material draws: VS `0x8af210a5e06f1b3e`, triangle strip, D32 depth, cb0 RGBA8 UNORM.
- The 17th, `0x4299ba393ac46b7d`, has **both** of its permutations orphaned. No key ever used it.

**Permutation 0 against permutation 1** (decoded `.meta` runtime info):
- They differ only in `color_buffers[0]` (bytes 148–155): permutation 1 has no colour target bound.
- Their SPIR-V confirms it:
  - permutation 0 is the full material: 6–23 texture samples and a write to location 0;
  - permutation 1 is 2 samples plus a demote, with no colour output.
- So permutation 1 is the same alpha-tested material, drawn depth-only. That is the pass, not the reason for the drop: 22 cached keys are a PS with depth only, drawn through the ordinary VS stage.

**`0x4299ba39`** (220 bytes of GCN, `analysis\shaders\dumps\fs_0x4299ba393ac46b7d.irprogram.txt`):
- It samples a texture and discards where `.g > 0.8`.
- It writes `(1, Δy·k + 0.498, Δx·k + 0.498, 1)`, where Δ is the difference between two projected positions (Param0 and Param1).
- That makes it a **velocity (motion-vector) shader**. Its two permutations differ only in the resource specialization.

**Proof by elimination:**
1. A new fragment permutation always makes a new key. So "N shaders, 0 pipelines" means `RefreshGraphicsStages` returned false after `:550`.
2. The `Vs` case cannot fail.
3. No `vk_pipeline_cache.cpp` line appears in any log under `analysis\`, which rules out the Warning branches.
4. There is no orphaned vertex, export or geometry meta, and none of the 2,143 keys has a TCS, TES or GS hash. So the draw failed at the **first** `bind_stage` after the switch:
   - `:566` (ES+GS, ES address 0),
   - `:577` (LS+HS, HS address 0),
   - or `:599`.

**Why the address is 0:**
- `sceGnmSetEsShader`, `SetGsShader`, `UpdateGsShader`, `SetHsShader`, `UpdateHsShader` and `SetLsShader` only write a NOP (`hle/gnm_packets.cpp:82-87`, registered at `:187-192`).
- Nothing else writes `SPI_SHADER_PGM_LO_ES/GS/HS/LS`. The init sequence writes only their RSRC3 (`core/libraries/gnmdriver/gnmdriver_init.h:129-135`).

**Geometry, not tessellation** (eboot scan):
- `VGT_SHADER_STAGES_EN` is context register 0x2D5 (`regs.cpp:114`). It has exactly one immediate writer, the Gnm wrapper at eboot+0xacf930 (the write is at +0xacf9a7).
- That wrapper has four callers:
  - +0x5c89f, +0x5d048 and +0x466021 pass 0 (VS/PS);
  - **+0x465d40 passes 0xB0 (ES+GS)**.
- No call site passes 0x45 or 0xAD, so no call site enables tessellation.
- **Which code it is** (call graph from `call rel32` targets and the nearest frame-pointer prologue; strings from `tools\strings_near.py`):
  - **On:** the 0xB0 call is in a function at +0x465cf0, which references the names `campos` and `positionOffset`.
  - **Off:** the GS mode is switched off (+0x465ff4) and the stages are reset to 0 (+0x466021) in the next function, +0x465fb0 (`modelViewProj_old`, `positionOffset`, `uvescale`, `campos`).
  - **Caller:** both are called only from +0x470200 (`uvmultiplier`, `uvescale`).
  - **Its callers:** the two routines at +0x46ef10 and +0x47d180, whose names (`extrude`, `flipped`, `centroid_param`, `uThingCol`, `tc0biasscale`) look like the general object ("thing") draw path.
  - **Not the rope code:** the rope code (`RenderAllRopes` in +0x463ee0, `RopeRenderLock` in +0x464530) sits right next to it but doesn't call it. An earlier draft of this note called this "the rope renderer". That was wrong: it is only adjacent.
  - **What it suggests (inference):** `campos` and `positionOffset` point to geometry expanded or offset towards the camera, such as fins, fuzz or ribbons. `modelViewProj_old` fits the velocity shader.
- **The GS state is already written:** the game's own Gnm wrappers write the rest of the GS state (GS mode, ESGS/GSVS ring item sizes, `GS_VERT_ITEMSIZE`, `GS_MAX_VERT_OUT`). The GS mode is set to 0x180003: scenario G, write-optimise, on-chip 0 (+0x60120). So the `:562` check would pass once the programs are bound.
- **The drop site is `vk_pipeline_cache.cpp:566-568`.**

**Why only these shaders show:**
- A dropped draw whose pixel shader matches an existing permutation compiles nothing. A colour pass with the same target format would match permutation 0.
- A dropped draw with no PS compiles nothing either.
- So the 17 programs are a lower bound on the materials affected, and the cache cannot say how many draws are dropped per frame.

**Why they are retranslated each run:**
- The warm-up loads only programs and permutations that some key references.
- So each run recompiles the orphan permutation (same index, same file names) the first time such an object is drawn: 2–3 ms each, harmless.
- The draw itself is dropped **every frame**, not once.

## 3. Where they appear

How the times were matched:
- Tonight's `.spv` modification times were matched to the regression logs.
- For the story run (`m2_m5_17s_1_stderr.txt`, launched 00:08:10), the rewrites at 00:08:24, 00:09:01, 00:09:29 and 00:10:26 match its four compile windows.
- For t13 B and intro2, each window's time was reconstructed from the `[video]` windows, summed back from the log's modification time. The rewrites fall within 3 s of these windows:
  - t13 B: `:576` at 23:57:13, `:786` at 23:57:50, `:968` at 23:58:15;
  - intro2: `:1006` at 23:45:30.
- First sightings use the `.meta` creation time.
- Play-log times are reconstructed the same way (±10 s). Those logs have no `[perf]` lines, so they are matched to archive opens.

| Program (perm.) | Pass | First translated | Tonight | Where (log event) |
|---|---|---|---|---|
| `4299ba39` (0) | velocity | 11-09 20:25:49, the first cached boot | every regression run tonight (the 2-shader window at `:~520` in the story, osk, backup, photo, t13, intro and bunkum logs) | **Pod home**, flips 90–120, after the intro videos and the Pod music `i_pod3.fsb` (`m2_m5_17s_1:522`). Not in the Needlepoint route runs (`m5_np_route.sh`, which restores `analysis\savedata_story_20261111`); why was not checked. |
| `221ff5dc` (1) | depth | 11-09 20:28:00 | as above | as above |
| `e3408e07` (1) | depth | 11-09 20:25:49 | t13 B `:576` (3 shaders, 23:57:13) | **Pod home with two players** (the third shader of that window) |
| `31a39138` (1) | depth | 11-10 18:33:43 (t10 HITL session; its log is empty) | t13 B `:786` (23:57:50) | Pod, two players, before the Introduction loads |
| `95a226e0` (1) | depth | 11-09 20:29:08 | story `:720`, photo_2 `:699` | Pod, while the story map or Create loads (114 new images near flip 1200) |
| `25a8da36` (1) | depth | 11-09 20:30:36 | intro2 `:1006` (23:45:30) | **Introduction** (`01_prologue/00_craftworld/craftworld.farc`) |
| `4f5df040` (1) | depth | 11-09 20:29:56 | t13 B `:968` (23:58:15), then story `:1225` | **Introduction** (right after `craftworld.farc` opens, t13 B), and the Legend of Bunkum (below) |
| `3f2dc85b` (1) | depth | 11-10 00:50:27 (feel session) | story `:947` | **Legend of Bunkum** (`c01_intro/intro.farc`), flip ~2070, before `cs01_10_newton.mp3` |
| `ac1e3c1a`, `1cee4952`, `6c38a09b`, and `4f5df040` again (1) | depth | 11-10 00:49:38 (two), 11-09 20:30:37 | story `:1225` (4 shaders, 00:10:26) | **Legend of Bunkum**, flip ~3780, after `cs01_70_newton.mp3` and `i_c_enter_heroes.fsb` (the heroes enter) |
| one of the above | ? | — | `m5_ab_*:~856` etc. | **Needlepoint Peaks entry**, flips 1620–1650, just after `needlepoint_peaks.farc`. Which shader it is is lost: later runs rewrote the files. |
| `c15538e9`, `bf334621`, `f2a4e2fb` (1) | depth | 11-11 17:52:28 and 17:52:44 | not reached by the scripts | Needlepoint Peaks, ~6.5 min after `needlepoint_peaks.farc` opened (`play_log.3.txt:4500`, 17:45:52) |
| `4299ba39` (1), `a32bd6ac` (1) | velocity, depth | 11-11 20:35:28 | not reached | **Manglewood**: within ±10 s of `diner.farc` opening (`play_log.txt:2237`), after the hub (`:1123`) |
| `e8e87bca` (1) | depth | 11-11 21:01:47 | not reached | **Diner**, ~26 min in, inside the 21:01:46–49 compile burst |
| `780b51ca` (1) | depth | 11-11 14:29:25 | not reached | no log kept |

So 11 are retranslated in tonight's scripted runs; the other 7 come from places the scripts don't reach.

**Pattern (an observation, not a conclusion):**
- The same programs turn up across archives:
  - the Pod at boot, with one more when a second player joins;
  - the Introduction and Bunkum (`4f5df040`);
  - a burst of four as Bunkum's heroes enter.
- So the GS-drawn objects seem to follow characters or save contents (costumes, decorations) rather than level archives.

**What's missing on screen: unknown.**
- At least these objects' depth-only pass and their motion vectors are missing. That means wrong depth-based effects and motion blur on them.
- If their colour pass also runs inside the ES+GS block (on at +0x465cf0, off in +0x465fb0, both called from +0x470200), the objects, or the GS-drawn part of them, are invisible wherever they appear. That is plausible but not verified.
- Nobody has reported anything missing so far.

## 4. Instrumentation, then the fix

**Instrumentation** (small, no risk; one stderr line per distinct dropped key, and a count):
- `vk_pipeline_cache.h`: add `const char* lbp3_drop_reason{}; u32 lbp3_drop_stage{};` to `PipelineCache`.
- `RefreshGraphicsStages` (`:519`):
  - clear both at entry;
  - set them before the returns at `:531` ("stage disabled") and `:538` ("no program address"), recording `stage_in_idx`;
  - set a reason at each switch `return false` that `bind_stage` doesn't cover: `:560`, `:564`, `:575`, `:589`, `:593`, `:597`, `:617`.
- `GetGraphicsPipeline` `:360-362`: before `return nullptr`, call a reporter that:
  - increments `g_lbp3DroppedDraws` (an atomic beside `:31-34`, read into `gpu::PerfCounters`, `gpu/renderer.h:40`, and printed as `dropped %u draws` on the `[perf]` line, `hle/videoout.cpp:149`);
  - hashes `{stage_enable.raw, lbp3_drop_stage, graphics_key.stage_hashes[0], key.color_buffers[0], key.z_format, key.prim_type}` with XXH3 and returns if the hash is already in a function-static `std::unordered_set<u64>`. The command processor is the only caller, so no lock is needed.
  - otherwise prints: `fprintf(stderr, "[gpu] dropped draw: %s at hw stage %u (stages %#x; pgm es %#llx gs %#llx hs %#llx ls %#llx vs %#llx; fs %016llx; cb0 %s; z %u; prim %u; gs_mode %#x; strmout %#x)\n", ...)`.
- Use `fprintf`, not `LOG_WARNING`: `VLog` throttles each call site after 20 lines.

Expected output: `no program address at hw stage 3 (stages 0xb0; pgm es 0 gs 0 …)`, once per material and pass, in the Pod and in Bunkum. The count shows how many draws a frame loses. Lines with no fs hash are draws with no PS, which today leave no trace at all.

**Fix:** port the register writes in place of the fills at `hle/gnm_packets.cpp:82,83,85,87`.
- Sources: shadPS4 `core/libraries/gnmdriver/gnmdriver.cpp` Es `:1563-1592`, Gs `:1599-1625`, Hs `:1627-1655`, Ls `:1657-1688`.
- Use this file's helpers (`ShReg`, `CtxReg`), and fill exactly `size` dwords, ending with `FillNop` up to `cmd + size`, the way `sceGnmSetPsShader` does (`:131`).
- **ES:** SH 0xC8 `{es[0], 0}`, SH 0xCA `{rsrc1 with the modifier, es[3]}`.
- **GS:** SH 0x88 `{gs[0], 0}`, SH 0x8A `{gs[2], gs[3]}`, context 0x2E5 `gs[4]` (streamout), 0x29B `gs[5]`, 0x2E4 `gs[6]`.
- HS and LS likewise, for completeness, although no call site enables tessellation.
- Put it behind a switch (e.g. `LBP3_GS_STAGES=off` restores the NOPs), and leave it off by default until A/B'd.
- The copy shader comes from the VS registers (`vk_pipeline_cache.cpp:168-170`). `sceGnmSetVsShader` already writes them.

**Risk: medium.**
- This is the first time shadPS4's ES→GS translation and pipeline run in this port. An assert or UNREACHABLE would hit `__debugbreak` (`gpu/shadps4_glue.cpp:54-67`) where today the draw is silently skipped.
- The spec's known risk of a GPU reset with GS stages.
- New pipelines bring first-sight compile stalls (ES+GS+PS for each affected material).
- If `gs[4]` (streamout) is non-zero, the draw is still dropped, but with a warning (`:562-564`).
- Game logic is unaffected.

**Checks for the fix:**
- the instrumentation's dropped count is 0 with the switch on;
- `[perf]` stops showing compiles with 0 pipelines;
- screenshot pairs (switch off and on) of the Pod home (story save, one and two players) and of Bunkum near flip 3780, diffed to find what appears;
- the milestone-5 regression suite;
- a cold-cache run to count the new pipelines.

## What stays unknown

- Which objects the GS path (+0x470200 → +0x465cf0) draws, and whether their colour pass goes through it. That decides between invisible objects and only missing depth and motion vectors. The first screenshot pair settles it. The "characters or save contents" pattern and the "fins, fuzz or ribbons" reading are inferences.
- Why the Needlepoint route's boot (a different save) shows no compile in the Pod.
- How many draws are dropped per frame: there is no counter yet.
- Whether shadPS4's GS translation handles these ES/GS programs: the streamout value, ring layout and output count are unread until the registers are written.
- Which shader the Needlepoint route retranslates at entry: its files were rewritten by later runs.
- The contexts of `780b51ca` (no log) and of `31a39138`'s first sighting (empty HITL log).
- Whether anything writes `VGT_SHADER_STAGES_EN` other than as an immediate. Only immediate SET_CONTEXT_REG writes were scanned.
