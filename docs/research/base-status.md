# Where LBP3 Recompiled stands against the offline finish line

Research for [issue 01](../../.scratch/lbp3-port/issues/01-base-status.md). Reading only: nothing was built or run.

**Sources.** Upstream is [PurpleGitty/LittleBigPlanet-3-Recompiled](https://github.com/PurpleGitty/LittleBigPlanet-3-Recompiled) at `dfc492a` (2026-10-28; the only other commit is `8663fcd`, the same day). Its submodule is pinned to [PurpleGitty/shadPS4](https://github.com/PurpleGitty/shadPS4/tree/lbp3) `4640726`, one commit on top of shadPS4 `0c9ca52` (2026-10-09) that touches 7 files, all in `src/video_core` ([compare](https://github.com/PurpleGitty/shadPS4/compare/0c9ca52...lbp3)). `NOTES.md:N` and code paths below are upstream files at `dfc492a`; prefix them with `https://github.com/PurpleGitty/LittleBigPlanet-3-Recompiled/blob/dfc492a098e1be9113e11f2aeaffa1f0b7b8f3b3/` to open them. The game dump was only inspected, never copied: its SELF headers, `sce_sys/param.sfo`, and the import tables of its 8 modules, read with a throwaway script of my own (upstream's tools were not run).

## Headlines

- **Upstream cannot load this dump as it stands.** Every module in the dump is a fake-signed SELF, and upstream's loader only accepts a plain ELF. The merged base+patch folder itself is fine. See [Dump layout](#dump-layout).
- **How far the record goes:** boot, the Pod and its menus, the Introduction, The Legend of Bunkum (only through an automatic skip), Needlepoint Peaks, and Create mode. Nothing past Needlepoint Peaks appears anywhere in the record.
- **The Legend of Bunkum hang probably has a known fix.** btsouth/lbp3-pc hit the same white screen and fixed it by starting guest threads with the PS4's floating-point state. Upstream gives that state to fibers only. See [Port family fixes](#port-family-fixes).
- **The Thruster freeze is recorded as fixed** (Oct 14), after the Oct 11 list that calls it open. That list was never updated.
- **191 imports still fall to the generic stub**, not 590. The ones the finish line touches are text entry (IME), JPEG, and the save, message and common dialogs.
- **The intro is Bink, not `libSceAvPlayer`.** All 45 videos in the dump are `.bik`, which the game decodes itself. AvPlayer is fully stubbed and nothing in the record uses it.

## Status table

| Part | Status | Evidence |
|---|---|---|
| **Boot** to the Pod | works | "Boot measured 14 s to the Pod (user saw ~30 s)" (`NOTES.md:524`). Some boots stall 30-40 s in game code (`NOTES.md:368-370`). |
| Intro video | works, skipped by default | The live-action intro "plays with subtitles" (`NOTES.md:235-236`); tearing fixed by synchronous GPU submits (`NOTES.md:254-256`). It's Bink, decoded on the CPU by the game's job system (`NOTES.md:254`); all 45 videos in `gamedata_orbis/videos` and `gamedata/videos` are `.bik`. `play.bat:7` sets `SKIP_INTRO=1`, which serves the logos and intro as `empty_video.bik` (`runtime/src/core/vfs.cpp:55-66`). |
| Network dialog on the way in | works | Default: signed out and NetCtl disconnected (`NOTES.md:88-91`, `:239-242`). The Pod runs as a local host+client session over loopback UDP (`NOTES.md:267-269`). The game "shows its own 'problem with your network connection' dialog, then continues offline" (`NOTES.md:413-415`). Every eboot byte patch is now opt-in by environment variable (`runtime/src/core/patches.cpp:26-74`, `:81`). The dialog eats the first input after loading (`NOTES.md:673-674`). |
| **The Pod** and its menus | works | "the Pod, menus and planet map run smoothly" (`NOTES.md:457`); fixes that got there: `NOTES.md:406-431`. Menus, My Moon and Create are reached by scripted runs (`NOTES.md:654-658`, `:675-676`). Keyboard gap: "in reach of the computer the game won't open the Popit" (`NOTES.md:361-363`). |
| **Story:** Introduction | works | "Now a locked 33.3 ms while walking" (`NOTES.md:463-470`). The Newton lamps freeze fix "Needs the user to confirm" (`NOTES.md:471-476`). |
| Story: The Legend of Bunkum (cutscene) | known broken, auto-skipped | "ends on the white checkpoint at ~106 s"; "Real cause still open" (`NOTES.md:568-596`). A watchdog calls the pause menu's skip after the last cutscene camera has been idle 5 s (`runtime/src/input/game_state.cpp:86-122`). It is on by default and fires in *every* cutscene level (`LBP3_CUTSCENE_AUTOSKIP=off` disables it). |
| Story: Needlepoint Peaks | works, with a known problem | The user played to its end. There "Sackboy got launched far out of the level onto the create-mode floor ... Recheck after the fix" (`NOTES.md:607-610`). Slowdown popping prize bubbles: fix made, "not reproduced by script" (`NOTES.md:597-606`). |
| Story: everything after Needlepoint Peaks | untested | No note reaches it. Per [Wikipedia](https://en.wikipedia.org/wiki/LittleBigPlanet_3) that's the rest of the opening (Nana Pud's Stitchem Manor), Manglewood (Oddsock), the Ziggurat (Toggle), Bunkum Lagoon (Swoop) and the finale in Newton's fortress, plus the Popit Puzzles tutorial world. Oddsock, Toggle and Swoop have never been *played*. The "Bunkum Heroes intro ... after Swoop's introduction" (`NOTES.md:495-512`) is the Legend of Bunkum cutscene. |
| **Create mode:** entering and basic editing | works | "Create mode works (Oct 11 user report), with three issues" (`NOTES.md:659`). |
| Create: tools, gadgets and logic | untested beyond two gadgets | Only the Thruster and rockets/missiles appear (`NOTES.md:471-495`). A "Newton 'pieces floating' crash log was lost" (`NOTES.md:487-488`). Logic and the other tools: no record. |
| Create: Thruster freeze | works (fixed per notes) | Open in the Oct 11 list (`NOTES.md:665-668`), then: "Thrusters fixed by that" (Oct 14, `NOTES.md:477-481`; the cause was stubbed `sceGnmMapComputeQueue`/`sceGnmDingDong`). The missile crash that followed: "Rockets fixed by the type-0 change" (`NOTES.md:481-495`). |
| Create: camera snapping | known broken (reduced) | Four pacing fixes; "Left: a rare isolated 0 step (~1 per 100 flips)" (`NOTES.md:622-636`). "Camera snapping is game-side ... Next: find the frame dt" (`NOTES.md:637-645`). |
| Create: sky | known broken | "the create-mode sky looks yellow/beige where it should probably be blue" (`NOTES.md:663-664`). No later note. |
| Text entry (`libSceIme`, `libSceImeDialog`) | untested | All 8 imported functions are stubbed (see [Libraries](#stubbed-and-partly-implemented-libraries)). The "Keyboard" boot step passes on the stubs (`NOTES.md:262`, `:277`). |
| **Popit** | works (opens) | The Popit opens from Square or the R key, checked through the game's own `AnyPoppetActive__` (`NOTES.md:353-363`). Its contents and tools: no record. |
| Stickers and photos (`libSceJpegDec`, `libSceJpegEnc`, `libScePngDec`) | untested | JpegDec (5) and JpegEnc (4) are fully stubbed. JpegDec "needs a real decoder" (`NOTES.md:22-23`). LBP3 imports no `libScePngDec` function at all, so PNG doesn't matter. |
| **Audio:** FMOD through `libSceAudioOut` | works | "Audio confirmed working by ear" (`NOTES.md:243-247`). Each port is an SDL3 stream; output blocks to pace FMOD (`runtime/src/hle/audioout.cpp:1-8`). |
| Audio: sound banks | works | FMOD `.fev` banks load at boot (`NOTES.md:235`, `:263-264`). |
| Audio: streamed music and voice lines | works (indirect) | "keeps rendering at 30 fps with music" (`NOTES.md:558`); the cutscene's voice-line streams "opened and closed in order" (`NOTES.md:572-574`). Voice chat (`libSceVoice`) is stubbed and out of scope (`NOTES.md:259`). |
| **Saves:** `libSceSaveData` | works | Each save is a plain folder, `user\savedata\CUSA00473\<dir>\` under the working directory (`runtime/src/hle/savedata.cpp:1-3`, `:43-47`). `play.bat:10` makes that the project folder. The game writes saves (`NOTES.md:302`, `:517-518`); a crash right after a save write was fixed (`NOTES.md:517-524`). Titles, icons and backups are dropped (`savedata.cpp:197-208`). |
| Saves: the save dialog (`libSceSaveDataDialog`) | untested | All 8 functions are stubbed. |
| Saves: save, quit and reload | untested | No explicit test. Indirect evidence: "a session ending in Create reopens there" (`NOTES.md:654-656`); save folders were backed up between sessions (`NOTES.md:285`). |
| **Controller:** SDL3 pad buttons and sticks | works | XInput, DirectInput, DS4/DualSense, hot-plug; "Verified with `build/padtest.exe` on a ShanWan pad (Xbox 360 mode)" (`NOTES.md:372-377`). Keyboard actions as well (`NOTES.md:343-363`). |
| Controller: touchpad | untested | Fingers mapped to DS4 coordinates (`runtime/src/input/gamepad_sdl.cpp:272-279`); Back stands in for the touchpad click (`gamepad_sdl.cpp:46`). No in-game check. |
| Controller: motion | untested (partial) | Raw accelerometer and gyro are passed on, but the orientation stays at identity: "TODO: verify axis directions ... integrate the gyro into `orientation` if LBP3 reads it" (`gamepad_sdl.cpp:233`, `:264-270`; `NOTES.md:378`). |
| Controller: light bar, vibration | untested | Implemented (`runtime/src/hle/libScePad.cpp:81-95`); never mentioned in the notes. |
| **Pacing:** the 30 fps cap | works | `sceVideoOutSetFlipRate` gives 30 Hz (`NOTES.md:250-252`); "the game runs at its 30 fps cap" (`NOTES.md:421-425`). Flips run on a fixed vblank grid (`runtime/src/hle/videoout.cpp:64-95`). On a 144 Hz monitor 30 fps alternates 4 and 5 refreshes (`NOTES.md:646-650`). |
| Pacing: hitches | known broken | "Remaining: occasional 100-400 ms hitches in the Introduction" (`NOTES.md:651-653`). |
| Pacing: camera snapping | known broken (reduced) | See Create above; it also shows in the Introduction (`NOTES.md:637-641`). |
| Pacing: launched out of the level | known broken, fix unconfirmed | `NOTES.md:607-610` (above). |
| **Stubbed libraries** on the offline path | see below | IME, JPEG, the save, message and common dialogs, and parts of Gnm. |

## Stubbed and partly implemented libraries

The 590 in `NOTES.md:29-31` was counted on 2026-10-09, before most of the runtime existed. To get the current number, I matched the imports of all 8 game modules in this dump against upstream's registrations (`HLE_REG` and `REG` in `runtime/src/hle/*.cpp`, plus one NID registration in `kernel_fs.cpp:526`). I used the linker's own rules: lookup by (NID, library), with `libSceLibcInternal` aliased to `libc` (`runtime/src/core/linker.cpp:183-224`, `runtime/src/hle/hle.cpp:18`).

- 733 unique (library, function) imports: 186 come from the game's own modules, 356 from the runtime, and **191 fall to the generic stub** (189 functions and 2 variables). The stub logs its first 3 calls and returns 0 (`linker.cpp:108-113`).
- The eboot alone imports 646 functions from 51 libraries, the same count as `NOTES.md:16`. `libScePngDec`, `libSceAudioIn` and `libSceInvitationDialog` are in its import table, but no function is imported from them.

On the offline finish line (stubbed / imported):

| Library | Stubbed | Why it matters |
|---|---|---|
| `libSceImeDialog`, `libSceIme` | 5/5, 3/3 | Create-mode text entry. With the stubs, `sceImeDialogGetStatus` returns 0 ("none"). Whether the game waits on that is untested. |
| `libSceJpegDec`, `libSceJpegEnc` | 5/5, 4/4 | Photos and picture stickers (likely). Untested. |
| `libSceSaveDataDialog` | 8/8 | System save dialogs (errors, no space). Not seen on the normal path. |
| `libSceMsgDialog`, `libSceCommonDialog` | 4/4, 1/1 | System message boxes. Not seen on the normal path. |
| `libSceGnmDriver` | 19/50 | Includes `sceGnmSetGsRingSizes`, `sceGnmGetTheTessellationFactorRingBufferBaseAddress`, `sceGnmRequestFlipAndSubmitDone`, `sceGnmFlushGarlic`, `sceGnmGetEqTimeStamp` and debug (Razor, mip stats). Partly implemented: `sceGnmSetEs/Gs/Hs/LsShader`, `UpdateGs/Hs` and `UpdatePs/VsShader` only write a NOP (`runtime/src/hle/gnm_packets.cpp:66-89`; "Es/Gs/Hs/Ls still NOP fills", `NOTES.md:198`). A later level with geometry or tessellation shaders would render wrong; whether one exists is unknown. |
| `libSceVideoOut` | 4/9 | Gamma, colour adjust, margins, capability info. Brightness calibration probably has no effect. Low. |
| `libSceAppContent` | 1/7 | `sceAppContentTemporaryDataFormat`. DLC is reported absent by design (`runtime/src/hle/app_content.cpp:1`, `:39-46`). Low. |
| `libSceSysmodule`, `libSceSystemService` | 3/3, 1/5 | Load and is-loaded return 0 (success); `sceSystemServiceReceiveEvent`. Low. |
| `libkernel` | 18/139 | Signals, `sysctl`, `getrusage`, backtrace and debug helpers, `sceKernelGetModuleInfoFromAddr`, `_exit`, `__pthread_cxa_finalize`; the variables `__progname` and `__stack_chk_guard` (the guard gets a random value, `linker.cpp:217-220`). Low. |

Out of scope or unused offline: `libSceAvPlayer` 15/15 (no video uses it), `libSceVoice` 10/10, `libSceCamera` 9/9, `libSceMove` 5/5, `libSceMoveTracker` 6/6, `libSceNpWebApi` 20/20, `libSceNpSignaling` 7/7, `libSceNpCommerce` 7/7, `libSceNpUtility` lookups 5/8, `libSceCompanionHttpd` 6/6, `libSceGameLiveStreaming` 10/10, `libSceGameCustomDataDialog` 6/6, `libSceContentSearch` 5/5, `libSceCoredump` 4/4.

## Open problems, as the notes describe them

1. The Legend of Bunkum ends on a white checkpoint; it is auto-skipped and the "Real cause [is] still open" (`NOTES.md:568-596`). A likely fix is under [Port family fixes](#port-family-fixes).
2. Sackboy launched out of Needlepoint Peaks; "Recheck after the fix" (`NOTES.md:607-610`).
3. Camera snapping is "game-side"; the frame delta-time hasn't been found (`NOTES.md:637-645`).
4. Create-mode sky is yellow/beige (`NOTES.md:663-664`).
5. 100-400 ms hitches in the Introduction (`NOTES.md:651-653`); some boots stall 30-40 s (`NOTES.md:368-370`).
6. The GPU rests on workarounds. Every submit is synchronous and costs ~2.5 ms a frame: "Proper fix later" (`NOTES.md:254-257`). A queue blocked on memory releases the guest after a 20 ms grace and learns addresses (`NOTES.md:597-605`, `:626-629`). The display readback goes through a slow Finish (`NOTES.md:197-198`).
7. The "Newton 'pieces floating'" crash, whose log was lost (`NOTES.md:487-488`); the Newton lamps fix awaits confirmation (`NOTES.md:476`).
8. Motion axes are unverified and the gyro isn't integrated (`NOTES.md:378`).
9. JpegDec "needs a real decoder" (`NOTES.md:22-23`).
10. Keyboard only: the Popit won't open while in reach of the Pod computer (`NOTES.md:361-363`).

## Real-PS4 baseline

- **What still works on a console.** Sony: "Offline features such as the campaign will remain playable"; locally stored levels stay available; new ones can be played "but not shared" ([PlayStation game servers page](https://www.playstation.com/en-us/legal/gameservers/)). LBP Union's FAQ (2024-04-20): "Story Mode, Create Mode and Level Pack DLC of any LittleBigPlanet title is available to play" in single player or local co-op; "Only local experiences are available" ([LBP Union](https://www.lbpunion.com/post/breaking-lbp3-ps4-servers-discontinued/)).
- **The message at boot.** For LBP titles generally, LBP Union quotes the failure as "a problem occurred when attempting to connect to the LittleBigPlanet servers 403" or "0xFFFFFFFF" ([LBP Union server status](https://www.lbpunion.com/lbp-server-status/)). No first-hand account of the exact LBP3 PS4 wording, or of it appearing on every boot, was found; Reddit, PSNProfiles and ResetEra were unreachable or silent. **Unknown on hardware.**
- **What the game's code does.** Upstream's reverse engineering shows the game trying to connect on every boot: boot step 75, "Connecting to LittleBigPlanet(TM)3 online..." (`NOTES.md:95`, `:200-202`). On failure it shows its own message and takes its offline flow (`NOTES.md:410-415`; `patches.cpp:30-35`). So a network message on each boot is the expected behaviour, not a port bug. btsouth's Linux port reports the same: it "reports that its online servers are unreachable, and goes on offline" ([README](https://github.com/btsouth/lbp3-pc/blob/00eabd13c0fd4e0ea3e29b1aafa2edf00b439764/README.md)).
- **Two dialogs for the baseline run to tell apart.** By default the dialog is the game's own, drawn in the game window. The Windows message box ("LittleBigPlanet 3 couldn't connect to its online services", `runtime/src/hle/dialogs.cpp:44-52`) is `sceErrorDialog`, which only appears in the `LBP3_NET=online` experiments (`NOTES.md:211-221`).
- **Two console states.** Upstream emulates a console that is signed out with no network (`NOTES.md:239-242`). btsouth emulates one signed in to PSN with the game's servers unreachable (`BB_NP_SIGNED_IN=1`, same README). A real PS4 can be in either state.

## Dump layout

- **The SELF wrapper is a blocker.** `Module::Load` throws "is not a decrypted ELF" unless a file starts with `\x7FELF` (`runtime/src/core/module.cpp:55-56`). No code path in upstream handles a SELF. In this dump, `eboot.bin`, `sce_module/libc.prx`, `sce_module/libSceFios2.prx` and the five `gamedata_orbis/spu/*.prx` all start `4F 15 3D 1D`: fake-signed SELFs. Their data segments carry neither the encrypted (0x2) nor the compressed (0x8) flag, and stored size equals real size. So unwrapping to ELF is mechanical (an example is under [Port family fixes](#port-family-fixes)). Upstream's game folder holds an `itemzflow` directory (`NOTES.md:611-612`), so its dump was probably made with the ItemzFlow homebrew, which writes plain ELFs; that is an inference, not something the notes say.
- **The merged folder works.** `GameDir()` takes `LBP3_GAME_DIR`, else the first line of `game_dir.txt`, else `..\LBP3 Game Files` (`runtime/src/main.cpp:45-57`). The runtime then uses `<dir>\CUSA00473-patch` and `<dir>\CUSA00473` and only requires `<dir>\CUSA00473\eboot.bin` (`main.cpp:71-79`). A missing patch folder is skipped: module search (`main.cpp:38-43`) and every `/app0` lookup (`runtime/src/core/vfs.cpp:115-121`, `:143-146`) test each layer with `exists`. So `LBP3_GAME_DIR=D:\Recomps\ROMs\PS4` serves this dump, base and patch together, once the SELF problem is solved. Alternatively, pass the merged folder as both positional directories (`main.cpp:67-69`).
- **Upstream's Python tools assume two folders.** `tools/gamedir.py:24-25` hard-codes `CUSA00473-patch/eboot.bin`, and `tools/ps4_imports.py` expects a plain ELF.
- **This eboot is the build upstream targets.** `param.sfo` says `APP_VER 01.28`. The import counts match (51 libraries, 646 functions, `NOTES.md:16`). All seven byte-checked sites in `patches.cpp` hold the expected bytes in this dump's eboot.
- **The dump stays read-only.** `/app0` writes fail with `EROFS` (`runtime/src/hle/kernel_fs.cpp:176`). Saves, logs and caches go to `user\` and `analysis\` in the working folder (`main.cpp:144-145`).

## Port family fixes

1. **The Bunkum hang, from btsouth/lbp3-pc** (an LBP3 port built on bloodborne_pc's bbport). Commit [`45bb339`](https://github.com/btsouth/lbp3-pc/commit/45bb3390456fcd01491ef733e5a5097b32bd76fa) starts every guest thread and the main thread with MXCSR `0x9fc0` (denormals are zero, flush to zero) and x87 control word `0x037f`. Its comment: with denormals kept, "LittleBigPlanet 3's physics turns resting bodies' tiny velocities into NaN ...; the game then deletes those bodies (and the cutscene cameras joined to them), which ends The Legend of Bunkum on a white screen." The README commit just before it reads "The Legend of Bunkum reaches a white screen" (`e09d6fd`); the one after, "plays to its end and goes on to Needlepoint Peaks" (`6cde70e`). Upstream sets that state only for fibers (`runtime/src/hle/fiber.cpp:240-244`). Game threads (`runtime/src/hle/kernel_pthread.cpp:124-138`) and the main thread run with Windows' default MXCSR. That matches upstream's symptom of "the last cutscene camera has finished" (`NOTES.md:584-591`). It may also bear on Sackboy being launched out of Needlepoint Peaks (physics), but that is a guess.
2. **SELF to ELF, from gravityrush_pc.** `parse_self` in [`scripts/prepare.py:48-95`](https://github.com/LucaPazienza/gravityrush_pc/blob/9eb14ef3140c1736af44f66d7ab3bc4933a2dcda/scripts/prepare.py#L48-L95) rebuilds the ELF image from a plaintext fake-signed SELF. It refuses encrypted or compressed segments.
3. **System dialogs and text entry, from gravityrush_pc and bloodborne_pc.** `src/runtime_services.c` in both implements ImeDialog as keyboard text typed into the game window, plus MsgDialog, SaveDataDialog (with a save list) and CommonDialog for "an offline, single-user console" ([gravityrush_pc, lines 380-440 and 719-735](https://github.com/LucaPazienza/gravityrush_pc/blob/9eb14ef3140c1736af44f66d7ab3bc4933a2dcda/src/runtime_services.c#L380-L440); [bloodborne_pc](https://github.com/deadinside28/bloodborne_pc/blob/0fc2921c45f8ff3900f6bfa43a299e9a4f532af4/src/runtime_services.c#L224-L277)). These are C runtimes with a different design, so the logic ports over but the code doesn't drop in.
4. **Motion and touchpad on pads without them, from bloodborne_pc.** [`src/runtime_pad/motion.c`](https://github.com/deadinside28/bloodborne_pc/blob/0fc2921c45f8ff3900f6bfa43a299e9a4f532af4/src/runtime_pad/motion.c) reports a consistent orientation quaternion and synthesises tilt, shake and touchpad swipes from the keyboard, mouse or an Xbox pad. That covers upstream's identity orientation and Back-as-touchpad (`gamepad_sdl.cpp:46`, `:233`).
5. **No match found.** JPEG: iss_pc only returns "unsupported" errors (`port/src/runtime_gr2.c:354-355`). Camera snapping, the yellow sky and hitches are game- or GPU-side. bloodborne_pc's `BB_FLIP_RATE` is a frame-rate override (`gpu/platform/videoout/videoout.cpp:138-152`), which is out of scope. issn_pc has nothing beyond the shared `runtime_services.c`.
