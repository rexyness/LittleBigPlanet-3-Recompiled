# Boot loading screen and the cache read-ahead (2026-11-14)

The user: "for the shaders, instead of a black screen for 40 secs or so you introduce a loading screen or something".

## What was black, and for how long

The game window opens in `sceVideoOutOpen` (`hle/videoout.cpp`), which then runs `gpu::Init` synchronously: the Vulkan device, the driver's pipeline cache file (45 MB), and the shadPS4 pipeline cache's `WarmUp`, which reads the shader cache one file at a time (~7,000 keys, ~5,000 metas, ~5,000 SPIR-V files in `user\shader_cache_best\CUSA00473`). The window shows black for all of it, then ~1 s more until the game's first non-black frame (its "Important Information" screen).

- Warm start: preload 5.3-5.9 s, first picture at 6.4-7.0 s.
- Cold start (first after a PC restart): 25-40 s (user reports and an earlier measurement).

## The loading screen (runtime `b868f84`, shadPS4 `85d68e5b`)

- `ui/boot_overlay.*`: an overlay layer on ImGui's background list, under the keyboard, dialogs and menu; no input.
- **Look:** like a PS4 starting a game. The dump's own `sce_sys/pic1.png` fills the window (cropped, not stretched), with a dark band at the bottom, the `icon0.png` icon, "LittleBigPlanet 3", a status line and a progress bar. Without the art: a plain dark-blue gradient. Nothing from the game is shipped; the files are read from the user's dump at run time.
- **Status:** "Starting", "Starting the graphics device", "Loading the graphics driver's cache", "Loading shaders 3,296 / 7,022 46%", "Preparing shaders" (the parallel builds), "Starting LittleBigPlanet 3". After 12 s of reading, a hint: "The first start after the PC restarts takes longer while Windows reads the shader cache."
- **Progress source:** shadPS4 `common/lbp3_boot.h` (phase, done, total; atomics); the total is a listing of the cache folder (`DataBase::CountBlobs`), no reads.
- **Hand-over:** at the first frame that isn't black (a 32x18 sample grid, any channel > 16), or 60 s after the game's first frame at the latest; 0.35 s fade; never comes back.
- **Gating:** only the game shows it (`main.cpp` calls `SetArt`); the test programs sharing the overlay host don't. `LBP3_BOOT_SCREEN=0`: the old black window. On by default in the runtime (it only exists before the game's first picture; the regression passes with it on).
- **Test aid:** `LBP3_PRELOAD_DELAY_US=<n>` waits n µs per cached pipeline (stands in for a slow start).

## The read-ahead (runtime `225aa4d`, shadPS4 `fb501343`)

Offline (Python): one pass over a freshly written copy of the cache took 94.6 s one file at a time and 8.3 s on 16 threads; a second pass 2.5 s / 1.4 s. Files written by `cp -r` opened at warm speed in the game, files written by Python's `copytree` stayed slow on first open (83 s in Python, in either folder), so the A/B uses `copytree` copies.

`WarmUp` times its reads every 128 pipelines; slower than 2 ms each, it starts `DataBase::StartPrefetch`: every file of the folder cache read once on cores − 1 threads (max 16), bytes thrown away, best effort (no asserts). Stopped and joined when the serial loop ends, before the pipeline builds. Always-on cost warm starts 0.55 s, hence the trigger. `LBP3_PRELOAD_PREFETCH=0` never, `=1` from the start. Log: `[gpu] pipeline preload: slow reads, the cache was read ahead on 16 threads from pipeline 128 on`.

| Start (1440p, 7,022 pipelines) | Read-ahead off | Adaptive (default) |
|---|---|---|
| Cold (fresh `copytree` copy) | reading 75.58 s | reading 11.83 s (started at pipeline 128) |
| Warm | 5.26, 5.36 s | 5.34, 5.32 s (never started) |

A real post-reboot start wasn't measured (no admin to empty the standby list); the cold copies are a stand-in.

## Tests

- `D:\Recomps\lbp3-wt\_tools\boot_screen_test.sh` (runs A slow preload with screenshots, B normal, C off; `RUNS=`), `boot_launch.ps1` (starts a given exe from the clone), `prefetch_ab.sh`, `boot_regress.sh` (the clone's exe swapped and restored by md5).
- Screens: `analysis\screenshots\boot_b1_*.png`, `boot_b2_*.png` (A at 2-26 s, the hand-over, the game after).
- **Regression** (`boot_regress.sh bootrg`, defaults, best-cache copy, test saves): keyboard, photo, backup dialogs, co-op PASS; Introduction 0.00% / 0.13%, Thruster 3/3, Bunkum 16/16 with Needlepoint, sky cyan; the screen came up and went off at 6.6-7.0 s in all 12 boots; no nvlddmkm/Display events; clone exe restored (`95d8af03…`).

## Landed

Fork `main` = `225aa4d`, shadPS4 fork `lbp3` = `fb501343` (fast-forwards, 2026-11-14 ~03:45). The user's `D:\Recomps\lbp3-wt\best` (on `main`) rebuilt; the previous exe is `best\build\lbp3.exe.pre_bootscreen_20261114_0345`.

Then merged with `feat/resolutions` (perf/resolutions.md): fork `main` = `3b0b958`, shadPS4 `lbp3` = `54abc447` (~04:45). The merged build passed 1440p, 4K and 3440x1440 in game and the full regression at the defaults (`boot_regress.sh mergerg`: keyboard, photo, backup, co-op, Introduction 0.00/0.13%, Thruster 3/3, Bunkum 16/16, sky cyan, no driver events). `best` rebuilt on it; the launcher the user clicks (`lbp3-wt\release\build\LBP3Launcher.exe`) replaced by best's (resolution dropdown), the old one kept as `LBP3Launcher.exe.pre_res_20261114_0447`.

Note: a save snapshot taken while another lane had test saves installed captured the test baseline; it was renamed `analysis\savedata_backups\testbaseline_not_human_20261114_0208`. Take snapshots only while the game lock is free and no marker is set.
