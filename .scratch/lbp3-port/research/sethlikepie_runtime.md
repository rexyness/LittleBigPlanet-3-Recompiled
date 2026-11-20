# Lane (sethlikepie study): runtime HLE, online, launcher and scripts, seth's port against ours (2026-11-19)

Part of [sethlikepie_vs_open_items.md](sethlikepie_vs_open_items.md). Read-only study of **sethlikepie/littlebigplanet3-pc `07cc7e7`** (from a temporary clone; check out `07cc7e7` from github.com/sethlikepie/littlebigplanet3-pc to follow the `seth:` citations), against our runtime `main` `29dad5a` (v0.1.7) and the notes in `.scratch\lbp3-port\`. Nothing was built or run; no seth script was executed. The only things run were two small read-only parsers of ours (`python -I`, in a temporary folder since deleted) over the dump's FSB headers and `param.sfo`, plus greps of our own logs.

Labels: **read** = from code, logs or files; **seth's claim** = their comments or docs, not measured by us; **inferred** = our reasoning.

Citations: `seth:<path>:<line>`, `lbp3-recompiled:<path>:<line>`, `notes:<path>`, `lbppc:<path>:<line>`.

## Bottom line

- **Nothing in seth touches the four open online items** (rejoin with no spawn, lost-message hitches, testing across networks, Dive In with strangers) **[read]**.
  - Seth has no shadNet client, no signaling peers and no P2P transport. P2P sockets stay on the loopback.
  - None of the rejoin addresses from `coop/rejoin_no_spawn.md` appear anywhere in the repo: `0x130d5c0`, `0x3ac240`, `0x3acef0`, `0x228a10`, `0x12cc4e8`, `0x3778d0`, `0x3604a0`, `0x378120`.
  - Seth's online is a "phase 1" that ours passed on 2026-11-13.
- **No Intel hybrid, P-core or E-core handling** **[read]**.
  - No CPU-vendor or CPUID code, and no thread priority or affinity applied.
  - No exact maths either: no `rcpps`, `rsqrtps`, `dpps`, "exact" or "diverge" anywhere in `src`, `gpu/shim` or `scripts`, including the 12k-line `runtime_lbp3_60fps.c` **[read]**. So seth on Intel should also hit the intro stall our v0.1.6 fixed **[inferred]**.
  - The separate Intel 12th-gen crash stays open in ours (`lbp3-recompiled:tools/package/RELEASE-NOTES.md:123-124`), and seth has nothing for it.
- **Correction to the brief: LBP3 doesn't import AJM or Videodec** **[read]**.
  - Seth's `runtime_ajm.c`, `runtime_mp3.c` and `runtime_videodec.c` are code carried over from the gow3_pc lineage.
  - AvPlayer *is* imported. But it's only reached by the PS4 Share-video gallery and by online video overlays, and neither fires today. **No player-visible media gap in ours.**
- **Worth taking, ranked:**
  1. automatic save backup before Play;
  2. trophies;
  3. DLC straight from `.pkg` files;
  4. an in-game "save a report" key;
  5. the timer-resolution opt-out for Windows 11;
  6. a "where the player is" line for Discord and the launcher;
  7. the game's own HTTP timeouts and abort;
  8. Add to Steam;
  9. third-party XML patches.
- **A brightness lead, half closed (ticket 17, cross-lane):**
  - At default settings, the game never calls `sceVideoOutAdjustColor_` or `sceVideoOutColorSettingsSetGamma_` **[read]**: not at boot, not in play, and not when the new-profile brightness screen is passed at its default.
  - So they're not the cause of the over-brightness at defaults.
  - **Untested:** moving the brightness slider (calibration screen or Options). The eboot imports both functions, so some caller exists. If the slider is it, the brightness setting does nothing in ours and seth's path is the fix (§8).

## Verdict table

| # | Item | Seth | Ours | Verdict | Effort | Risk (incl. lockstep) | Open items touched |
|---|---|---|---|---|---|---|---|
| 1 | Save backup before every start | yes, keeps 10 | manual and before-restore only | portable (idea) | S | none | none (save safety) |
| 2 | Trophies (unlock, popup, list) | yes; names need the player's console key | log only | portable with rework | M | none: the game is told "OK" either way | none; player-visible |
| 3 | DLC from `.pkg` without extracting | yes (reads 0x80 header bytes) | needs third-party `pkg_extractor.exe` | portable with rework | S | none new | friend setup |
| 4 | In-game report key (zip at the moment) | F12, with a screenshot and place | launcher zip only | portable (idea) | S–M | none | testing across networks (indirect) |
| 5 | Timer-resolution opt-out (Windows 11, window covered) | yes, 0.5 ms tick | `timeBeginPeriod(1)` only | portable as-is | S | none for lockstep; tick gate needed | co-op hitches (inferred, Win 11 only) |
| 6 | "Where the player is" (Discord, launcher pill) | in-game, from CGame fields | launcher, level from the log | portable with rework | S–M | read-only memory reads | none |
| 7 | Honour the game's HTTP timeouts and abort | yes | fixed 10/10/15/30 s; abort no-op | portable with rework (log first) | S | none | slow networks (inferred) |
| 8 | Add to Steam (non-Steam shortcut with art) | yes (native launcher) | desktop shortcuts | portable with rework | M | none | none |
| 9 | Third-party shadPS4/GoldHEN XML patches | yes | none | portable with rework | M | **high**: both co-op players need the same set | none |
| 10 | AvPlayer (FFmpeg) | shadPS4's, registered | generic stub (returns 0) | not worth it | M–L | none | none |
| 11 | AJM / MP3 / Videodec | implemented | none | not applicable (not imported) | — | — | — |
| 12 | JPEG dec/enc | stb | stb | already have | — | — | — |
| 13 | Online (server, NP, Web API, P2P) | phase 1, no P2P | shadNet, Web API forwarding, L3P1 link | not applicable (ours ahead) | — | — | none |
| 14 | Thread FPU state, priority, affinity, stacks | 0x9fc0/0x037f; recorded only | same | already have | — | — | Intel: nothing |
| 15 | Memory model (one committed section) | yes | per-mapping commit | not worth it | L | — | — |
| 16 | SaveData param/icon kept, sorted search | yes | not kept, unsorted | not worth it now | S | — | — |
| 17 | PS4 save import | `tools/import_ps4_save.py` | `tools/ps4_save_import.py` | already have (ours safer) | — | — | — |
| 18 | Mods (`mods.py`) | GoW3 layout | `file_edits` | not applicable | — | — | — |
| 19 | Audio output pacing | steady clock, SDL's downmix | queue pacing + our downmix | not worth it (ours ahead on level) | — | — | — |
| 20 | Timed waits with `tv_nsec == 1e9` | tolerated | `sem_timedwait` already tolerant | already have | (S optional) | — | — |
| 21 | Launcher translations; Standard/Online builds | yes | no | not worth it | — | — | — |
| 22 | Video-out gamma (`AdjustColor`) | shadPS4's, to the presenter | stub | open: depends on the slider test | S–M if needed | none | brightness (ticket 17) |
| 23 | Fiber `DeallocationStack` swap | yes | `StackBase`/`StackLimit` only | not worth it now | S | none | none |

## 1. Media libraries: AvPlayer, AJM, MP3, Videodec, JPEG

**What LBP3 imports [read]**
- `lbp3-recompiled:analysis/eboot_imports.json`: 51 import libraries, no `libSceAjm` or `libSceVideodec`, in neither `imports` nor `needed`. `libSceAvPlayer` has 15 functions.
- The linker's missing list over **all** loaded modules (eboot, libc, Fios2, the four FMOD prx and spu.prx) has no Ajm or Videodec either (`lbp3-recompiled:analysis/boot_b1_A_stdout.txt:15-34`).
- Seth's NID table also names `libSceMouse`, `libSceNpMatching2` and `libSceNpProfileDialog` (`seth:src/import_names.inc`), none of which LBP3 imports. That table was carried from earlier ports, and AJM/Videodec came along with it **[inferred]**: GoW3 uses AJM and Videodec, and `seth:scripts/lbp3_compat.py` still maps the old `GOW3_*` names.

**Audio formats in the dump [read]**
- 6,178 `.mp3` files (`gamedata/audio/dialogue`, `lbp3`, `lbp3dlc`).
- FSB banks: 596 FSB4, 15 FSB5 IMA ADPCM (mode 7) and 25 FSB5 MPEG (mode 11). No ATRAC9 (mode 13).
- So FMOD in the eboot decodes everything in software **[inferred]**. That fits our gates hearing all Bunkum voice lines (16/16).
- There is no MP3 music import on LBP3.
- **Verdict on AJM / MP3 / Videodec: not applicable.** (Seth: `seth:src/runtime_ajm.c:1-6`, `seth:src/runtime_mp3.c:1-3`, `seth:src/runtime_videodec.c:1-3`, registered at `seth:src/runtime.c:292-295`.)

**Videos [read]**
- Every in-game movie is Bink: 45 `.bik` files under `gamedata/videos` and `gamedata_orbis/videos`, and Bink is linked into the eboot (string `BinkStartVideo`).

**AvPlayer**
- **What LBP3 uses it for [inferred from strings near each other in `analysis/eboot.elf`; no cross-reference to the import thunk]:**
  - **Share video player:** strings at 0xe5479f–0xe548ac: "Share Video Player Mutex", "Share Video Work/Texture Heap", "Share Video Thread", "Share Video Upload Thread", `/video/upload/`. Script natives at 0xec350b–0xec3640: `FindVideos__`, `GetVideoPathByIndex__i`, `IsVideoBuffering__`, `PlayVideo__Q7StringAQ7StringAbb`. These are the player's PS4 captures, found through `libSceContentSearch`.
  - **Online video overlays:** `/developer_videos?language=` (0xe52024), `/store_videos.xml` (0xe52040), `GetBestVideoUrl__iiji`, `PlayOnlineVideoOverlay__Q7StringA`.
- **What happens in ours [read]:**
  - An import with no HLE goes to `UnimplementedCall`, which logs the first 3 calls and returns 0 (`lbp3-recompiled:runtime/src/core/linker.cpp:110-141`, stub made at `:252-253`). So `sceAvPlayerInit` would hand back a NULL player.
  - It is never called. There is no `[stub] libSceAvPlayer ::` line in:
    - `analysis/play_log*.txt` (4 user sessions);
    - any `analysis/*_stderr.txt` (the test runs);
    - `analysis/online/*.txt` (the online runs);
    - the package's fresh-profile runs (`D:\Recomps\lbp3-dist\test-v012\report\*_stderr.txt`).
  - ContentSearch is called at boot and finds nothing (`[stub] libSceContentSearch :: sceContentSearchSearchContent`). Our `jpeg.cpp:15-18` already notes that path is unreachable offline.
  - Online, play.lbppc.com answers `developer_videos` with 200 and 0 bytes, and `store_videos.xml` with 404 (`lbp3-recompiled:analysis/online/k1_stderr.txt`). There is nothing to play.
- **Seth:** shadPS4's AvPlayer on FFmpeg, registered through the GPU library (`seth:gpu/CMakeLists.txt:187-188`, `seth:gpu/VENDOR.txt:4`, `seth:gpu/shim/lbp3gpu.cpp:276`). Release builds ship FFmpeg (`seth:README.md:112`).
- **Verdict: not worth it.** No player-visible gap. FFmpeg would be a large new dependency for a path that never fires. Revisit only if a server starts serving developer videos.

**JPEG: already have.** Ours (`lbp3-recompiled:runtime/src/hle/jpeg.cpp:1-22`) and seth (`seth:src/runtime_jpeg.c:1-15`) both use stb. Seth marks its JpegDec layouts "UNVERIFIED".

## 2. Trophies

**Seth [read]**
- Unlocks are kept per user in `<user>/trophy/<NP comm ID>.txt`. A trophy unlocks once, and the platinum comes by itself (`seth:src/runtime_services.c:540-549`, unlock at `:705`).
- Unlocking shows a popup with the trophy's picture and grade, plus an optional WAV the player supplies.
- The overlay has a Trophies tab (`seth:src/trophy_info.h:9-22`).
- `GetTrophyUnlockState` returns real flags (`:806`).
- Off by default without a key. While off, nothing unlocks, but the game is still told it worked (`:711`).

**`seth:scripts/trophies.py`**
- GPL-2.0-or-later (SPDX at line 2), with its own pure-Python AES.
- Unpacks `sce_sys/trophy/trophy00.trp`. The pictures aren't encrypted. The names and grades in `TROP.ESFM` need the console's trophy key (`LBP3_TROPHY_KEY`, or `ReleaseTrophyKey` in a shadPS4 `keys.json`) (`:1-17`, seth's claim).
- The native launcher stores the key (`seth:launcher/native/lbp3_launcher.cpp:503`), and the bug report scrubs it.

**Ours and LBPPC**
- Ours logs only and reports nothing unlocked (`lbp3-recompiled:runtime/src/hle/trophy.cpp:1-4`, `:41`, `:64`).
- LBPPC is the same as ours (`lbppc:src/runtime_services.c:379`, `lbppc:src/runtime_extra.c:425`).
- The game really unlocks trophies: `[trophy] unlocked trophy 13` in our play logs **[read]**. The dump has `sce_sys/trophy/trophy00.trp` (5.3 MB).

**Verdict: portable with rework, M.**
- Runtime: unlock store, unlock-state flags, a popup on our ImGui overlay.
- Launcher: a list, with `.trp` extraction in C++ (Windows BCrypt AES-CBC instead of seth's Python).
- The key is the player's own: never shipped, never logged, kept out of reports.
- Without a key: pictures and "trophy N" only.
- **Lockstep risk: none.** It's a scope call: `notes:map.md:56` lists trophies outside the offline finish line.

## 3. Online

**Server protocol [read]**
- An LBP HTTP game server: "a community server such as Bonsai" (`seth:README.md:61-63`).
- The server patch is sgdc3's "Bonsai online patch", which the player supplies ("No online patch is included"). It lives in a code cave after `.text` (`seth:src/probe.c:561`).

**NP [read]**
- A local sign-in under `online_id` from `lbp3.ini`, with Plus, an adult and the auth code "AUTHEN" (`seth:src/runtime_online.c:1-18`, `:214-300`).
- The Web API is answered in process as an empty social graph (`seth:src/runtime_npwebapi.c:1-18`).
- Signaling makes a context with no peers (`:205-209`). Matching2 fails.

**Co-op transport [read]**
- Online, plain TCP and UDP sockets get host Winsock sockets (`seth:src/runtime_netloop.c:170`).
- **P2P sockets stay on the loopback** ("no shadNet/P2P transport yet", `:17`), so a datagram to a remote peer is dropped.
- Raw sockets refuse sends (`:17-19`); ours sends the ICMP echo.
- The README and native launcher say "online co-op is built in" (`seth:README.md:61`, `seth:launcher/native/lbp3_launcher.cpp:54`). That is seth's claim; the code can't carry a remote player. Local co-op while online is possible **[inferred]**.
- No `shadnet`, STUN, UPnP or relay code anywhere in the repo (only NID names and log classes), and none of the rejoin addresses **[read]**.

**HTTP [read]**
- **Seth:** a worker thread with non-blocking mode and epoll (`seth:src/runtime_http.c:1-14`).
  - LBP3 imports none of `SetNonblock`, `CreateEpoll` or `WaitRequest`: 38 HTTP imports, all blocking-style. So that machinery is unused here.
  - Seth does honour the game's connect, send and receive timeouts (`:223-224`), `AbortRequest` (`:575`), and headers set on templates and connections (`:390-405`).
- **Ours:** fixed WinHTTP timeouts of 10/10/15/30 s (`lbp3-recompiled:runtime/src/hle/http_online.cpp:93`), abort a no-op (`:649`), template and connection headers dropped (`:725`), the timeout setters no-ops (`:746-751`).
- **Verdict (item 7): portable with rework, S; low.**
  - First log the values the game passes to `sceHttpSet*TimeOut`. Only if they are shorter than ours does honouring them matter, on slow networks **[inferred]**.
  - HTTP isn't on the simulation path, so there's no lockstep risk.
- **gzip:** seth says "LittleBigPlanet 3 inflates itself" (`seth:src/runtime_http.c:10-11`, seth's claim). Our r3 run measured the opposite against Refresh (`notes:best/online.md`, "Compression"). Keep ours.

**Verdict on online: not applicable.** Ours is ahead on every piece: shadNet, Web API forwarding, push events, invitations, the L3P1 link with DATA3, join protection, exact maths, ICMP and NAT info. **Nothing here for the four open online items.**

## 4. Threads, FPU state, timers (and Intel)

**FPU state, priority, affinity, stacks: already have [read].**

| | Seth | Ours |
|---|---|---|
| MXCSR / x87 control word | 0x9fc0 / 0x037f on every guest thread (`seth:src/runtime_thread.c:255-259`, `seth:src/runtime.h:20-29`), on the main thread, and in fibers | same (`lbp3-recompiled:runtime/src/hle/kernel_pthread.cpp:122-129`), fibers in `fiber.cpp` |
| Priority, affinity | recorded only (`seth:src/runtime_thread.c:4`) | recorded only (`kernel_pthread.cpp:398`, `:422`) |
| Stack | 1 MiB default plus a 256 KiB margin, 64 KiB minimum (`:27-29`) | 2 MiB default, 1 MiB minimum, the whole stack committed (`:30-37`, `:107-119`) |

**Intel [read]**
- No CPUID or vendor check, no `SetThreadPriority`, affinity, CPU sets or EcoQoS.
- An illegal instruction ends the game with a report (`seth:src/probe.c:182-190`). Nothing for the 12th-gen crashes.

**Timer resolution (item 5)**
- **Seth** sets a 0.5 ms tick with `NtSetTimerResolution`. It also opts out of Windows 11's power throttling of timer requests (`PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION`), so the tick holds while the window is covered (`seth:src/compat_win.c:112-133`). Their reason: Bink polls at 1 ms (seth's claim).
- **Ours** calls `timeBeginPeriod(1)` once (`lbp3-recompiled:runtime/src/hle/kernel_time.cpp:55`). Two waits depend on it:
  - the game host thread's epoll wait sleeps in 1 ms steps (`net_offline.cpp:588-594`);
  - audio pacing sleeps 1 ms at a time (`audioout.cpp:302-305`).
- **Windows 11:** the `timeBeginPeriod` documentation says: "Starting with Windows 11, if a window-owning process becomes fully occluded, minimized, or otherwise invisible or inaudible to the end user, Windows does not guarantee a higher resolution than the default system resolution." It points to `SetProcessInformation` for the opt-out (learn.microsoft.com, checked 2026-11-19; not measured here).
  - A friend on Windows 11 who minimizes or covers the game in co-op could then get 15 ms oversleeps on the 33.3 ms tick: 0-then-2 steps and late P2P reads **[inferred]**.
  - This PC runs Windows 10, so it can't show it.
- **Verdict: portable as-is, S.** One `SetProcessInformation` call next to `timeBeginPeriod`.
  - Gate: Introduction steps ≠ 1 under 1% (×2) and Thruster 3/3.
  - No lockstep effect (timing only).

**Timed waits with `tv_nsec == 1e9` (item 20)**
- Seth: "RAD's Bink wait helper can build tv_nsec == 1e9 … ud2 on EINVAL" (`seth:src/runtime_evf.c:327-329`, seth's claim). Seth tolerates it there, in `nanosleep` (`seth:src/runtime_kernel.c:122`) and in condition waits (`seth:src/runtime_mutex.c:262`).
- Ours: `sem_timedwait` never rejects (`lbp3-recompiled:runtime/src/hle/kernel_sync.cpp:241-250`). `nanosleep` returns EINVAL for 1e9 or more (`kernel_time.cpp:284`), but the eboot doesn't import `nanosleep` **[read]**.
  - Its timed waits are `sem_timedwait`, `sceKernelSleep`, `sceKernelUsleep` and `scePthreadMutexTimedlock`.
  - The last three take microseconds, not a timespec.
- **Already have.** Optional one-line hardening: carry `nsec` into `sec` in `nanosleep`.

## 5. Memory, saves, files, mods

**Memory: not worth it**
- **Seth:** one pagefile-backed section, committed at start (`SEC_COMMIT`), so any physical range can be mapped twice.
  - Size: 5,056 MiB, up to 16 GiB with `LBP3_DMEM_MB`, plus 1 GiB flexible (`seth:src/runtime_memory.c:26-36`, `:84`, `:244`).
  - It retries for 30 s when the commit limit is hit (`:239-250`).
  - The README's "about 19 GB" is seth's claim.
- **Ours:** commits per mapping; aliasing isn't supported and is logged (`lbp3-recompiled:runtime/src/hle/kernel_memory.cpp:1-5`, `:43-54`).
- LBP3 runs on ours, and seth's model costs friends more commit.

**SaveData: already have**
- Seth keeps the param and icon next to each save, and sorts searches by key and order, filling params and infos (`seth:src/runtime_savedata.c:194-230`, `:260-290`).
- Ours logs `SetParam` and `SaveIcon`, and searches in directory order without params (`lbp3-recompiled:runtime/src/hle/savedata.cpp:199-244`).
- LBP3's searches (`%xPBACKUPx%`, `%LEVEL%`, `%ADV%`) and our import list with dates already work and are tested (`notes:m2/dialogs.md`). Keeping the params is optional polish.

**Save backup before every start (item 1): portable (idea), S**
- **Seth** copies `savedata` before every start, keeps 10, and a restore first backs up the current saves (`seth:scripts/save_backup.py:5`, `seth:run.py:398`).
- **Ours** has `BackupSaves` (`lbp3-recompiled:runtime/launcher/launcher_sys.cpp:327`). Only two callers: the manual button (`launcher_pages.cpp:192`) and before a restore (`launcher_sys.cpp:346`). Nothing runs before Play **[read]**.
- Add one call before Play, and prune to N.
- The notes hold no earlier decision on automatic backups (grep for "backup" in `.scratch\lbp3-port\`).
- Why it ranks first: online co-op writes saves, and one session's save was lost on 2026-11-13 (`notes:best/online.md`, "Save incident").

**PS4 save import: already have**
- Ours routes `bigfart`/`littlefart`/`playfart` by name, backs up first and refuses while the game runs (`lbp3-recompiled:tools/ps4_save_import.py:1-17`).
- Seth's copies folders and writes `param.bin` (`seth:tools/import_ps4_save.py:1-9`).

**Mods: not applicable**
- `seth:scripts/mods.py:22-32` requires a `dvdroot_ps4` folder. Its tests use `.wad`/`.dcx` assets (`seth:tests/test_mods.py:22`); that's the GoW3 layout.
- The LBP3 dump has no `dvdroot_ps4`. Ours already serves edited files in memory (`file_edits.cpp`, the Quit Game edit).

## 6. DLC (AppContent)

**Seth [read]**
- Reads each `.pkg` header from `<data>/dlc`: magic `7F CNT`, content ID at 0x40, content type 0x1b/0x1c at 0x74 (`seth:src/runtime_content.c:1-7`, `:87-105`). No extraction: LBP3's packs are licences only.
- Maps each content-ID prefix to a service label from `param.sfo`'s `SERVICE_ID_ADDCONT_ADD_n` (`:107-120`), and lists per label (`:144`).
- The native launcher names packs from the game's own `DLCt` catalog inside the `.farc` archives (`seth:launcher/native/lbp3_launcher.cpp:677-790`).

**Ours**
- Needs extracted folders. The launcher runs the third-party `pkg_extractor.exe`, which a friend must bring (`lbp3-recompiled:runtime/launcher/launcher_pages.cpp:30-58`; `notes:best/package.md:21`).
- Lists every pack under label 0 (`notes:issues/18-dlc.md`).

**The dump's service labels [read]**
- `param.sfo` names `SERVICE_ID_ADDCONT_ADD_1 = UP9000-BCUS98148_00` (LBP1) and `ADD_2 = UP9000-BCUS98245_00` (LBP2).
- On a console, the PS3 pack `PLITTLEBIG000005` would be listed under label 1 **[inferred]**. Ticket 18's decompile says the game treats every label alike, so no change is expected. Seth's mapping is simply the console-faithful one.

**Verdict: portable with rework, S.**
- Accept `.pkg` files in the DLC folder by reading their first 0x80 bytes, and keep the extracted-folder path.
- This drops `pkg_extractor` from a friend's setup.
- No new lockstep risk: DLC already differs per player.
- Catalog names are a nice-to-have (M).

## 7. Launcher and scripts

**In-game report key (item 4): portable (idea), S–M**
- **Seth:** F12 (or F1 > About), or the launcher's "Report a problem", writes a zip to the Desktop (`seth:scripts/bug_report.py:1-13`, `:37-46`). It holds:
  - the last 20 MB of the two newest logs;
  - crash, hang and GPU-fault reports;
  - a screenshot and the place in the game;
  - PC details and an eboot check.
  - The Windows user name, PC name, player names and PSN IDs are scrubbed; the trophy key and saves are never included.
- **Ours:** "Make a report zip" in the launcher (`lbp3-recompiled:runtime/launcher/launcher_sys.cpp:610-634`). The friend's launcher kept only two sessions (`notes:coop/rejoin_no_spawn.md`).
- A hotkey that writes the zip at the moment (with the `[p2p]`/`[slots]` lines and a screenshot) gets timelier friend reports for "untested across networks". Scrub online names too.

**"Where the player is" (item 6): portable with rework, S–M**
- **Seth:** an in-game Discord thread with a built-in app ID (`seth:src/runtime_discord.c:1-10`, `:29`). The place comes from CGame `eboot+0x12ecc80` (`seth:src/runtime_lbp3_60fps.c:144`), read pointer-checked from another thread (`:12217-12350`):
  - the slot type at +0x2f8 (5 = Pod);
  - +0x4f5 "is Pod";
  - +0x108 == 1 while creating;
  - +0x398, the story level's name (UTF-16);
  - +0x3c8, the local player count;
  - player+0xb08 UI bits: 0x4000 paused, 0x80/0x400 Popit;
  - a Bink file open means "watching a video".
- **Same eboot [read]:** seth's tested SHA-256 `4e335c5b…` (`seth:scripts/bug_report.py:33`) equals our dump's `eboot.bin`, so the addresses apply as-is.
- **Ours:** launcher Discord, opt-in with the player's own app ID, with the level scraped from the game log (`lbp3-recompiled:runtime/launcher/launcher_main.cpp:241-285`).
- **Port as:** a `[place]` log line on change, which the launcher already reads.
- **Risk:** read-only reads; they must never write guest state.

**Add to Steam (item 8): portable with rework, M; low**
- **Seth:** writes a non-Steam shortcut into `shortcuts.vdf` with art cut from the player's own game pictures. It closes and reopens Steam, and keeps one backup of the list (`seth:launcher/native/lbp3_launcher.cpp:1040-1313`, GPL-2.0-or-later, SDL3 + ImGui like ours).
- **Ours:** desktop shortcuts. (`notes:spec.md` puts Linux and the Steam Deck out of scope; this is Windows Steam.)

**Third-party XML patches (item 9): portable with rework, M; lockstep risk high**
- **Seth:** shadPS4/GoldHEN XML patches from `data/patches`, including patch repositories with `files.json`. Literal and mask lines; code caves (`mask_jump32`) refused; the eboot SHA-256 checked (`seth:scripts/patches.py:1-10`).
- **Ours:** none.
- **Online:** any patch that changes the simulation must be identical on both players. Put a hash of the active set in the L3P1 HELLO and refuse a mismatch, with no fallback (the same-version rule). Only if the user wants community patches.

**Not worth it**
- Launcher translations (`seth:launcher/lbp3_lang.py`).
- Separate "Standard" and "Online" builds (`seth:src/runtime_online.c:20-21`): ours is opt-in at run time and off by default.
- Run-time Python preparation (`prepare.py`, `link_*.py`, `content_profile.py`): ours does this in C++ at load.
- The GoW3 rename shim (`lbp3_compat.py`, `runtime_env_compat.c`).

**Audio: not worth it**
- Seth paces `sceAudioOutOutput` on a steady clock and claims that fixes "a click every 10.7 ms" (`seth:src/runtime_audio.c:1-8`, seth's claim). It keeps SDL's quiet 7.1-to-stereo mix.
- Ours has the downmix and limiter (ticket 16), and the user heard no crackles.

**Dialogs: not applicable**
- Seth's common dialogs "complete immediately (no UI is drawn yet)" (`seth:src/runtime_services.c:1-6`). Ours draws them in ImGui.

**Other seth-only runtime files (one line each)**
- **`runtime_fiber.c`** (Windows notes read, `seth:src/runtime_fiber.c:16-45`): swaps the TIB's `StackBase`/`StackLimit` **and** the TEB's `DeallocationStack` (gs:[0x1478]) inside the switch's assembly. It warns that the exe must not be CET-compatible.
  - Ours swaps only `StackBase`/`StackLimit`, from C++ around the switch (`lbp3-recompiled:runtime/src/hle/fiber.cpp:215-227`, `:400-402`, `:416`). Our CMake doesn't set `/CETCOMPAT` **[read]**.
  - `DeallocationStack` feeds `GetCurrentThreadStackLimits` and stack-overflow handling **[recalled, not verified]**.
  - No crash on a fiber stack has been seen in ours. **Not worth it now**; revisit if a host-library exception on a fiber stack ever kills the game.
- **`runtime_sampler.c`** (header only): `LBP3_SAMPLE` suspends every thread and prints RIP and frame chains. **Already have** (`lbp3-recompiled:runtime/src/core/runner.cpp:703-706`).
- **`runtime_lbp3_probe.c`** (header only): a memory-diff probe for their 60 fps extra frame. **Not applicable** here (graphics lane).
- **`record_pad.h`** (header only): pad input logged beside their video recorder. **Not applicable** (input/recording lane).
- **`runtime_offline.c`** (header and NP/socket part read): signed-out, fail-fast answers when offline. **Already have** the equivalent (`np_offline.cpp`, `net_offline.cpp`, ticket 03's verdicts).
- **`runtime_evf.c`** (header and F74 read): event flags and POSIX semaphores, FIFO waiters, `LBP3_SEM_STATS` wait diagnostics. **Already have** (`kernel_sync.cpp`).

## 8. Facts worth keeping (same eboot, 1.28)

- CGame pointer at `eboot+0x12ecc80`. Fields as in §7: +0x2f8 slot type, +0x308/+0x318/+0x328 previous and primary slot tags, +0x4f5 is-Pod, +0x108 play/edit mode, +0x398 level name, +0x3c0/+0x3c8 players and count. The CGame reflection table is at 0xc58180 (`seth:src/runtime_lbp3_60fps.c:12211-12216`, `:12292`, `:12318`).
- `InputManager::GetNumPredictedFrames` at 0x35da40, and an "is online" routine at 0x254270 (`:137`, `:143`). These may matter for netcode work; unverified by us.
- **Brightness (ticket 17), video-out gamma:**
  - `sceVideoOutColorSettingsSetGamma_` and `sceVideoOutAdjustColor_` are missing in ours (`lbp3-recompiled:analysis/remote_stdout.txt:32`, 2026-11-18) **[read]**.
  - At default settings they are never called **[read]**: no stub call in `analysis/play_log*.txt`, nor in `D:\Recomps\lbp3-dist\test-v012\report\*_stderr.txt`, whose fresh profiles pass the brightness screen at its default.
  - So they don't cause the over-brightness at defaults.
  - **Untested:** moving the brightness slider (calibration screen or Options), and which code calls the imports (a cross-reference with `tools/find_refs.py` would say).
  - If the slider calls `AdjustColor`, the setting does nothing in ours today, and seth's handling is the fix: shadPS4's, feeding the presenter's gamma (`seth:gpu/shadps4/core/libraries/videoout/video_out.cpp:407-426`). That would be S–M, and it would be a player-visible gap **[inferred]**.

## Licence

- Seth's repository is GPL-2.0-or-later (`seth:README.md:112`). Scripts and the native launcher carry SPDX lines for it, and the vendored shadPS4 stays GPL-2.0-or-later.
- Their release packages are GPL-3.0 because of FFmpeg (seth's claim).
- Our runtime is GPL-3.0-or-later, so any of it can be used with the notices kept and sethlikepie and Emre_exe credited.
