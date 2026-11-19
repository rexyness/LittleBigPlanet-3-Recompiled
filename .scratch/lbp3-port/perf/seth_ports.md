# sethlikepie's ideas, ported: Batch 1 (2026-11-19)

From [research/sethlikepie_vs_open_items.md](../research/sethlikepie_vs_open_items.md), Tier 1 items 3 and 5–8 (the small, safe ones; items 1, 2 and 4 are Batch 2). The user said "go ahead".

## Branches

Pushed to the forks; nothing is merged into main, and there's no version bump or release notes yet.

- **Runtime `feat/seth-small`**, from main `29dad5a`, at `cbe2c29` (`rexyness/LittleBigPlanet-3-Recompiled`). Worktree: `D:\Recomps\lbp3-wt\seth-small`.
- **shadPS4 `lbp3-seth-small`**, from `37508fd9`, at `694fe2ce` (`rexyness/shadPS4`). Worktree: `D:\Recomps\lbp3-wt\seth-small-shadps4`.

| Commit | Item |
|---|---|
| shadPS4 `76e80636` | sgdc3 `80ffa53`: the texture cache asks whether an image has enough mips **and** layers (`SubresourceExtent::Contains` at the recreate, expand and overlap-resolve checks) |
| shadPS4 `de2c41bc` | sgdc3 `7184f52`: `LoadGraphicsPipeline` clears `fetch_shader`, `infos` and `modules` on entry (the early returns left the last pipeline's stages behind) |
| shadPS4 `f4858977` | sgdc3 `2966581`: a colour-buffer clear gets `RemapSwizzle(format, Identity)` (the packed 1_5_5_5, 11_11_10, 10_10_10_2 and 4_4_4_4); still never the buffer's comp_swap (`876508ce`). The first 3 changed clears are logged |
| runtime `936e079` | Windows 11 timer resolution kept while the window is hidden (`SetProcessInformation`, IGNORE_TIMER_RESOLUTION, state 0); `LBP3_TIMER_KEEP=0` leaves it out |
| runtime `b22e1e7` | Frame-generation cut rule: also the camera's world move (`+0xd0`) and view turn (`+0xc0`); a cut when the camera moves more than 150 units and 6× the frame before, or turns more than 6° and 6× the turn before. Every cut logged with its rule. `LBP3_FRAMEGEN_CUTREL=0` turns it off |
| runtime `a1843bf` | The Garlic pool (`0x5bf55`) grows above 1440p: +320 MiB per extra 1080p of pixels, in 2 MiB steps (2,658 MB at 4K); unchanged up to 1440p |
| runtime `42d527a` | Launcher: Play first copies the saves to `auto_<stamp>` when they changed since the newest automatic copy, and keeps the newest 10 automatic ones; manual and before-restore copies are never deleted |
| runtime `ae56ffc` | Submodule pointer to shadPS4 `f4858977` |
| shadPS4 `694fe2ce`, runtime `cbe2c29` | The clear-remap line raised from info to warning: info lines never reach the game log, so the first run couldn't show it |

**Credit and licence:** sethlikepie's littlebigplanet3-pc `07cc7e7` (GPL-2.0-or-later), and sgdc3 for the three shadPS4 fixes; named in each commit. `CREDITS.md` gets them at release time.

## Tests

- **Build:** clean, 1,442 steps. The only warnings in the changed files are in lines that were already there.
- **Unit tests:**

  | Test | Checks | Failed | Covers |
  |---|---|---|---|
  | `framegentest` | 120 | 0 | 13 new checks for the cut rule |
  | `restest` | 2,672 | 0 | the pool size at every listed size |
  | `launchertest` | 425 | 0 | `TestAutoBackup`, on a temporary folder |
  | `clocktest` | 16 | 0 | |

- **In game:** `_tools/seth_small_regress.sh seth1`.
  - **How it ran:** muted, under the game lock, on a copy of the em lane's shader cache.
  - Each milestone suite starts from the baseline save and puts the human's saves back.

| Check | Result |
|---|---|
| Introduction ×2, steps ≠ 1 | 0.00% / 0.13% (gate: under 1%); frame averages 33.3–35.0 ms |
| Thruster recipe ×3 | 3/3 OK |
| Legend of Bunkum | 16/16 voice lines; Needlepoint loaded; no auto-skip |
| Create sky | cyan (121, 177, 182) |
| Frame generation (warp) through the Introduction | 1 old-rule cut (camera jump); **1 new-rule cut** (camera move) |
| Frame generation through the map, the Bunkum cutscene and Needlepoint | 17 camera-jump cuts and 11 image cuts (old rules); **0 new-rule cuts** |
| 4K boot to Home | `pool: Garlic 1698 -> 2658 MB (scaled with the size, above 1440p)`; mapped at 2,658 MB; Home renders |
| 1440p boot to Home | no pool line; Garlic 1,698 MB as before |
| `ExpandImage` / overlap-resolve failures per gameplay run | 19 / 0, the same as every earlier build on the same scripts (`mua`, `mub`, `r014b`, `sl`) |
| Introduction frame times against earlier builds | 33.3–35.0 ms averages, 5 and 3 frames over 40 ms; earlier builds `sl`, `mub`, `mua`: 33.3–35.6 ms, 3–5 over 40 ms |
| Clear remap line | not visible in this run: it was info-level, and info lines never reach the game log. See `seth2` below |
| `seth2` (`cbe2c29` / `694fe2ce`, `_tools/seth_small_quick.sh`): Bunkum route + 1 Thruster run | Bunkum 16/16 and Needlepoint; Thruster OK; Create sky cyan (121, 177, 182); **0 remapped clears**, while 157 other warning lines reached the logs; clone exe restored by the script's trap |
| Device lost; nvlddmkm or Display events | none |
| Timer line | `refused, an older Windows (error 87)` on this Windows 10 PC |

## What the results say, and don't

- **The cut rule's one new cut** came as the Introduction starts, during the story save.
  - The picture was nearly still (image diff 0.2) while the camera jumped 2,706 units after 131.
  - The old rule just missed it: 3,032 is under 4 × the 1,575 before.
  - Dropping one in-between frame over a still picture is invisible.
- **It added no cuts during play,** through the map or through the Bunkum cutscene.
- **The case it's for wasn't exercised.** That case is a cut between two nearby shots that stays under the jump threshold; sethlikepie names one in the Heroes cutscene.
  - Bunkum's cuts are all big jumps, caught by the old rules.
  - To see it fire, play the Heroes cutscene with frame generation on, and look for `[framegen] cut (camera move|camera turn)` lines.
- **The clear remap** isn't exercised by LBP3 on the Bunkum route or the Thruster recipe's Create level. `seth2` shows no warning lines for it. It's for exactness.
- **No tool reads the changed log lines.** Nothing in `lbp3-wt\_tools` or `.scratch\lbp3-port\tools` reads the old `[framegen] cut:` or `[framegen] on:` lines.
- **`80ffa53`** changed no texture-cache behaviour on these routes: the `ExpandImage` and overlap-failure counts are the same as before.
- **`7184f52`** only matters with a damaged or pruned shader cache, which these runs didn't have.
- **The Windows 11 timer fix** can't be shown on this PC (Windows 10 refuses the flag). Its effect, keeping 1 ms waits while the game is minimised, is unverified until a Windows 11 player tries it.
- **The pool** is a margin, not a measured need: 4K loaded without it before.
- **The automatic backup** was tested on temporary folders only. Pressing Play on the real install wasn't done.

## An incident during the run

- **What happened:** the first version of `seth_small_regress.sh` sourced `saves.sh` in its own shell for the boot section. `saves_keep` sets `trap saves_return EXIT`, which replaced the script's restore trap.
- **What it left behind,** when the run ended:
  - the clone's `build\lbp3.exe` was still the `feat/seth-small` build;
  - the original was in `build\lbp3.exe.seth_bak`;
  - the game lock was still held by `sethsmall`.
- **Put right by hand afterwards:**
  - the original was copied back and checked (md5 `7edc135f…`, the copy taken before the swap);
  - the `.seth_bak` copy was deleted;
  - the lock was released.
- **No harm to saves.** The saves had already been returned (no marker or kept copy left).
- **The fix:** the script now runs that section in a subshell (it passes `sh -n`). The new single-suite runner `_tools/seth_small_quick.sh` runs each suite in its own shell; its `seth2` runs restored the exe and released the lock as designed.
- **The window was 43 seconds.** The run took 14:47–15:09:01 and the restore was at 15:09:44. Anything started from the main clone between those two times used the `feat/seth-small` build.

# Batch 2 (2026-11-19, "bro you keep on working")

Tier 1 items 1, 2 and 4. New branches on top of Batch 1, so Batch 1 can be merged on its own:
- **runtime `feat/seth-batch2`** at `60d63ae`;
- **shadPS4 `lbp3-seth-batch2`** at `b0242a25`.

**Update, after the regression: the wait policy is opt-in** (shadPS4 `b0242a25`, runtime `60d63ae`).
- **Why:** lining the cold route's waits up with the loads, `perframe` waited about 4.8 s around the boot's first Pod frames, 6.2 s in one window of the menus before the level, and up to 0.6 s at the level's start. Background builds are on by default online to stop first-time freezes in play.
- **Now:** `all` is the default again, so every other draw may be skipped, as before. The capture wait still applies.
- **`perframe` and `geometry`** are opt-in.
- **The default is the user's call:** the Pod poster against those waits.

**Left to do (stopped at the user's request, a CPU-heavy job of theirs running):**
1. The photo test under the default `all`. The capture wait runs before the policy check, so it should hold; `sb2photo` passed under `perframe`.
2. ~~A screenshot comparison of `seth1` against `sb2`.~~ Done without a game run. See [the comparison](#screenshot-comparison-batch-1-against-batch-2) below.
   - No picture or brightness change at the run-to-run noise level in the scenes the regression shoots.
   - Home's floating card sits elsewhere in Batch 2. That's most likely animation phase, but it's open: check it with two Home shots and an `LBP3_F91=0` boot at the next game run.
3. A ticket for `OnSubmit` never running (texture-cache GC, staging trim). Drafted and waiting for the user's go-ahead. The cause is the runtime's `sceGnmSubmitDone` stub; see [the wait policy](#the-wait-policy-item-2).

Both are pushed to the forks and not merged. Same worktrees as Batch 1.

| Commit | Item |
|---|---|
| shadPS4 `4e29781d` | **Write-fault window** (item 1): a guest thread's write fault also unprotects the clean, buffer-tracked pages of the aligned 256 KiB around it, except pages with GPU-modified data (`mask & ~gpu`). Not on the GPU thread. `LBP3_FAULT_WINDOW=<KiB>`, default 256, 0 = off |
| shadPS4 `91372593` | **Shader translation** (item 4): `v_rsq_clamp_f32` / `v_rcp_clamp_f32` clamp to ±FLT_MAX; `v_writelane_b32` writes one lane (sgdc3 `eb51f01`); `s_and_b64 exec, sA, sB` opens an exec scope (sethlikepie's F91; `LBP3_F91=0` off, with an empty cache). `ShaderBinaryVersion` 7 → 8 |
| shadPS4 `04cfbeb7`, `b0242a25` | **Background-build wait policy** (item 2): with `LBP3_ASYNC_PIPELINES=1`, every draw waits while a photo or thumbnail capture is queued. With `LBP3_ASYNC_POLICY=perframe`, one-time, full-screen, indirect and loading-screen draws also wait instead of being skipped. `all` (the default since `b0242a25`), `perframe` or `geometry`; `LBP3_ASYNC_STREAK` (2), `LBP3_ASYNC_MIN_DRAWS` (300) |
| runtime `ae8d8ee` | The `[faults]` and `[async]` status lines, and the flip interrupt ticking shadPS4's frame count |
| runtime `0390003` | Submodule pointer to shadPS4 `04cfbeb7` |

## The shader scan (item 4)

`tools/opcode_scan.py` (new, in this repo) runs over the 135,428 shaders `res1440_fullscan.py` extracts (re-extracted for this, into the job's temp folder: the clone's `analysis\res1440_scan` was empty). "Ran" means a `.meta`/`.spv` of that hash is in the player's own shader cache (10,006 hashes).

| Instruction | Shaders | Ran | Ours before |
|---|---|---|---|
| `v_rsq_clamp_f32` | 135,191 (all but 237) | 4,522 | plain `rsq`: `rsq(0) = +inf`, so `normalize(0)` = NaN where the PS4 gives 0 |
| `v_writelane_b32` | 3,127 | 43 | zero for the whole register: every SGPR spilled through it came back as 0 |
| `s_and_b64 exec` (not the discard form) | 53,549 | 284 | no exec scope: the VALU code after it ran on every lane |
| `rcp_legacy`, `rsq_legacy`, `log_clamp`, `log_legacy` | 0 | 0 | not used by LBP3, so upstream `#5346` isn't taken |

- **Cost to players:** the version bump means every player's shader cache is rebuilt once. That's one round of first-visit pauses everywhere.

## The write-fault window (item 1)

Warm Needlepoint route, window off and on alternately, twice (`_tools/seth_fault_ab.sh fa1`, on the build before item 4):

| | Off | 256 KiB |
|---|---|---|
| Write faults per status window | 18,187 / 18,703 | 2,582 / 2,586 (−86%) |
| Graphics blocked, mean per frame | 0.26 / 0.25 ms | 0.13 / 0.14 ms |
| Graphics blocked, worst window | 3.3 / 3.5 ms | 2.3 / 2.3 ms |
| Walk work | 19.96 / 20.49 ms | 19.39 / 20.67 ms |
| Session frames over 40 ms | 27 / 23 | 26 / 27 |
| Re-uploads per frame | 26.9 / 26.1 MB | 26.1 / 26.1 MB |
| Boot to the Pod: faults per window | 21,025 | 730 |

- **The blocked time halves,** but the walk's work doesn't move. The frame already fits its 33 ms slot on this PC, so the gain is headroom for slower PCs.
- **No visual change:** the Pod and Needlepoint screenshots are identical off and on.
- **Not done:** gfx_wait.md's fault-address log. The counts answered its question: most faults were windowed away.

## The wait policy (item 2)

- **What the first runs showed (`sa1`):** every new pipeline waited, which is worse than building without background builds at all.
- **Why:** `Rasterizer::OnSubmit` never runs in this port, so the frame counter never moved. The runtime's flip interrupt now ticks it.
  - **Corrected later the same day:** the cause first written here, "LBP3 never calls `sceGnmSubmitDone`", is not what the sources show.
    - The game imports it: NID `yvZ73uQUqrk#W#X` is in `analysis\eboot.elf`.
    - The runtime binds that import to its own stub (`runtime/src/hle/videoout.cpp:766`), which returns OK and never reaches shadPS4's `Liverpool::SubmitDone`.
    - How often the game calls it hasn't been counted. The same wording is in the comment at shadPS4 `vk_rasterizer.cpp:649`, left alone so the branch heads stay on the tested builds.
- **What `OnSubmit` would have run** (shadPS4 `vk_rasterizer.cpp:653`):
  - **The texture cache's garbage collector** (images and samplers). Images are only freed by overlap and unmapping here. Upstream, the collector starts at `(budget − min(budget, 8 GB)) / 2` of VRAM in use, which is 0 on cards with 8 GB or less: there it runs every frame.
  - **The staging pool's trim.** So `research/sethlikepie_gpu.md` 2g's "blocks live ~10 s" is wrong: they are never trimmed.
    - The 16 MB ring blocks stay at the session's peak.
    - A request over 16 MB, or a deferred one, gets a new buffer for every new size class (`RequestLarge`), and only `TrimLarge` frees those.
    - Unmeasured: the runtime prints no VRAM or staging figure.
  - **The buffer cache's `TickFrame`: harmless here.** It only processes the GPU fault buffer, which is requested only for shaders that use DMA, and the runtime's shim turns DMA off (`runtime/shim/core/emulator_settings.h:30`).
    - That header is the one compiled: `runtime/cmake/shader_recompiler.cmake:41` puts `runtime/shim` before shadPS4's `src`, `video_core` inherits it, and shadPS4's `emulator_settings.cpp` isn't built.
  - **`ProcessDownloadImages`: covered.** `OnFence` runs it too, at every `EventWriteEop`, `EventWriteEos`, `ReleaseMem` and `WriteData` packet (`liverpool.cpp:896–1441`).
- **That deserves a ticket of its own.**
- **Cold caches** (shader and NVIDIA driver), `_tools/seth_async_ab.sh sa4`, background builds on, one run each:

| | Skip all (as `84e058af`) | perframe, streak 8 (sethlikepie's) | **perframe, streak 2 (default)** | geometry |
|---|---|---|---|---|
| Boot: skipped / waited | 3,051 / 12 (46 ms) | 0 / 175 (5.1 s) | 0 / 180 (5.0 s) | 186 / 61 (1.1 s) |
| Pod window's reflected poster | **missing** (also in `sa3`) | drawn | drawn | drawn |
| Route: skipped / waited | 10,801 / 19 (44 ms) | 800 / 450 (14.3 s) | 574 / 359 (13.0 s) | 4,225 / 125 (3.0 s) |
| Walk: frames over 40 ms (worst) | 1 (133 ms) | 11 (1.8 s) | **0 (33.7 ms)** | 31 (560 ms) |
| Session: frames over 40 ms (worst) | 29 (3.0 s) | 44 (6.0 s) | 45 (6.3 s) | 65 (3.0 s) |

- **The bug it fixes exists today:** background builds skip a one-time render, and the reflected poster in the Pod's right window never appears. That's on a cold cache, and background builds are on by default online since v0.1.6. The reference is the warm, background-builds-off boot (`fa1_w0_pod`), which has the poster.
- **The cost is time waiting on a first, cold visit,** mostly during loading screens: 13 s over the whole route, against 44 ms. With streak 2 the walk itself had none.
- **This is a taste call** (no missing pictures vs longer first loads). `LBP3_ASYNC_POLICY=all` brings back the old behaviour.
- **Single cold runs vary a lot,** so geometry's bad walk may be noise.

## Regression (Batch 2 build)

**How it ran:** `_tools/seth_small_regress.sh` with `SETH_CACHE=seth_cache8` (a fresh folder for cache version 8), at runtime defaults (fault window on; background builds off).
- **`sb2w`** warmed the new cache. Its first Introduction run never reached the level: the scripted presses outran the shader builds, as expected on an empty cache.
- **`sb2`** is the measured run.
- **`sb2photo`** is the photo test with background builds on and empty shader and driver caches (`seth_small_quick.sh`).

| Check | `sb2` |
|---|---|
| Introduction ×2, steps ≠ 1 | 0.78% / 0.13% (gate: under 1%); frame averages 33.3–35.0 ms, 5 and 4 frames over 40 ms. Earlier builds on the same script: 0.00–0.73% (`r014a` 0.52 / 0.73, `sl` 0.26 / 0.46, `mua`, `mub`, `r014b`, `seth1` ≤ 0.26): top of the usual noise |
| Thruster recipe ×3 | 3/3 OK |
| Legend of Bunkum | 16/16 voice lines; Needlepoint loaded; no auto-skip |
| Create sky | cyan (121, 177, 182) |
| Frame generation through the Introduction and through the map, Bunkum and Needlepoint | the same cuts as Batch 1 (the one camera-move cut at the Introduction's start) |
| 4K / 1440p boots | Garlic 2,658 / 1,698 MB; 990 flips each |
| `ExpandImage` / overlap failures / device lost | 19 / 0 / 0 per gameplay run, as every earlier build |
| nvlddmkm or Display events | none |
| Photo, background builds on, cold caches | PASS: encoder line, capture readback, a 30,852-byte 640x360 JPEG that isn't black |
| Clean-up | the clone's exe restored by both scripts (md5 `7edc135f…`); lock free; no marker or kept copy |

**Not shown:**
- **F91's target**, the power-up wheel: the routes don't open it.
- **The writelane fix's 43 shaders:** which of them the routes run, and what they looked like before.
- **NaNs before the clamp fix:** whether any were visible.
- **A Windows 11 or slower-PC run.**
- **A full play-through with background builds on and the new policy:** only the cold route, boots and the photo test.

## Screenshot comparison, Batch 1 against Batch 2

Done on 2026-11-19 from the shots already taken; no game run.
- **Why:** item 4 changes how nearly every shader translates, so this looks for any picture change, brightness included (ticket 17).
- **Tool:** `tools/shot_groups.py` over the regression's shots in `analysis\screenshots`. Batch 1 is `seth1` and `seth2`; Batch 2 is `sb2` and `sb2w`.
- **Method:** each shot's difference across the two batches is set against the difference between two runs of the same batch.
- **Columns:** mean absolute pixel difference, 0–255. "Clipped" is the share of pixels with a channel at 250 or more.

| Shots | Across batches | Same batch | Mean luma, B1 → B2 | Clipped, B1 → B2 |
|---|---|---|---|---|
| Bunkum map, with and without frame generation | 1.3–2.2 | 1.0–1.9 | 93.6–93.7 → 93.5–93.6 | 9.8% → 9.8% |
| Bunkum end, with and without frame generation | 1.0–4.2 | 2.0–4.4 | 122.9–123.1 → 122.9–124.1 | 12.2% → 12.2–12.3% |
| Pod at boot, 1440p and 4K | 2.4–3.7 | 2.2–2.9 | 36.9 → 36.8–37.0 | 0.9 / 1.1% → 0.9 / 1.1% |
| Introduction level, two runs (`sb2` only*) | 0.8–0.9 | – | 215.2–215.3 → 215.2–215.3 | 54.5% → 54.5–54.6% |
| Introduction level, frame generation | 1.1–2.5 | 2.4 | 215.2 → 214.8–215.2 | 54.8% → 54.4–54.6% |
| Thruster recipe, 3 rounds × dialog, level, after (round 3 `sb2` only*) | 0.2–4.6 | 0.4–4.7 | 98.1–110.8 → 98.1–110.8, each shot within 0.3 | 0.6–1.7%; within 0.5 points |
| Home, at boot and before each Introduction (5 shots) | 5.8–10.9, worst 32×32 block 105–113 | 3.6–5.2, worst block 31–57 | 37.3–38.4 → 37.3–38.0 | 1.9–2.6% → 1.9–2.5% |

\* `sb2w` is the cache warm-up run.
- Its two Introduction level shots show the map, because that run never reached the level.
- Its third Thruster round's shots are 1×1 failed captures.
- Its first Home shot before the Introduction shows different content. All of these are left out.

**What it says:**
- **No picture or brightness change from Batch 2 in these scenes, Home aside (below).**
  - Each shot's difference across the batches is inside or just above its same-batch range: Bunkum map 2.2 against 1.9, Pod 3.7 against 2.9.
  - A same-batch range is only one or two pairs here. The real noise floor is the Bunkum map's 16 earlier runs, which differ from each other by 0.95–5.0.
  - Mean luma moves by at most 1.2, and only where it moves as much between two runs of one batch.
  - Clipping moves by at most 0.5 points (Thruster dialog), against up to 0.4 between two runs of one batch.
- **Bunkum end's static parts agree to within 0.4 luma in all four runs.** That's the sky and the lit stage, the 4×3 grid's cells except the left column (`--cells`).
  - The left column varies from run to run: 78–84 at the top.
  - In the frame-generation shot, `sb2w` equals `seth1` there.
- **Home: open, most likely animation phase.**
  - **What differs:** the only change is where the "Recently Edited" card (with its "Edit Now" tag) sits.
    - The Play/Create menu and the bottom buttons align at zero shift in every Home shot, residual 0.2–1.8 (up to 3.2 between 1440p and 4K).
    - `tools/card_shift.py` (±40 px) measures the card's vertical offset, each suite against a reference from the same suite.
  - **Boot shots, against `seth1`:**
    - `seth1`'s card is at the same place at 1440p and 4K (−1 px).
    - `sb2` and `sb2w` put it 21–22 px lower at 1440p and 11–13 px lower at 4K. The two runs agree within 2 px.
  - **Pace shots, against `pace_seth1_intro1_home`:**
    - 10 earlier main builds (`sl`, `mua`, `mub`, `r014a`, `r014b`, `mergerg`, `emrg`, `bootrg`, `asrg`, `sn5_gate`) land anywhere from −4 to −16 px, and a run's two shots differ by 0–5 px.
    - `seth1`'s own shots: 0, −6, −9.
    - `sb2`: −10, −23, −23.
    - `sb2w`: −23, −23.
  - **Reading:**
    - The card's position isn't fixed by the game's code: builds that draw Home the same way spread over 12 px.
    - Batch 2 lands about 7 px beyond that spread in the pace shots, and the other way at boot.
    - **Inferred:** Batch 2's changed timing catches the card at a different point of its motion. The fault window cut boot faults per window from 21,025 to 730.
    - **Also possible:** a larger swing.
  - **Check at the next game run:** two Home shots a second apart, and a boot with `LBP3_F91=0` on an empty cache.
- **Not covered:**
  - Needlepoint: the regression takes no shot there.
  - The power-up wheel (F91's target).
  - Whichever of the 43 `v_writelane_b32` shaders run elsewhere.
  - Any spot where a NaN was visible before the clamp fix.
  - This is "no change in these scenes", not "no change anywhere".

**A brightness change that isn't Batch 2's: v0.1.7's lighting fix.**
- **The 14 Bunkum runs from 13–14 November** (`bootrg` to `sn5_gate`) show the end shot at mean luma 130.3–132.4 and median 128–133.
- **All four Batch 1 and Batch 2 runs** show 122.9–123.7 and median 116–118.
- **The drop is in the lit foreground:** the lower third (basket, cloth, grass) is about 20 darker, from 92–100 to 72–80. The sky cells don't change at all.
- **The cause is main `a2e5ea9`** (16 November, v0.1.7, `LBP3_STACKED_TARGETS`). Its release notes describe the same effect: "shadows and mid-tones have their depth back", with Needlepoint's average brightness down from 136 to 108.
  - It's inferred, not isolated: no run of main `29dad5a` on this script exists.
  - For ticket 17, this is the band that was too bright against the PS4 (the lower band, luma p50 124 against 86–93). Ticket 17 still says parked from 12 November.

## An incident: a renamed exe kept running (2026-11-19, 16:52–19:42)

- **What happened:** for the wait-policy A/B (`sa2`) the build was frozen as `build\lbp3_b2a.exe`. Every `taskkill //im lbp3.exe` in the scripts, `saves.sh`, and the game lock's "is a game running" check go by the name `lbp3.exe`, so no instance was ever closed.
- **What it left:**
  - three `lbp3_b2a.exe` were running when you stopped the run at 19:42;
  - the lock was held by `sethasync` for nearly three hours;
  - the fault-window A/B never ran.
- **Cleaned up by another session:**
  - the games and the shell were killed;
  - the lock was released;
  - your saves were checked: untouched, no marker or kept copy.
- **I removed** the six `seth_cold_*` folders.
- **`sa1`** (build `lbp3.exe`) is unaffected; the `sa2` numbers are discarded.
- **The fix:**
  - the frozen build is `build_b2a\lbp3.exe`, under the normal name in its own folder;
  - all four `seth_*` scripts in `_tools` refuse an exe not named `lbp3.exe` before taking the lock (tested);
  - long runs get a watchdog.
