# Build LBP3 Recompiled and run it on this dump

Type: task
Status: resolved
Claimed by: build session (2026-11-09)
Blocked by: none
Map: [LBP3 native port](../map.md)

## Question

Does unmodified upstream build on this PC and run this **game dump**? How far does it get, and how well? The gap list needs first-hand facts from this machine, not just upstream's notes.

## Checklist

1. **The human installs the Clang component.** Visual Studio 2026 Community 18.5 is installed with the C++ workload and CMake. It's missing clang-cl, which upstream needs because game-facing functions use the System V ABI. The human runs this in the prompt (the `!` prefix runs bash, not PowerShell); it asks for administrator rights:

   ```bash
   ! "/c/Program Files (x86)/Microsoft Visual Studio/Installer/setup.exe" modify --installPath "C:\Program Files\Microsoft Visual Studio\18\Community" --add Microsoft.VisualStudio.Component.VC.Llvm.Clang --add Microsoft.VisualStudio.Component.VC.Llvm.ClangToolset --passive --norestart
   ```

   The agent checks the result with `vswhere -requires Microsoft.VisualStudio.Component.VC.Llvm.Clang`.
2. **Clone upstream, untouched, with its submodule.** Clone `https://github.com/PurpleGitty/LittleBigPlanet-3-Recompiled` recursively into `D:\Recomps\lbp3-recompiled\`, which pulls in PurpleGitty/shadPS4 `lbp3`. Record both commit hashes.
3. **Get SDL3.** Download `SDL3-devel-3.4.16-VC.zip` from SDL's GitHub releases into its own new, empty folder, then extract it so that `third_party/SDL3/cmake` exists.
4. **Build with `build.bat`.** Record the warnings that matter and the build time.
5. **Point it at the dump.** The dump is `D:\Recomps\ROMs\PS4\CUSA00473`, merged (base and patch in one folder), and must stay read-only. Use `game_dir.txt` or `LBP3_GAME_DIR`. If upstream insists on separate base and patch folders, try configuration first. If the code has to change, make the smallest change on a throwaway local branch and record it as a **gap**. Stop and record it if the port wants to write inside the dump folder.
6. **Run and drive it.** Use `play.bat` / `run.ps1`, with upstream's remote input (`tools/remote_start.ps1`, `analysis\input_cmd.txt`) to go from boot to the Pod, the Introduction level, and into Create mode. Upstream's notes list the button path. For each step, record whether it was reached, the FPS, any hitches, and the relevant log lines and screenshots. Keep logs and screenshots out of git; the logs may contain game data.
7. **The human plays for a short while.** The agent can't hear audio or judge feel. Give the human five minutes with a controller, Pod → Introduction → Create (place a Thruster). Record their impressions of sound, controls and smoothness in their own words.

## Done when

An `## Answer` records: the commits built, the toolchain versions, any change needed to run the merged dump, the furthest point reached, the measurements from step 6, the human's impressions, and every new problem seen (each one a gap for [Which gaps stand between LBP3 Recompiled and the offline finish line?](03-gap-list.md)).

## Answer

Built and run on 2026-11-09 in `D:\Recomps\lbp3-recompiled\`: upstream `dfc492a` and shadPS4 fork `4640726e`, plus one local change on the throwaway branch `local/self-unwrap` (`ba83dcb`). The comments below hold the step-by-step detail. Logs and screenshots are in `analysis\`, which git ignores.

**Toolchain.** Visual Studio Community 18.5 with clang-cl **20.1.8**; upstream's 22.1.3 isn't needed. The build uses Visual Studio's bundled CMake 4.2.3 and Ninja 1.12.1, and SDL3 3.4.16. `build.bat` exits 0 in 60 s (295 steps) with 102 harmless warnings. Start it from the repo folder with `cmd /d /c`.

**Running the merged dump.** Setting `game_dir.txt` to `D:\Recomps\ROMs\PS4` is enough for the merged folder. One code change was needed: every module in the dump is a fake-signed SELF, and upstream loads only plain ELF. `UnwrapSelf` in `runtime/src/core/module.cpp` (about 40 lines) rebuilds the ELF in memory. With it, all 8 modules load and the eboot lands at `0x400000`. The port doesn't write into the dump: `/app0` is read-only, and no file in the dump was written or created after the runs began.

**Furthest point reached.** Scripted: boot, then the Pod (about 20 s), the Introduction (walking), back to the map, then Create → My Moon → a new level → the Popit → the Tools Bag. The human played the Pod, the Introduction and Create, and placed a Thruster and switched it on. It ran at first, then rendering froze with the Thruster firing (gap 8). Nothing crashed.

**Measurements.**

| Where | Frame time (30 fps cap = 33.3 ms) | Spikes |
|---|---|---|
| Boot and Pod (45 s smoke run) | about 35 ms | 253 ms at boot |
| Introduction (scripted) | 24–26 ms | 225–305 ms entering the level; first loads stall 2.5–6.3 s |
| Create (scripted) | about 20 ms | none |
| Human's session | mostly 14–15 ms, otherwise about 24.5 ms | 12 of 518 intervals had a frame over 40 ms: 189–311 ms, and one 3.0 s load |

Audio port 3 (type 2, mono float) underran more than 2,400 times in the 20-minute scripted session and 851 times in the human's. Linking stubs 171 functions and 9 variables.

**The human's impressions** (their words): "sound is good, control respond properly. no sutters, game seem too fast yeah. thruster is in the game and it's not freezing it by any means." A few minutes later: "wait hold on i think the game freezing because of thurster rn".

**New gaps, for [Which gaps stand between LBP3 Recompiled and the offline finish line?](03-gap-list.md):**

1. **The dump's modules are SELFs, and upstream loads only ELF.** Fixed on `local/self-unwrap`; the change has to land in the fork.
2. **The game runs too fast.** The 30 fps cap doesn't hold on this PC: frames take 14–26 ms instead of 33.3 ms, and the human confirms the speed-up. The vsync path in `presenter.cpp` (`WaitForFlipSlot`, 8 presents per flip at 239 Hz) counts presents, which complete faster than the refresh rate. Suspects are variable refresh and the extra virtual displays (Meta, SudoMaker). This may also account for the camera snapping in upstream's notes.
3. **Hitches.** Loads stall 2.5–6.3 s, and play spikes to 189–420 ms when new images appear. The human didn't notice them.
4. **Audio port 3 underruns constantly.** The human didn't hear anything wrong.
5. **The Create sky is yellow/beige.** Confirmed.
6. **Popit panels may open off-screen.** In the scripted run, the Tools Bag and Gadgets panels opened mostly past the right edge. The human didn't confirm it, and placed a Thruster through the Popit without trouble.
7. **`tools/gamedir.py` `eboot()` assumes a `CUSA00473-patch` folder.** Only analysis tools use it.
8. **The Thruster freeze still happens.** In the human's Create session, a Thruster was placed and switched on and ran fine at first. Then rendering stopped at flip 17,220 with the Thruster firing (`analysis\screenshots\freeze1.png`). Audio, the network loopback and the window kept running. The F10 report (`analysis\perf\perf01.txt` and the `[perf]` dump in `play_log.txt`) shows every game thread waiting, with the main thread in `sceKernelWaitEqueue`. The GPU command thread is stuck in `gpu::ReadbackDisplayBuffer` → `Vulkan::Scheduler::Finish` → `Semaphore::Wait`, busy-waiting in the NVIDIA driver. Meanwhile the GPU runs at 45–48% and 180 W, so a submission is running and never finishes. That points at a runaway shader, likely the Thruster's flame and particles, rather than a lost signal. The process uses about 2.3 cores. Upstream's Oct 14 note calling it fixed (`NOTES.md:481`) doesn't hold on this PC.

**Not covered by this run:** the Legend of Bunkum retest with `LBP3_CUTSCENE_AUTOSKIP=off`, Needlepoint Peaks and everything after it, save and reload, text entry, and stickers and photos.

## Comments

- 2026-11-09 (charting session): **Step 1 done.** VS Clang components installed. `clang-cl` 20.1.8 is at `C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin\clang-cl.exe`, and `vswhere` confirms both components.
  - Upstream's NOTES.md says it was built with clang-cl **22.1.3**. If the build fails on 20.1.8, try a standalone LLVM 22 first, then record which version it needs.
  - The `!` bash command in step 1 fails unelevated: `--passive` needs an elevated start (exit 5007). What worked was PowerShell `Start-Process ... -Verb RunAs -Wait` with the same arguments; the human accepts the UAC prompt.
- 2026-11-09, from [Where does LBP3 Recompiled stand against the offline finish line?](01-base-status.md):
  - **Upstream won't load this dump unmodified.** `runtime/src/core/module.cpp` `Module::Load` throws `"<file> is not a decrypted ELF"` unless the file starts with `\x7FELF`. Every module in this dump (eboot, `sce_module/*.prx`, `gamedata_orbis/spu/*.prx`) is a fake-signed SELF (`4F 15 3D 1D`). Step 5 therefore needs a change. Prefer teaching the loader to unwrap SELF, porting gravityrush_pc's SELF-to-ELF code, on a throwaway branch, recorded as a gap. The fork needs that anyway, since dumpers write SELFs. The fallback is unwrapping to ELF files outside both the dump and the repo.
  - **The merged layout seems fine:** `LBP3_GAME_DIR=D:\Recomps\ROMs\PS4` (the folder that *contains* `CUSA00473`). The eboot is the 1.28 build upstream targets; all 7 patch sites match.
  - The intro movie is Bink, not AvPlayer, and is skipped by default. The Legend of Bunkum is auto-skipped (`LBP3_CUTSCENE_AUTOSKIP`).
- 2026-11-09 (build session): **Steps 2 and 3 done, and step 5's loader change is ready.**
  - Cloned to `D:\Recomps\lbp3-recompiled\`: upstream `dfc492a`, shadPS4 fork `4640726e` (detached, on `lbp3`).
  - SDL3 3.4.16: `SDL3-devel-3.4.16-VC.zip` (sha256 `1a784cb2…93e7de`) extracted to `third_party/SDL3`.
  - `game_dir.txt` holds `D:\Recomps\ROMs\PS4`. The runtime mounts `/app0` read-only (writes return `EROFS`, `kernel_fs.cpp:175-176`), so it can't write into the dump.
  - The SELF unwrap is on the throwaway branch `local/self-unwrap` (`ba83dcb`, about 40 lines in `module.cpp`). It rebuilds the ELF image in memory from the SELF's blocked segments. A read-only check of all 8 modules in the dump (eboot, `libc.prx`, `libSceFios2.prx`, `spu.prx` and 4 FMOD plugins) found plain, uncompressed segments that cover every program header except the version note (`0x6fffff01`), which the loader never reads.
  - Upstream's CMake builds only the submodule sources; nothing is downloaded at configure time.
  - **Step 4 is waiting on the human.** Claude Code's auto mode blocked running `build.bat` because it runs code cloned from GitHub. Building and running need the human's go-ahead.
  - Checked before building: VS's bundled CMake is 4.2.3 (upstream needs ≥ 3.25), its Ninja is 1.12.1, and `vcvars64.bat` and `clang-cl.exe` are where `build.bat` expects them.
  - **Two things for the run:** remote input drives player 1 only when no SDL controller is connected (`gamepad_sdl.cpp`, `Read`: `if (!p)`), so the controller has to stay off during scripted runs. `tools/nav.py` needs reference screenshots in `tools/nav_refs/`, which each user takes themselves; there are none yet, so the first runs are driven by hand from screenshots.
  - **Small gap:** `tools/gamedir.py` `eboot()` returns `CUSA00473-patch\eboot.bin`, which doesn't exist in the merged dump. That breaks `tools/boot_steps.py` and `tools/farc.py`, though only analysis tools use it.
- 2026-11-09 (build session): **Steps 4–6 done.** Logs and screenshots are in `D:\Recomps\lbp3-recompiled\analysis\`, which git ignores.
  - **Build (step 4):** `build.bat` exits 0 in 60 s (295 steps) with the installed clang-cl **20.1.8**, so LLVM 22 isn't needed. There are 102 warnings, none that matter: 76 unused zlib functions, 19 `WIN32_LEAN_AND_MEAN` redefinitions, and 7 `u8path` deprecations. `build.bat` must be started from the repo folder with `cmd /d /c` (as `run.ps1` does), because it uses relative paths.
  - **Load (step 5):** with the SELF unwrap, all 8 modules load straight from the dump, and the eboot lands at `0x400000`. Linking reports 36,910 relocations, 708 imports from game modules, 411 from HLE, and 171 functions plus 9 variables stubbed (172 unique). The runs wrote nothing into the dump: no file in it was written or created after the runs began.
  - **Run (step 6, remote input, fresh save):** the Pod renders at about 20 s. Cross opens the Pod's options menu, where "Play Online!" is crossed out. Home shows Play, Create and All About Me; Community and Store are crossed out. Play → Adventure → Prologue (0 of 5) → Introduction loads in about 15 s, and walking works. The pause menu's Return to Map works. Create → My Moon → Create! shows the Advanced Create prompt, then the create floor, then the Popit. The Pod, Introduction and Create were all reached; nothing crashed.
  - **Saves** land in `user\savedata\CUSA00473\` (`LBPxSAVE`, `LBPxSAVEx12x349`, `LBPxSAVExLITTLE`).
  - **New gap candidates:**
    1. **The 30 fps cap doesn't hold on this PC.** Frames come every 24–26 ms in the Introduction and 20 ms in Create, not 33.3 ms. The display is 2560×1440 at 239 Hz (max 360) on the RTX 3090, with Meta and SudoMaker virtual displays also attached. The vsync path (`presenter.cpp` `WaitForFlipSlot`, 8 presents per flip) counts presents faster than the refresh rate. Variable refresh or the extra displays are suspects. This may also feed the camera snapping.
    2. **Hitches:** first loads stall 2.5–6.3 s (109–167 new images), and in-level frames reach 225–420 ms when new images appear.
    3. **Audio port 3** (type 2, mono float) underruns constantly: more than 2,400 times in about 20 minutes.
    4. **Popit bag panels open mostly off the right edge** (Tools Bag, Gadgets), so the items can only be identified from their name labels. It's unknown whether a PS4 shows them like this.
    5. **The Create sky is yellow/beige,** confirmed.
  - **The Thruster isn't among the reachable Movers** (Mover, Advanced Mover, Follower, In/Out Mover, Force Chip), so that recheck is left to the human's play test.
  - Remote presses of 250 ms or longer register twice in menus; use about 150 ms.
