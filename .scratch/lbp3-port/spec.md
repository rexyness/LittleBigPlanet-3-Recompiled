# LBP3 native port: port spec

> **Final (2026-11-10).** Assembled from the resolved tickets. This file decides nothing new: each section points to the ticket that decided it. The ladder and the fork layout are in [04](issues/04-milestone-ladder.md#answer).

## Scope

- **Goal: the offline finish line.** Story mode start to finish, plus Create mode and the Pod, with audio, saves and a controller, run from a dev build, plus local co-op for up to four players (added 2026-11-11, [13](issues/13-local-co-op.md)) ([map](map.md), [GLOSSARY](../../GLOSSARY.md)).
- **What runs:** the US release `UP9000-CUSA00473` at version 01.28, base and patch merged into one folder (the **game dump**), on Windows. The dump is read-only and never copied into a repo.
- **Out of scope** ([map](map.md#out-of-scope)):
  - online play and community servers;
  - other releases and patch levels;
  - Linux and the Steam Deck;
  - PS Move, the PlayStation Camera, voice chat and trophies;
  - DLC and level packs;
  - broadcasting, the companion app and content search;
  - a launcher, release packaging, and enhancements (60 fps, higher resolution, new mouse and keyboard work);
  - static recompilation, decompilation, and targeting the shadPS4 emulator.

## Base and how the fork tracks upstream

- **Base:** LBP3 Recompiled ([ADR 0001](../../docs/adr/0001-native-port-on-a-forked-base.md)), cloned to `D:\Recomps\lbp3-recompiled`. Its shadPS4 fork is the `third_party/shadPS4` submodule.
- **Remotes** ([04](issues/04-milestone-ladder.md#answer)): in the clone, `origin` is upstream (PurpleGitty) and `fork` is the human's public fork `rexyness/LittleBigPlanet-3-Recompiled`. The submodule has the same pair, with `fork` at `rexyness/shadPS4`. Both forks were published on 2026-11-10 at the human's request, with every work branch, and `port` (milestone 1) was pushed to both the same evening with the human's OK.
- **Fork layout:**
  - Each fix is first a branch (`local/…` or `fix/…`) and a patch in [patches/](patches/), then merged into the integration branch `port`. The submodule has a matching `port` branch.
  - On `port`, `.gitmodules` points at `rexyness/shadPS4`, so a fresh recursive clone of the fork can fetch the submodule commits the fixes pin.
  - Upstream is fetched at each milestone boundary and merged into `port`.
- **Outward-facing steps** are confirmed with the human first: pushing new branches such as `port`, and pull requests upstream. For pull requests the human chose "not yet".
- **Planning:** this repo (`lbp3-pc`, published as the private `rexyness/lbp3-pc`) holds the map, the tickets, the patches and the test tools.

## Toolchain and running the dump

- **Toolchain** (map decisions; [02](issues/02-baseline-run.md)): Visual Studio 2026 with clang-cl 20.1.8, CMake/Ninja and SDL3 3.4.16, as the base uses.
  - Build with `cmd /d /c .\build.bat` from the clone. The agent's shell needs `cmd /d /c` because it sets `NoDefaultCurrentDirectoryInExePath`.
  - Stop the game before building; the linker can't overwrite a running `lbp3.exe`.
- **Running:** `build\lbp3.exe --run [--skip-intro]`, with the dump found through `LBP3_GAME_DIR`/`game_dir.txt`.
  - The dump's modules are fake-signed SELF files, which the loader unwraps in memory (`ba83dcb`).
  - Saves land in `user\savedata\CUSA00473`.
- **Test tooling** (the clone's `tools\` and `analysis\`, plus [tools/](tools/) here):
  - remote input (`LBP3_REMOTE_INPUT`, `analysis\input_cmd.txt`), `capture.ps1` (DPI-aware since `1199cf2`), `camera_check.py`, F9 (frame trace) and F10 (thread dump and samples);
  - on `diag/gpu-checkpoints`: GPU checkpoints with a device-lost report, the sync-event ring (`LBP3_TRACE_SYNC`), the poll watcher (`LBP3_POLLWATCH`), the watch ring (`LBP3_HWWATCH_RING`), `LBP3_SKIP_SHADERS` and `LBP3_DISPLAY`. The sim-step trace (`LBP3_TRACE_SIM`) and `LBP3_PACING` are on `local/frame-pacing`, so `port` has them;
  - here: `hitl_start.sh` (play-session presets `isolate`, `control` and `all`; diagnostic build only) with `watch_freeze.sh`, `t10_ab.sh` (the Thruster save recipe), `t09_ab.sh` and `pace_run_intro.ps1` with `sim_steps_level.py` (the camera measure), `t08_bunkum.sh`, and `soak.sh` (it presses R1 once a minute, because after 5 minutes without a button press the game swaps the scene for its attract video). The scripts start from the baseline save `analysis\savedata_thruster_on` and put the human's current saves back on exit (`saves.sh`).
- **This PC:** RTX 3090 with an NVIDIA global profile that forces vsync off. The port has to cope with that setting, so leave it alone ([06](issues/06-frame-pacing.md)). Displays: the human's primary at 240 Hz and a secondary at 165 Hz (2560×1440). Since 2026-11-10 the human wants tests and agent-run sessions on the 165 Hz secondary (`LBP3_DISPLAY=165hz`, set by the clone's test launchers from `analysis	est_display.txt`), so new measurements compare with the 165 Hz baselines. Windows runs at 125% scaling, so screen captures must be DPI-aware.

## Gaps

From [03](issues/03-gap-list.md), updated by [06](issues/06-frame-pacing.md), [07](issues/07-thruster-freeze.md), [08](issues/08-ps4-reference.md), [09](issues/09-sim-step-bursts.md), [10](issues/10-compute-wait-deadlock.md) and [04](issues/04-milestone-ladder.md).

| # | Gap | State | Fix or next step |
|---|---|---|---|
| 1 | Fake-signed SELF modules | fixed on a branch | `ba83dcb` (in `local/frame-pacing`); milestone 1 |
| 2 | `gamedir.py` assumes a patch folder | fixed on a branch (`boot_steps.py` names all 77 boot steps; `farc.py` lists 93,353 files) | `fix/gamedir`, [patch](patches/gap2-gamedir.patch); milestone 1 |
| 3 | Game too fast: presents don't block on this PC | fixed on a branch; the human confirmed normal speed (06) | `local/frame-pacing`; milestone 1 |
| 4 | Camera snaps: 0/2 sim steps | fixed on a branch: in the real Introduction steps ≠ 1 fell from 12.10/9.76% to 0.31/0.12% at 165 Hz and from 10.53/12.55% to 0.30/0.44% at 240 Hz; the human's walk was smooth between transitions (09). `sim_steps_level.py` now starts counting after the story save on entering the level, which had pushed some runs over 1% when it came late (12, 2026-11-11) | `fix/epoll-precise`, [patch](patches/09-epoll-precise.patch); milestone 1 |
| 5 | Thruster freezes: the save deadlock and the GPU reset | fixed on a branch: the save recipe passed 14 of 14 at 165 Hz and 6 of 6 at 240 Hz (both pacings). In the human's sessions at 240 Hz the old wait reset the GPU the moment water started firing, and the fix ran clean at both pacings, including 3 Thrusters with water and a save (07, 10) | `fix/gfx-submit-wait`, [patch](patches/10-gfx-submit-wait.patch); milestone 1 |
| 6 | Create sky has red and blue swapped | fixed on a branch (the sky is cyan; floor and walls unchanged) | `fix/clear-swizzle` (shadPS4), [patch](patches/gap6-clear-swizzle.patch); milestone 1 |
| 7 | Popit panels open off-screen | **not a bug**: the screenshots were cropped by a DPI-unaware capture (08 Comments); the page sits at 72.7% as on a PS4 | [capture patch](patches/tools-capture-dpi.patch) for the test tools; milestone 1 |
| 8 | The Legend of Bunkum ends on a white screen; auto-skip hides it | fixed on a branch (A/B 2 of 2 each way; the clean branch also plays through) | `fix/guest-fpu`, [patch](patches/gap8-guest-fpu.patch), then auto-skip off by default; milestone 1 |
| 9 | Sackboy launched out of Needlepoint Peaks | not seen with 3 and 8 fixed: 3 spawns, and one 70 s walk with jumps (04 Comments) | the Prologue playthrough, milestone 3 |
| 10 | Hitches and load stalls | narrowed (04, 09, 10): the 7 s boot stall is the game's own pauses (a PS4 with this dump does the same). Entering a level stalls single frames (170–350 ms, rarely 0.75–1 s). In the Introduction the game makes no new frames for 1–2.5 s at transitions (Newton's entrance and its story save, tutorial videos), in the same 5 s windows as 14–50 MB of reads ending in the sound banks. The dump is on an NVMe drive, so the hardware isn't the limit; whether the runtime's read path or the game's own work holds the frames is open. The human felt these. A growing Create scene at 240 Hz had 86 frames over 40 ms in 23 minutes (worst 359 ms), at asset loads or with frame work near the 33.3 ms budget. **Milestone 5 (15), 2026-11-11/12:** heavy story levels ran over budget (Needlepoint Peaks work 33.9 ms) because recording and the GPU ran in series; **fixed on `port` `2d92d65`**: the GPU starts on each finished command buffer (walk work 27.4 → 22.5 ms) and the pre-submit wait is graphics-only (the once-per-session 20 ms burst gone; walk frames over 40 ms 9 → 1). Level-entry freezes of 0.5–1.4 s are the driver's first-sight pipeline builds; second visits are clean | the rest: async pipeline creation (the human's call), the graphics queue's blocked waits (~4 ms in half the heavy frames, ticket 10's risk class), the Introduction's transition stalls |
| 11 | Voice port 3 underruns | not a gap | rate-limit the log; milestone 1 |
| 12 | Text entry (`libSceIme`, `ImeDialog`) stubbed | **done on `port`** (12): a PS4-style on-screen keyboard drawn with ImGui over the game, played with the pad (the PS4's buttons), the PC keyboard or the mouse, with the game's title, prefilled text and max length; the level name and Text Chat work and the name is kept after a relaunch. The native window remains for the test programs and `LBP3_IME=window` | human session (typing a name with the pad); milestone 2 |
| 13 | JPEG (`libSceJpegDec/Enc`) stubbed | **done on `port`** (12): both libraries with stb, the compression ratio mapped to the firmware's quantisers. Only the encoder is reachable offline (Popit photos). Photos were black until the capture targets were written back for the CPU (shadPS4 `50b4720a`, only while the game has a capture queued) The human took a photo and placed it as a sticker on a wall with a DualSense, by Cross and by a touch-pad tap (12) | none |
| 14 | System dialogs stubbed | **done on `port`** (12): message, save-data, common and game-custom-data dialogs, drawn with ImGui over the game in the on-screen keyboard's style since 2026-11-11 (native windows remain for the test programs and `LBP3_DIALOGS=window`); the offline game reaches the save-data dialog through level and profile backup/import; a new backup, overwrite Yes and No, import and import Cancel were all checked in game. Left: the error dialog's validation, the store dialog (online only) | none |
| 15 | Geometry and tessellation stages are NOPs | **fixed on `port` `d84eae8`** (15, 2026-11-12): LBP3's ES+GS path (adjacency line lists, likely character fabric fins) was dropped whole, ~13-60 draws a frame; writing the stage registers made the GPU fault because shadPS4 declared 64 output vertices (over the 1,024-component limit). Now the real count (4) is declared, a guard drops and logs any over-limit GS, and the stages are on by default (`LBP3_GS_STAGES=0` off): 0 dropped draws, no GPU fault, every check passes | the human looks at characters in motion |
| 16 | Story after Needlepoint Peaks untested | check | human playthroughs per world with save snapshots; milestones 3–4 |
| 17 | Save, quit and reload untested | **done** (12): a saved edit comes back after a kill and relaunch and an unsaved one doesn't; story progress (Needlepoint Peaks unlocked after the Legend of Bunkum) survives a relaunch. Rerun with real capture data in the saves | none |
| 18 | Intro movie untested | **done** (04): the logos and the 90.7 s movie play at real-time speed with steady frames and hand over to the Pod; the human listened at 240 Hz: "intro plays perfectly" | none |
| 19 | Touchpad and motion | touchpad optional (08), sticker placement included: its hint shows a touch-pad tap, but Cross places a sticker too (12, the human with a DualSense). DualShock 4 and DualSense touch points reach the game, other pads only give the touch-pad button; motion unknown | watched during the story |
| 20 | Local co-op: a second controller does nothing | **done on `port`** (13): each controller signs in as a player (2–4) and unplugging signs out; players join the Pod and the story by themselves, and each guest gets its own profile in `user\savedata\user<N>`. Tested with virtual pads (2 and 4 players) and `tools/t13_coop.sh` | the human with a second person (Parsec); milestone 3 |
| new | Black band about 9 rows tall at the top of every frame | **probably expected**: the top-only look was the capture crop; the game draws a symmetric 14-row border at 1080, and one PS4 source fits that border plus a 0.97 safe area to the row (08 Comments) | none |

## Milestone ladder

From [04](issues/04-milestone-ladder.md#answer), which has the full table with checks and estimates:

| # | Milestone | Closes | Estimate |
|---|---|---|---|
| 1 | **Foundation**: a `port` branch that boots this dump, plays at the right speed, and survives the Thruster | gaps 1–6, 8 and 11, and the screenshot tools | **done 2026-11-10** ([11](issues/11-milestone-1-foundation.md)) |
| 2 | **Create mode works like a PS4** | 12, 13, 14, 17 | **done 2026-11-11** ([12](issues/12-milestone-2-create-mode.md)) |
| 3 | **Prologue and first world** | 9, 16 begins, 19 watched, 20 ([13](issues/13-local-co-op.md)) | 1 session + HITL |
| 4 | **Every story world, every character** | 16, 19, 15 if hit | 1 HITL session per world + fixes |
| 5 | **Performance** | 10, and headroom in the heaviest levels | 2–3 sessions |
| 6 | **Intro movie and audio** | done early (gap 18) | — |
| 7 | **Offline finish line** | everything above | HITL |

**Milestone 1, Foundation**, ready for a build session. Its inputs are branches, published on the fork, and patches in [patches/](patches/). A dry run on the throwaway branch `try/all-fixes` merged them all and passed checks 1–4 (04 Comments).
1. Create `port` from `local/frame-pacing`, which contains the SELF unwrap (`ba83dcb`). In the submodule, create `port` from the commit `local/frame-pacing` pins.
2. Merge the submodule first: `fix/gfx-submit-wait` (`40d6adc5`) and `fix/clear-swizzle` (`876508ce`) into the submodule's `port`.
3. Merge `fix/gfx-submit-wait`, `fix/epoll-precise`, `fix/clear-swizzle`, `fix/gamedir` and `fix/guest-fpu` into `port`, resolve the submodule pointer to the submodule's `port`, and apply [tools-capture-dpi.patch](patches/tools-capture-dpi.patch).
4. Point `.gitmodules` at `https://github.com/rexyness/shadPS4.git`, default the cutscene auto-skip to off, and rate-limit the voice port's underrun log (gap 11).
5. Build, then run the checks:
   1. boot → Pod → Introduction at 33.3 ms, sim steps ≠ 1 under 1% over the level (`tools/pace_run_intro.ps1`, then `tools/sim_steps_level.py`, which also fails if `00_craftworld` never loaded);
   2. the Thruster save recipe passes 3 of 3 (`tools/t10_ab.sh`);
   3. the Legend of Bunkum plays to its end with `LBP3_CUTSCENE_AUTOSKIP=off` (`tools/t08_bunkum.sh`);
   4. the Create sky is cyan;
   5. HITL: 10 minutes in Create with a Thruster and a water emitter, at present pacing and at vblank pacing, plus the Bink intro, the Newton cutscene and rockets. `hitl_start.sh` runs only on the diagnostic build, so on `port` launch with the clone's `tools\remote_start.ps1` (add `LBP3_PACING=present` for present pacing) and arm `tools/watch_freeze.sh <tag>`. Without the diagnostic build's device-lost report, a reset shows as a crash or a flip stall, and the System log (`nvlddmkm` 153) confirms it.

## Known risks

- **GPU hangs in later levels.** Geometry or tessellation stages, or other async-compute patterns. Mitigation: the checkpoint build names the hung work, the rejected-sampler warning came before every reset so far, and `LBP3_SKIP_SHADERS` tests a suspect.
- **Timing races between the runtime and guest code.** Both Thruster freezes came from the runtime letting the game run ahead of the GPU. Other places where the runtime's synchronous model differs from the console may hide similar races, including the fix's own residual window (a submit while the graphics queue is blocked on an earlier buffer), which read 0 wherever it was counted. Mitigation: the sync-event ring, the poll watcher and that counter.
- **Untested story content.** Nobody has played past Needlepoint Peaks, and the other characters are unseen.
- **Performance headroom.** Frame work is about 24–28 ms of the 33.3 ms budget in the Introduction and the Thruster level, and reached 25–48 ms as the human's Create scene grew. The transition stalls are unexplained. The heaviest levels are unmeasured.
- **One PC.** Everything was measured on one RTX 3090 with forced vsync off, on its 240 Hz and 165 Hz displays.
