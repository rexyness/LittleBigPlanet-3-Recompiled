# One shader cache for 1080p and 1440p (2026-11-12 night)

[first_visit.md](first_visit.md) found that pipelines persist fine, with one exception. The 1440p shader swap (`LBP3_RES=1440` with `LBP3_RES_SHADERS=1`, which `play.bat` sets) moved the whole shadPS4 cache to `user\shader_cache\CUSA00473_res1440`. So everything first compiled at 1080p compiled once more at 1440p: 8 visited levels, the menus and the Pod.

Labels: **code** = read in shadPS4; **measured** = run on the cache files offline; **untested** = still needs the game.

## How shadPS4 names its cache (code)

- **A program's identity is `params.hash`,** the shader hash the game's binary carries (`AmdGpu::GetParams`, `regs_shader.h`). It is the only place the hash is read.
- **SPIR-V:** `<pgm>_<idx>.spv`, one file per permutation, numbered in discovery order.
- **Permutation meta:** `<perm>.meta`, with `perm = HashCombine(pgm, idx)`. The header is `u32 version, u32 version, u64 perm, u64 idx` (measured: all 1,921 metas), followed by the specialization and the shader info.
- **Graphics pipeline:** `g_<XXH3_64(key)>.key`. The key is the 344-byte `GraphicsPipelineKey`, whose first 6 `u64` are the stage perm hashes (measured: all 2,313 + 3,254 key names equal XXH3 of bytes 8–352).
- **Compute pipeline:** `c_<perm>.key`.
- At boot, `WarmUp` loads every key, its metas and its SPIR-V. A draw finds its pipeline by key, and a program's permutation by matching the specialization.

## The change (shadPS4 `fix/shared-cache` `b789a896`; runtime `fix/shared-cache` `1407467`, pointer and test)

- **`common/lbp3_res.h`** (new) holds THE list of rescaled shaders (moved from `recompiler.cpp`; 44 since the scan lane's `fix/res1440-shaders` was merged in, see below) and a fixed salt (a splitmix64 finaliser of `hash ^ "RES1440"`). `StoredHash` returns the salted hash only for a listed shader, only with the swap on, and only with the shared cache.
- **`GetParams`** now returns `Lbp3Res::StoredHash(bininfo.shader_hash)`. Everything downstream follows from that: the program map, the SPIR-V and meta names, the perm hashes in the pipeline keys, and so the key names.
- **At 1440p,** the rescaled shaders and every pipeline that uses one live under names a 1080p session never looks up, and 1080p's entries for them are never picked up at 1440p. Everything else is shared.
- **The recompiler** recognises a listed shader in either form. Its log line names the original hash and the cache key: `[res] shader 0x5ed16706fc9e0eb1: N 1080p literals scaled for 1440p (cache key …)`.
- **`cache_storage.cpp`** uses `<serial>_res1440` only with `LBP3_RES_CACHE=separate`, which restores the old scheme exactly: no salt, separate folder.
- **Side effects (code):**
  - At 1440p, WarmUp also loads the 1080p translations of the 17 shaders, and the reverse at 1080p. They're harmless (never matched) and a handful of extra pipelines at boot.
  - The 1440p logs and dumps show the 17 listed shaders under their salted hash. Nothing in the port keys on those hashes: the fills hook keys on `5c5e1be6`, which isn't listed, and the frame-generation hooks find the composite by role.
- **Tests (measured, 44-shader list):** the build passes, and so do `rescachetest` (new, 148 checks) and `cooptest`, `dlgtest`, `osktest`, `dialogtest`, `jpegtest`, `framegentest`, `audiotest` and `addconttest`, all with 0 failed. `imetest` wasn't run: it reads the live pad, and another lane had the game.

## Carrying the existing 1440p folder over (`tools/res_cache_merge.py`, `tools/xxh3tool.cpp`)

Without a merge, everything built at 1440p since 15:40 would compile once more with the shared cache (the DLC costume browsing, the Manglewood levels first seen at 1440p). The tool writes a NEW folder: a copy of `CUSA00473` plus what's safe to add from `CUSA00473_res1440`. Neither input is ever written. It matches by content, because permutation numbers follow discovery order and differ between the folders:
- **The 17 listed programs** are skipped: in the res1440 folder they are the rescaled translations under their original names.
- **Programs absent from main** are copied as they are.
- **Programs in both:** a permutation with identical SPIR-V maps to main's index. Anything else is appended after main's permutations, with the meta's perm and idx rewritten. The metas of the two folders always differ in session-dependent addresses (the 1440p memory layout), so only the SPIR-V is compared.
- **Keys** get their stage hashes rewritten through that map and are renamed by XXH3. `xxh3tool.exe` computes it, built from shadPS4's bundled `xxhash.h`; the tool first checks it against every existing key name and stops on any mismatch. Nothing in main is overwritten; it stops on any name clash.

**Dry run on the user's folders (measured, read-only):**

| Folder | Programs | Metas | Keys |
|---|---|---|---|
| res1440 | 2,361 | 2,570 | 3,262 |
| main | 1,641 | 1,921 | 2,325 |

- **Programs:** 16 listed ones skipped, 1,120 new, 1,225 shared. In the shared ones, 1,368 permutations matched main by SPIR-V and 10 were appended.
- **Keys:** 1,696 added, 1,464 already in main (an equivalent pipeline exists), and 102 unusable (they use a rescaled shader, so they compile once more at 1440p).
- **Written** to `D:\Recomps\lbp3-recompiled\analysis\shared_cache\merged44_CUSA00473` (with the 44-shader list; the earlier 17-list copy was identical and is removed): 10,209 files, main's 6,168 plus 4,041. Every graphics key's stage hashes have a meta, every meta has its SPIR-V, and every key name equals XXH3 of its key.
- **Not applied** to `user\shader_cache`. The user's two folders are unchanged.

## Still to do in game (untested)

On COPIES in a scratch cache folder, never `user\shader_cache`; shadPS4's cache path follows the user dir, so run from a scratch working copy:
- **(a)** a 1080p session, then a 1440p session of the same route: the second should compile only the pipelines using the 17 rescaled shaders;
- **(b)** at 1440p, the listed hashes are logged as rescaled, and the SMAA edges and composite match the current separate-cache build (pixel-identical or within noise);
- **(c)** a 1080p session after a 1440p one matches `port` at 1080p;
- **(d)** boot with the merged copy: WarmUp loads it without "conflicts with index" warnings, and the Pod, a 1440p-only level and the costume browser show no compile lines.

- **(e)** the scan lane's check: the x-ray silhouette (`proc_base_xray_special`, Sackboy behind scenery) and the outline highlight (`proc_base_outline_special`, grabbable or highlighted objects) at 1440p against 1080p. Positions must line up, with no 4/3 stretch. Test saves and cache copies only.

**Switching the user over, after (a)–(e) pass:** rename `CUSA00473` to `CUSA00473_pre_shared` and `merged44_CUSA00473` to `CUSA00473`; keep `CUSA00473_res1440` as it is.

## Integration with the shader scan (2026-11-12 night)

The scan lane (`perf/res1440_shader_scan.md`) found 27 more shaders needing the rescale, 44 in all: shadPS4 `fix/res1440-shaders` `b9eaec0f`, runtime pointer `2ca9761`. Both are merged into `fix/shared-cache`, so the rescale and the cache salt use one list, `Lbp3Res::kShaders` in `common/lbp3_res.h`. It matches the scan lane's 44 exactly, checked by a script.
- **Commits:** shadPS4 merge `5312fb6f`, then `b789a896`, which puts the 44 entries in the header (the merge itself had kept 17). Runtime merge `eb75266`, then `1407467` (`rescachetest` checks the 44).
- **Tests:** rebuilt; `rescachetest` gives 148/0 and the other 8 offline programs pass. `imetest` wasn't run, because the verification lane had the game running.
- **`res_cache_merge.py`** now reads the list from that header (`--list`; the default is the clone's submodule once merged).
  - Rerun with 44 into `merged44_CUSA00473`: the same result as with 17 (16 listed programs skipped, 1,696 keys added, 1,464 already in main, 102 unusable).
  - None of the 27 new shaders was ever compiled at 1440p: `CUSA00473_res1440` holds 16 of the 44, all from the old list, so there are no unscaled 1440p translations of them to worry about.
  - Main (1080p) holds 17 of the 44. At 1440p these now compile once more under their salted names, rescaled.

## In-game results (2026-11-12, 23:23–23:36)

Build: runtime `fix/shared-cache` `964bd44` (it adds `LBP3_SHADER_CACHE_DIR`, so the tests point the cache at copies), shadPS4 `b789a896` (44-shader list). The runs used the story test save (`savedata_story_20261111`) via `saves.sh`, on the 165 Hz display, with frame generation off (the capture hooks need it off). Each run: boot, a capture in the Pod, then the Prologue map into Needlepoint Peaks and a capture at the level start, 3 final frames each (`LBP3_FG_DUMP=3`). Every cache was a COPY in `analysis\shared_cache\scratch\` (removed afterwards). The user's `user\shader_cache` was untouched: 6,168 and 8,403 files before and after. The script is `D:\Recomps\lbp3-wt\_tools\sc_plan.sh`, and the scorers are `sc_keys.py` and `sc_score.py`; progress and stderr are in `analysis\shared_cache\runs\`.

| Run | Cache copy | Settings | Pipelines compiled | `[res]` scaled | Warnings |
|---|---|---|---|---|---|
| A1 | X = user's 1080p cache | 1080p | 0 | 0 | 0 |
| B1 | X, after A1 | 1440p, shared | 12 | 11 shaders | 0 |
| C1 | X, after B1 | 1080p | 0 | 0 | 0 |
| S1 | Y = user's 1440p folder | 1440p, `LBP3_RES_CACHE=separate` | 0 | 0 | 0 |
| S2 | Y, after S1 | the same (noise) | 0 | 0 | 0 |
| R1 | Z = user's 1080p cache | 1080p (noise and reference) | 0 | 0 | 0 |
| D1 | M = `merged44_CUSA00473` | 1440p, shared | 12 | 11 shaders | 0 (no "conflicts with index") |

"Warnings" counts GPU warnings, conflicts and stale-cache lines.

- **(a) PASS (measured).** At 1440p on the warm shared cache, B1 compiled 12 pipelines. `sc_keys.py` resolves every new key: all 12 use a rescaled shader under its salted key (`4d3d6de4`, `3a0615bd`, `c9311bbd`, `8079a39d`, `3d19e625`, `fdf2b5d5`, `8e1db4f9`, `2949baf9`, `0b2a3346`, `33c89263` and `5ed16706`, the composite), and 0 use anything else. Everything else on the route came from the 1080p entries.
- **(b) PASS (measured).** The 11 rescaled shaders each logged `[res] shader <orig>: N 1080p literals scaled for 1440p (cache key <salted>)`.
  - **Level start** (the composite `5ed16706` and SMAA draw here): B1 against S1 has a mean absolute difference of **0.000**, 0.000% of pixels off by more than 8, and an edge-pixel difference of 0.00. That's pixel-identical. The noise pair S2/S1 is also 0.000.
  - **Pod:** B1 against S1/S2 has a mad of 1.97/2.17, inside the Pod's run-to-run noise (S1/S2 1.36, A1/R1 2.18). The differences sit only in animated parts: the network-error dialog's pop-in, the trees outside the window, and Sackboy's idle (`analysis\shared_cache\pod_diff.png`).
- **(c) PASS (measured).** C1, 1080p after the 1440p session on the same cache, compiled nothing and logged no rescaling, so it used the original translations.
  - **Level start:** C1 against A1 (1080p before any salted entry existed) has a mad of **0.001**, as does A1 against R1.
  - **C1 against R1 directly is 1.36.** The 3 captured frames of each run hit different animation phases with no overlap. A1's frames include an exact match for each, so this is timing, not rendering.
  - **Pod:** C1/A1 0.85, inside the noise (A1/R1 2.18).
- **(d) PASS on this route (measured).** The merged copy (4,012 keys) loaded with no "conflicts with index" or stale-cache lines. D1 compiled only the same 12 salted pipelines as B1, which are expected: the merge can't carry over the rescaled ones. The level start against S1 is 0.000. The Pod is 2.7 against S1, with the dialog's pop-in phase different because the bigger cache's warm-up shifts boot timing (`pod_diff.png`, top row). A 1440p-only level and the costume browser weren't on this route, so they're not checked.
- **(e) NOT DONE.** No run, scripted or human, has ever compiled any of the 27 new shaders. Neither of the user's caches holds one, and no log in `analysis\` or `analysis\play_logs\` has a `NOT on the list` line, including the earlier 1440p sessions that would have printed one. So I couldn't find a spot that shows the x-ray silhouette or the outline highlight in the time box. Untested.
- **State:** no game running, no marker or kept copy. The human's saves are identical to `human_play_20261112_2318`, and the clone's exe md5 is unchanged (`458ececb`). The capture sets were trimmed to their final frames (`analysis\fg_dump\{A1,B1,C1,D1,R1,S1,S2}_{pod,np}`) and the scratch caches removed.

## Split, (e), and the full regression (2026-11-13, 00:00–00:40)

**Split (coordinator's decision: the shared cache is merge-worthy; the 27 new shaders are held until (e) is seen in game).**
- shadPS4 `fix/res1440-shaders-44` = `b789a896` (the 44-shader state), pushed.
- shadPS4 `fix/shared-cache` `553be3ba` puts the list in `lbp3_res.h` back to the original 17 (checked against `56120048`'s by script). The comment names the held 27.
- Runtime `fix/shared-cache` `9625c5a`: the pointer, and `rescachetest` expects 17, with an outline variant not listed and not salted. Both pushed.
- Rebuilt; all 10 offline programs pass with 0 failed: `rescachetest` 68/0, and `imetest` 403/0 this time, with no game running.

**(e) not reached (time box about 30 minutes).**
- **Game data:** in `orbisguids.map`, `proc_base_xray_special.gmat` is GUID `0x54c39` (SHA1 `48fd7a69…`, `chunk1_001.farc`) and `proc_base_outline_special.gmat` is GUID `0x54c30` (SHA1 `ef1c5781…`).
  - **The search:** every resource in every `.farc`, decompressed, for each SHA1, its GUID as a u32 and its GUID as a varint (`D:\Recomps\lbp3-wt\_tools\find_gmat_refs.py`, `analysis\shared_cache\gmat_refs.txt`).
  - **Placements:** no level or object resource places either material.
  - **Byte matches:** coincidental, in textures, a language file and two meshes. Apart from those, there's the all-GUIDs `thermo_match.therm` and one bundle in `intro_001.farc` (`87d34411…`, magic `01 AB 03 F9`, probably the compiled scripts).
- **The eboot:** the outline GUID appears once, in `cmp eax, 0x54c30` at `0xb3e6a`, inside `FUN_000b2330`, which classifies an object into a category code. The x-ray GUID doesn't appear.
- **So:** both effects are applied by script or engine logic on events I couldn't identify cheaply. No route reached them, and the 27 stay held.

**Merged copy rebuilt from the LIVE folders, 17 list, no game running:** `D:\Recomps\lbp3-recompiled\analysis\shared_cache\merged17_CUSA00473`.
- **Counts:** 10,209 files; 1,696 keys added, 1,464 already in main, 102 unusable. Programs: 16 listed skipped, 1,120 new, 1,225 shared; 1,368 permutations same and 10 appended.
- **No difference from before:** the live folders hadn't changed since the first run (6,168 and 8,403 files; newest keys 21:15 and 21:55). None of the 27 is in `CUSA00473_res1440`, so the 17-list and 44-list results are the same. `merged17` is byte-identical to the old `merged44`, which is removed.

**(4) 1440p-only content, the costume browser** (the DLC costumes, first browsed at 1440p). Each run: the story test save, Pod, popit, Customize Character, Costumes, scroll, next tab, with 3 captures.

| Run | Cache copy | Settings | Compiled | Boot: display / Pod |
|---|---|---|---|---|
| E1 | `merged17` | 1440p shared | 10, all salted | 4.33 / 5.29 s |
| E2 | the user's 1440p folder | separate | 0 | 3.68 / 4.64 s |
| E3 | the same, again | separate | 0 | 3.67 / 4.64 s |

- `sc_keys.py`: all 10 of E1's new keys use a rescaled shader under its salted key, and 0 other. The costume content itself came from the carried-over keys.
- **Pictures:** E1 against E2/E3 has a mad of 3.3–5.6 (edge 30–48). The noise pair E3/E2 is 3.6–5.4 (edge 33–40). That's within noise; the animated Sackboy preview dominates.

**Boot times (measured: from launch to the `display buffers` line, i.e. renderer up after the cache warm-up, and to the profile mount in the Pod; polled every 0.25 s):**

| | Display | Pod |
|---|---|---|
| 1440p, merged (4,012 keys) | 4.33 s | 5.29 s |
| 1440p, user's separate folder (3,262 keys) | 3.67 s | 4.64 s |
| 1080p, merged (4,012 keys), runs T1 and T3 | 4.77 / 4.46 s | 5.75 / 5.75 s |
| 1080p, user's main folder (2,325 keys), runs T2 and T4 | 3.14 / 2.70 s | 4.13 / 3.66 s |

So the boot to the Pod is **about +0.65 s at 1440p** (`play.bat`'s setting) and **about +1.6–2.1 s at 1080p**. WarmUp loads every key, and the merged folder has about 4,000. This is a once-per-launch cost in exchange for no first-visit compiles of content seen at either resolution.

**(5) The full regression on scratch copies of `merged17`** (the build swapped into the clone, md5 restored, `LBP3_SHADER_CACHE_DIR` set, and a guard after every step checking that nothing under `user\shader_cache` changed: it never fired):

| Check | Runtime defaults (1080p) | `play.bat`'s settings (1440p, frame generation) |
|---|---|---|
| Introduction steps ≠ 1 | 0.00% / 0.26% | 0.13% / 0.13% |
| Thruster recipe | 3 of 3 | 3 of 3 |
| Bunkum | 16/16, Needlepoint loads | 16/16, Needlepoint loads |
| Create sky | cyan | cyan |
| Photo | PASS (encoder, readback, 1 JPEG) | PASS |
| `t13_coop` | PASS | PASS |
| Pipelines added | 0 | 17, all salted (`sc_keys.py`) |

- **The heavy route at `play.bat`'s settings:** walk work 21.40 ms, 1 frame over 40 ms; standing 19.41 ms; 0 GPU warnings.
  - **`[framegen] gpu`:** in the level, every window fetched all its frames with HUD-less images and the game's vectors, except one window at the load boundary (6 fetched, 5 with). `port`'s run (`m5_vplay_np`) has the same one-frame gap at its load (25 fetched, 24 with). So finding the composite by role still works with it salted.
  - **`[fills]`:** identical to `port`'s run: 3.0–3.1 refreshes (44.8 MB), 63 cleared, 1 pre-sync and 7 skipped per frame.
  - **Compiles:** 1 on the whole route, a rescaled shader (`03101c0e`, salted).
- **The `play.bat`-equivalent boot:** the user's profile copy (`human_play_20261112_2318`) as test saves, `play.bat`'s env, a `merged17` copy.
  - 2560x1440, 256 DLC packs, the GPU warp on, `[framegen] gpu` 30/30/30, 0 pipelines compiled, no warnings.
  - It closed by WM_CLOSE ("window closed").
- **State:** no game running, no marker, kept copy or `.sc_bak` left. The clone's exe md5 is `458ececb`, and the human's saves are identical to `human_play_20261112_2318`.

**Ready to merge:** shadPS4 `fix/shared-cache` `553be3ba` (17 list) and runtime `fix/shared-cache` `9625c5a` (on `port` `2cd464a`, plus `964bd44`'s `LBP3_SHADER_CACHE_DIR`).

**Cache install:**
- **Rebuild first if needed:** the user may have played at 1440p since 00:00, which adds keys to `CUSA00473_res1440`. If so, rebuild with no game running: `python -I .scratch/lbp3-port/tools/res_cache_merge.py user/shader_cache/CUSA00473_res1440 user/shader_cache/CUSA00473 --list <lbp3_res.h> --xxh3tool analysis/shared_cache/xxh3tool.exe --out <new> --apply`.
- **Then:** rename `CUSA00473` to `CUSA00473_pre_shared` and the merged copy to `CUSA00473`. Keep `CUSA00473_res1440`.

## Merged and installed (2026-11-13, about 00:45, unattended)

- **`port` = runtime `9625c5a`, submodule `port` = `553be3ba`** (the shared cache, with the original 17 rescaled shaders), pushed. The 27 shaders from the full scan stay on shadPS4 `fix/res1440-shaders-44` (`b789a896`, pushed) until they're seen in game. All 10 offline test programs pass on the merged clone.
- **Cache installed by copy and rename:**
  - `user\shader_cache\CUSA00473` is now the merged copy (`analysis\shared_cache\merged17_CUSA00473`, 10,209 files, built from the unchanged live folders);
  - the previous `CUSA00473` is kept as `CUSA00473_pre_shared_20261113_0043` (6,168 files);
  - `CUSA00473_res1440` is untouched (8,403 files): it is the cache `LBP3_RES_CACHE=separate` uses.
- **One `play.bat` boot** with the installed cache on the 165 Hz display:
  - 2560x1440, 256 DLC packs;
  - 8 pipelines compiled, all under the rescaled shaders' salted keys (logged with their cache keys);
  - closed cleanly;
  - saves changed only by the game's own profile rewrite (`littlefart1601` → `1602`).
- **Rollback:**
  - `LBP3_RES_CACHE=separate`, and point the main folder back: rename `CUSA00473` aside and `CUSA00473_pre_shared_20261113_0043` back;
  - or check out the previous `port` (`2cd464a`, submodule `56120048`).
