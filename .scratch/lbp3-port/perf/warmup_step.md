# A "shader compile step" for first-visit freezes: feasibility (2026-11-13)

The human asked: "how do we solve this shaders thing? can we do shaders compilation step or sth". This file checks the plan:
1. an automated warm-up tour that loads every story level once on a test save, so the pipeline cache fills;
2. a boot warm-up that stays fast as the cache grows;
3. background compiling as a safety net (the human's call).

Offline only: Ghidra on the eboot and the human's play logs; the game was not run. Labels: **decompiled**, **measured** (logs, files), **inferred**. Companions:
- [first_visit.md](first_visit.md): compiles are once per level, ever.
- [compile_stalls.md](compile_stalls.md): where a freeze's time goes, and the cache layout.
- [shared_cache.md](shared_cache.md): warm-up boot times.
- [../research/lbppc.md](../research/lbppc.md): LBPPC's pipeline work. Their persistent `VkPipelineCache` and parallel warm-up are GPL-2.0-or-later and are being ported by another lane, so this file doesn't redesign them.

## Verdict

- **A tour is feasible: the game has a direct "go to level" call with no story-progress check in it (decompiled).** A runtime hook can launch every story level from a test save in sequence and fill the cache.
- **Loading a level and standing still only catches about a third of what a playthrough compiles (measured: 39% of pipelines, 30% of compile time in a level's first 60 s).** Cutscenes are the exception, almost fully covered by playing them.
- **So the compile step is a tour plus at least one coverage booster, with the human's own play and the safety net catching the rest:** let cutscenes run to the end, scripted movement through each level, and possibly a culling bypass.
- **Pre-translating all shaders is not worth doing (measured):** translation is 6% of compile time; the driver's pipeline build is 94%. That agrees with LBPPC's 96%.

## 1. The level jump (decompiled)

The script natives are the `{function, "Name__Sig"}` pairs in the eboot's relocations (3,916, the same count the runtime finds; listed offline by `%USERPROFILE%\.claude\jobs\f158f9f3\tmp\warmup\natives.py`). The level-change ones:

| Native | Address | Calls |
|---|---|---|
| `GoToTutorialLevel__Q6SlotIDQ20Game.EPlayOrEditMode` | `0x84dbf0` | converts the script's SlotID (`FUN_006e0250`), then `FUN_007c6780(SlotID, mode)` → **`FUN_002689c0(game = DAT_012ecc80, SlotID, mode)`** |
| `GoToTutorialLevel__gQ20Game.EPlayOrEditMode` | `0x84dc50` | `FUN_002689b0(game, id, mode)` → `FUN_002689c0(game, (u64)id << 32, mode)`, i.e. SlotID type 0 |
| `SetLevelUnlocked__Q6SlotID` | `0x84eab0` | `FUN_00261520(game, &SlotID)`: sets bit 2 of the slot's flags at `+0x34` (types 0, 1, 8, 11, 16 only) |
| `GetLevelSlotID__` | `0x84c780` | `FUN_007c75d0()`, the current level's SlotID |
| `IsLaunchingLevel__` | `0x826bf0` | whether a level change is in progress |
| `StartPreviousLevel__`, `StartPreviousPrimaryLevel__bb` | `0x84ed90`, `0x84eda0` | return paths |
| `PerFrameUpdate__Q18Game.EPlayerNumberi` | `0x846440` | a per-player, per-frame native on the game thread; a candidate tick for a tour |

**`FUN_002689c0(game, SlotID, mode)` is the general "go to level" (decompiled):**
- It has three gates:
  - a global state below 2 (`FUN_0033efe0(FUN_0033efd0(&DAT_0130d5b8, 0)) < 2`);
  - `FUN_003500e0(DAT_0130d5e0) == 0`;
  - the game's byte at `+0xd0` clear.
  
  Their meaning isn't decoded (inferred: not mid-change and not online-busy).
- It looks the slot up in the slot database (`FUN_00242af0`). If the slot exists, it fills a level-change request with the slot's level reference (`lVar7 + 0x10`), the SlotID and the play/edit mode (`FUN_0087b290`), and posts it to the loader (`FUN_0034f880(DAT_0130d5e8, …)`).
- **No unlock or story-progress check is in this path.** The lock bit that `SetLevelUnlocked` sets is read by the map UI, not here (decompiled for this function; whether the loader checks later is unknown).

**SlotID** is 8 bytes: low u32 = type, high u32 = id (`FUN_006e0250` packs `type | id << 32`; decompiled).
- The slot lookup (`FUN_00242af0`) switches on type:
  - 1/14: developer slots (`FUN_0028cc20`);
  - 2/15: server slots;
  - 4;
  - 8/9: DLC (`DAT_012ed148`);
  - 12: adventures;
  - **11, 13, 16, 17: levels inside an adventure,** looked up in an ordered map at the adventure's `+0x3f8` keyed by (type, id) (`FUN_0022b3c0`).
- **The story is adventure SlotID (12, 349)** (`FUN_004c9df0` defaults to `0x15d0000000c` = type 12, id 0x15d = 349; the other branch gives 351). The human's story save is named `LBPxSAVEx12x349` for that slot (measured).
- **Inferred: a story level's id is (349 << 16) | index.** Type 11 with one legacy id is remapped to `0x15d002c0000000b` = type 11, id 0x015D002C = adventure 349, level 0x2c. The `MakeAdventureLevelID__Q6SlotIDi` native suggests the same scheme.

**How a tour would enumerate and launch (inferred design; needs one game run to confirm):**
- On the game thread (a hooked per-frame native such as `PerFrameUpdate__`, or the `IsOnline__` pattern `my_unlocks.cpp` already uses), get the story adventure with `FUN_004c9df0(&p, 1)`.
- Walk its map at `+0x3f8` (red-black tree; key at node `+0x20`, value at `+0x30`) and log every level SlotID. Map them to the 46 archives with the `[fs] open …official_adventure_levels/…farc` line that follows each launch.
- Then for each level: `FUN_002689c0(DAT_012ecc80, slot, play)`, wait until `IsLaunchingLevel` clears and `FUN_007c75d0()` returns the slot, dwell or play it (section 4), then the next.
- Unknowns for that run:
  - whether the gates allow a launch from inside a level, or whether to return to the Pod between levels;
  - whether out-of-order story levels misbehave (scripts expecting progress);
  - the play/edit mode value.

**The 46 story level archives** (`gamedata/lbp3/official_adventure_levels/`, 44 folders; the game outro has `game_outro`, `_a`, `_b`). Visited means seen in any play log this week (first_visit.md):

| World | Archives | Visited | Not yet |
|---|---|---|---|
| Prologue (8) | craftworld, needlepoint_peaks, newtons_airship, stitchem_manor, stitchem_boss, intro, stitchem_gate, titan_released | 8 | — |
| Manglewood (12) | manglewood_hub, space, casino, diner, crypts, titan_1, meet_sackbeast, titan_01_intro, titan_01_outro, manglewood_bp, bite_size, mp_lift | 8 | titan_1, titan_01_intro, titan_01_outro, mp_lift |
| Ziggurat (13) | ziggurat_hub, torch, docking_area, top_down_platformer, caverns, titan_2, meet_giant, titan_02_intro, titan_02_outro, ziggurat_bp, m_mash, mp_rocket, giant_dwarf_quest | 0 | all |
| Bunkum (13) | bunkum_hub, boost_blaster, belly_beast, sky_caravan_a, save_sb, titan_3, meet_sackbird, sackboy_kidnapped, titan_03_intro, game_outro (+_a, _b), bunkum_bp | 0 | all |

13 of the 44 folders are cutscenes (`cNN`).

## 2. What a freeze is made of (measured; LBPPC agrees)

Summed `[perf] compiled N pipelines (X ms), M shaders (Y ms)` over the human's 7 sessions with compiles (fg, 1440, 1440a, 19:55, 21:59, unlocks, DLC):
- 2,622 pipelines took 60.9 s;
- 1,963 shader translations took 4.2 s.

So **translation is 6.4% and the driver's pipeline build 93.6%**: about 23 ms per pipeline on average (the DLC browser's pipelines averaged 30 ms) against about 2 ms per translation. LBPPC measured 96% and 28.6 ms median per pipeline on a cold RTX 4090. **Pre-translating the 135k shaders offline would save at most a few percent of each freeze** and isn't worth building. Only having the pipeline itself in the cache before the first visit removes the freeze.

## 3. Warm-up cost

- **Measured per key, serial, driver cache warm:**
  - 3,262 → 4,012 keys added 0.65 s to the Pod at 1440p;
  - 2,325 → 4,012 keys added 1.6–2.1 s at 1080p (shared_cache.md).
  
  That's about 0.9–1.2 ms per key.
- **A full tour adds an estimated 1,000–4,500 keys** (30–150 per unvisited archive, compile_stalls.md, plus the new characters), so serially about +1–5 s of boot.
- **The parallel warm-up being ported from LBPPC** (cores−1 threads) makes that roughly a tenth. Its persistent `VkPipelineCache` covers the cold-driver case after a driver update. Nothing more is needed here for the compile step.

## 4. Coverage: what a load-and-stand tour misses (measured)

Pipelines compiled in the first 60 `[perf]` windows (about 60 s) after each level's archive opened, against the rest of that visit, from the human's sessions:

| Level (session) | First 60 s | Later |
|---|---|---|
| Manglewood hub (1440) | 123 (375 ms) | 12 |
| meet_sackbeast, a cutscene (1440) | 45 | 1 |
| Diner (1440) | 48 | 23 |
| Space (1440) | 55 | 57 |
| Casino (1440) | 70 | **309 (8.6 s)** |
| Crypts (1440) | 24 | **165** |
| Manglewood quest (fg) | 59 | 35 |
| Bite Size (fg) | 18 | 12 |
| craftworld (1440 / 1440a) | 49 / 0 | 72 / 100 |
| **All** | **493 (6.2 s)** | **786 (14.6 s)** |

So **standing at a level's start compiles about 39% of its pipelines and 30% of its compile time.** Hubs and cutscenes are front-loaded. Platforming levels (Casino, Crypts) compile most of their content further in.

**What a tour misses even when moving:**
- event-triggered effects, boss phases (the three Titans);
- power-ups and gadgets picked up mid-level;
- the new playable characters (Oddsock in Manglewood, Toggle and Swoop later), whose shaders appear only when played;
- co-op-only paths (`mp_lift`, `mp_rocket`);
- Create mode and community levels (about 13,500 materials).

**Cheap ways to raise coverage, best first:**
1. **Let every cutscene level run to its end** (13 folders, a few minutes each). Close to full coverage for them, measured on meet_sackbeast.
2. **Scripted movement in each interactive level:** hold right and jump on a timer for 2–3 minutes, using the remote-input commands the test scripts already use. Crude, but it walks the camera through most of a level's art. Coverage is unmeasured; a run per level against the human's later real visit would measure it.
3. **A culling bypass while touring (inferred, unverified):** `spu.prx` has a `FrustCull` job (fps60_spu_re.md). If what the engine draws is limited by that culling, forcing it to keep everything in tour mode would draw, and so compile, a level's whole art at load. Needs a check that culling is that job, and that drawing everything doesn't break the frame.
4. **The human's own play** already records every pipeline it compiles (the cache persists). A tour only has to get ahead of them.
5. **The safety net:** background compiling for whatever's left (LBPPC's `BB_ASYNC_PIPELINES` design, reusable; the human's call, perf/first_visit.md has the constraints).

## Recommended design

- **`LBP3_WARMUP_TOUR=1` (runtime; test save only, never the human's):** after the profile loads in the Pod:
  - enumerate the story adventure's levels (section 1);
  - for each level, launch it with `FUN_002689c0`;
  - wait for the load;
  - if it's a cutscene, let it run to the end; otherwise run the scripted movement for a fixed time;
  - log `[tour] level <slot> <archive> compiled N pipelines`;
  - move to the next level.
  
  Run it at `play.bat`'s settings (1440p, frame generation), so the keys include the 17 rescaled shaders' salted pipelines.
- **The tour writes into a COPY of the shared cache** (`LBP3_SHADER_CACHE_DIR`). The copy is then installed the same way as the merged cache (back up, then copy). Keys are only ever added, so this is safe to merge.
- **Validation before handing it over:**
  - run the tour on a cache copy from before the human's Manglewood sessions (`CUSA00473_pre_shared_20261113_0043`);
  - compare its keys against the keys the human's real Manglewood play produced;
  - the hit rate is the tour's coverage. Do it once per booster, so the boosters' worth is measured, not guessed.

**Effort:**
- One session for the hook (enumerate, launch, wait, next) and its logging, plus one game run to confirm the SlotIDs, the gates and whether levels launch out of order.
- Half a session for the scripted movement and cutscene waits.
- The culling bypass is a separate check of about an hour.
- Then the validation run (about 2–3 hours of game time for 30 levels with movement).

**Risks:**
- **Out-of-order launches:** story scripts that expect earlier progress could stall a level or crash it. Use a watchdog with a timeout per level, and skip it.
- **A test save that ends up in a strange state:** use a scratch copy each run.
- **Gate conditions:** launching may need returning to the Pod between levels.
- **Coverage:** below the human's real play for interactive levels unless movement or the bypass works. Measure it before claiming "no freezes".
- **Boot time:** parallel warm-up (being ported) is the mitigation.

## Tour results (2026-11-13, `feat/warmup-tour`)

**The level jump, confirmed in game.**
- **The list:** the story adventure lists 54 entries: type 11, ids 0x15d0001–0x15d003e (50, with gaps), and type 13, ids 0x15d0000–0x15d0003 (4).
- **From the Pod** (current slot type 5, id 0), all three gates are open (count 1, loader 0, busy byte 0), and `FUN_007c6780(slot, 1)` loads the level. Mode 1 plays it.
- **From inside a level:** each next launch in the sweep came from inside the previous level and worked, with no Pod round trip needed.
- **Type 13 doesn't work:** ids 2 and 3 never start ("the level never started" after 75 s); ids 0 and 1 only re-opened Manglewood archives with 0 compiles.
- **The index → archive map** is `%USERPROFILE%\.claude\jobs\f158f9f3\tmp\tour\mapA.txt`. The ids are NOT in story order: Manglewood is spread over indexes 4, 18–23, 33–38, Ziggurat is 5–17, and Bunkum is 24–32 and 43–49. Six type-11 entries (8, 27, 37, 39, 41, 42) opened no new archive (sub-levels of an already loaded one).

**The first run crashed in shadPS4.** Jumping from the Pod into `01_prologue/c01_intro` drew a LineLoop (primitive type 18), and `LiverpoolToVK::PrimitiveType` hit UNREACHABLE. Fixed on shadPS4 `feat/warmup-tour` `fd3f9cbb`: LineLoop is drawn as a line strip, without its closing segment, with a warning.
- No other level drew one in the sweep.
- No human play log ever hit it; their normal route into Bunkum doesn't draw it.
- **Any build without that fix that boots a cache containing the LineLoop pipeline will crash in WarmUp.**

**Sweep A: all 54 entries, 20 s each, cutscenes capped at 30 s.**
- **Settings:** play_best's (1440p with the shader swap, frame generation, DLC), into `user\shader_cache_best`. Run 06:39–07:12 in two chunks.
- **Result:** 52 loaded, 2 skipped (type 13), 0 crashes. 712 pipelines compiled (540 + 172). The best cache went from 4,008 to 4,718 graphics pipeline keys.
- **Biggest first-load sets:** the Ziggurat hub (78), Ziggurat's giant/dwarf quest (71), Bunkum's hub (57), boost_blaster (51), docking_area (37), m_mash (31).
