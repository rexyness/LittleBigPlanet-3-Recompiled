# Why doesn't the 30 fps cap hold on this PC?

Type: task
Status: resolved
Blocked by: none
Map: [LBP3 native port](../map.md)

## Question

On this PC the game runs too fast. Frames come every 13–26 ms instead of 33.3 ms: the human's first session averaged 13.3–14 ms a frame for minutes at a time (`D:\Recomps\lbp3-recompiled\analysis\play_log.1.txt`), and the human said "game seem too fast". Upstream reports a locked 33.3 ms on a 144 Hz monitor (`NOTES.md:421-425`). What breaks the pacing here, what change holds 33.3 ms on this PC, and how big is that change? Is upstream's camera snapping (`NOTES.md:622-650`, `:661-662`) the same problem?

The milestone ladder needs the answer because game speed underlies every other check: physics, cutscenes and the Needlepoint Peaks launch all depend on it.

## Leads

- `WaitForFlipSlot` (`runtime/src/video/presenter.cpp:124-138`) holds each flip for `round(refresh / 30)` presents. Each present counts as a vblank once `SDL_RenderPresent` returns (`presenter.cpp:113-118`). The baseline logs show `display refresh 240.00 Hz` (read from the window's own display, `presenter.cpp:59-61`), which gives 8 presents per flip, and `window renderer: direct3d11, vsync on (1)`. A 13.3 ms frame therefore means 8 presents finished in 13.3 ms, about 600 a second. Vsync is requested and reported on, but presents are **not** held to the refresh rate. Suspects:
  - a driver-level vsync override, such as "Vertical sync: Off" in the NVIDIA Control Panel, which turns `Present(1)` into `Present(0)`
  - G-Sync in windowed mode
  - DXGI letting several frames queue up
  - the Meta and SudoMaker virtual displays
- When pacing by presents fails, `WaitForFlipSlot` returns false and the caller falls back to a timer. Check that fallback too.
- SDL rounds the refresh rate to 240.00 Hz, but Windows reports the display as 239 Hz. Even with presents held to vsync, 7.97 presents per flip would make a grid of 8 presents drift slowly.
- Pacing history: `NOTES.md:622-650` (fixed grid, sleep then spin, the flip-anchored game clock).

## Checklist

1. **AFK.** Log per flip: the QueryPerformanceCounter time, the present count, the measured refresh (`g_refreshHz`) and SDL's renderer name and vsync state. Run the scripted Introduction walk (upstream's remote input) with `LBP3_TRACE_SIM=1` to count sim steps per flip.
2. **AFK.** On a throwaway branch, try one variant at a time: pace flips on a QueryPerformanceCounter grid of 33.33 ms and ignore present counts; force vsync or FIFO presentation; run windowed and fullscreen. For each variant, record frame intervals, sim steps per flip and the camera scroll per frame (`tools/camera_check.py`).
3. **HITL, only if step 2 points at the displays.** The agent writes exact steps for one run with G-Sync off or the virtual displays disconnected, the human carries them out, and the agent runs the variants again.
4. **HITL.** The human plays the Introduction for two minutes on the best variant, then says in their own words whether the speed and the camera feel right.

Keep logs out of git. The game dump stays read-only. The agent's shell sets `NoDefaultCurrentDirectoryInExePath=1`, so start the build with `cmd /d /c .\build.bat` from `D:\Recomps\lbp3-recompiled`. A bare `build.bat` is "not recognized". Auto mode allows the build and runs in that clone.

## Done when

An `## Answer` gives the cause, the change that holds 33.3 ms on this PC (a diff on a throwaway branch, not merged), whether camera snapping goes away with it, and an estimate in sessions for the milestone ladder.

## Answer

Worked on 2026-11-10. Steps 1, 2 and 4 are done; step 3 wasn't needed. The feel test is recorded at the end.

**Cause.**

- **Proven: presents don't block.** With vsync requested and reported on, `SDL_RenderPresent` returns far faster than the 240 Hz refresh:

  | Renderer | Presents per second |
  |---|---|
  | D3D11 windowed | about 3,700 |
  | D3D11 fullscreen | about 3,000 |
  | SDL's GPU renderer | about 2,500 |
  | D3D12 | about 2,200 |
  | Vulkan | 430, in bursts (median interval 0.08 ms) |
  | OpenGL | 240 |

  `WaitForFlipSlot` counts each returned present as a vblank, so the 8 "vblanks" per flip pass in about 2 ms and never hold a frame back. Frames then run as fast as the work allows. In today's runs that was 26–28 ms in the Introduction; the baseline session's 13 ms frames weren't reproduced (the Pod and menus took 18–20 ms of work today). The sim does exactly 1 step per flip, so the game runs too fast: 1.25× at 26.6 ms.
  - A standalone probe (SDL3 3.4.16, D3D11 renderer, vsync on, like the presenter) reproduces this outside the game. `DwmFlush` and `IDXGIOutput::WaitForVBlank` both tick at a true 240.0/s at the same time.
  - In-game, upstream's unchanged pacing on the OpenGL renderer (`SDL_RENDER_DRIVER=opengl`, which honours vsync here) gives 240 presents/s and 33.3 ms frames. That makes non-blocking presents the mechanism.
- **Best-supported explanation: the driver setting.** The NVIDIA driver's global profile ("Base Profile") on this PC has **Vertical Sync = Force off** (`VSYNCMODE` `0x08416747`, read through NVAPI DRS, read-only). That is not the driver default ("use the 3D application setting"). The setting was not toggled to confirm it, because changing the user's driver profile is out of bounds and the fix doesn't depend on it.
- **Ruled out:**
  - Injected overlays: no non-system module was loaded in the probe.
  - Refresh drift: the display runs at 239.999 Hz (SDL's rational is 239999/1000), so an 8-vblank grid doesn't drift.
- **Not determined:** G-Sync and the frame-rate limiter. Their setting IDs came back "not found" without a name, which is also what a wrong ID would return. Neither matters to the fix.
- **This PC now:** two physical displays, `DQO27360` 2560×1440 at 239.999 Hz (primary) and `DQI27165` at 165.007 Hz to its left. The Meta and SudoMaker virtual displays didn't enumerate today. Whether they were active during the baseline is unknown, and it doesn't matter to the fix.

**The change** (throwaway branch `local/frame-pacing`, commits `7ec7ab7` and a comment fix on top of `ba83dcb`, not merged; also saved as [patches/06-frame-pacing.patch](../patches/06-frame-pacing.patch), about 230 changed lines in `presenter.cpp/.h`, `videoout.cpp` and `CMakeLists.txt`):

- **Vblank source.** A vblank thread waits on the DXGI output the window is on (`WaitForVBlank`), which the driver override doesn't affect, and counts real vblanks. It follows the window between monitors.
- **Presenter.** It presents once per real vblank, so it no longer spins at 3,400 presents a second.
- **Game clock.** Flips report the vblank's ideal time (anchor + index × refresh period) to the game's frame clock, not the wake-up time.
- **Refresh rate.** Uses SDL's exact rational.
- **`LBP3_PACING`:** `vblank` (default), `present` (upstream), `timer` (the 60 Hz grid only).
- **Instrumentation:** work and slot-wait times in the `[video]` line, a `[pace]` line every 5 s, and `LBP3_PACE_LOG=1` for per-flip CSV.

**Measurements** (tools: [vsync_probe.cpp, nvdrs_read.cpp, pace_run.ps1](../tools/); scripted run: Pod → Introduction, then `tools/camera_check.py 240` while walking; sim steps over the last 40 s, 1,200 flips; logs in `D:\Recomps\lbp3-recompiled\analysis\pace_*`, not in git):

| Pacing | Presents/s | Flip interval | Work / slot wait | Sim steps ≠ 1 (last 40 s) | Camera hitches (240-frame walk) |
|---|---|---|---|---|---|
| `present`, D3D11 (upstream on this PC) | 3,400 | 26.6 ms (sd 3.6) | 27 / 0 ms | 0.8% (game too fast) | 0 |
| `present`, OpenGL (vsync honoured) | 240 | 33.35 ms | 23 / 10 ms | 13.8% | 16 |
| `timer` (upstream's fallback grid) | 240 | 33.33 ms | 25.5 / 7.8 ms | 28.9% | 19 |
| `vblank`, flip time = wake-up time | 240 | 33.35 ms (sd 0.36) | 26 / 7 ms | 43.0% | 7 |
| **`vblank`, exact flip times** (two runs) | **240** | **33.33 ms (sd 0.01)** | **25 / 8.3 ms** | **7.4%, 18.3%** | **17, 12** |
| `vblank`, exact, OpenGL | 240 | 33.33 ms | 24 / 9.3 ms | 9.2% | 14 |
| `vblank`, exact, `LBP3_FRAME_CLOCK=off` | 240 | 33.33 ms | 25 / 8.5 ms | 24.1% | 8 |

The two snapping measures don't agree. They also cover different moments: the step count covers the last 40 s, which in some runs was mostly standing still after the walk, while the hitch count covers the walk alone. Read the last two columns as noise around a persistent problem, not as a ranking of the variants.

- **Windowed vs fullscreen:** the probe shows fullscreen D3D11 ignores vsync under the override too. The new vblank source doesn't depend on the window mode, so the game wasn't rerun fullscreen.
- **Moving the window** to the 165 Hz display mid-game: the vblank source follows it. 165/30 isn't a whole number, so flips fall back to the timer grid and still hold 33.3 ms. Moving back resumes vblank pacing. The move itself caused one blip of five frames over 40 ms (worst 48.5 ms).
- **Headroom:** the Introduction's frame work is 24–27 ms, which leaves 6–9 ms under 33.3 ms. The Pod and menus take 18–20 ms. Heavier scenes are unmeasured.

**Camera snapping is a separate problem.** Upstream's camera snapping was measured on a 144 Hz monitor, which already takes the timer path, so it isn't this bug. Every variant that paces correctly still runs 0 or 2 sim steps in 7–43% of flips, in bursts like `120210…`. The camera check shows 7–19 hitches per 240-frame walk, each a double step followed by near-zero steps. No pacing variant is shown to help: two runs of the same variant differ as much as the variants do, and the two measures rank the variants differently. Only the too-fast unpaced mode gets close to all 1s. The exact flip times stay in the fix because a console's flips land exactly on vblanks, not because they measurably reduce the snapping. The steps are game-side: [Why does the sim run 0 or 2 steps per frame at a steady 33.3 ms?](09-sim-step-bursts.md).

**Estimate for the ladder:** landing this fix in the fork takes **half a session**: keep `vblank` as the default with the `present`/`timer` fallbacks, keep or trim the instrumentation, and rerun this table. The snapping fix is unknown until [09](09-sim-step-bursts.md) is answered; my guess is 1–3 sessions of reverse engineering.

**Left for step 4 (HITL):** `build\lbp3.exe` is already built from `local/frame-pacing`. The human starts it from `D:\Recomps\lbp3-recompiled` with no environment variables (`vblank` is the default) and the log captured:

```powershell
Start-Process .\build\lbp3.exe -ArgumentList '--run','--skip-intro' -RedirectStandardError analysis\feel_stderr.txt -RedirectStandardOutput analysis\feel_stdout.txt
```

They play the Introduction for two minutes, with a controller if they like, and say in their own words whether the speed and the camera feel right. Expect correct speed with occasional camera snaps. Leave the NVIDIA setting as it is; the port has to work with it. Afterwards the agent checks `feel_stderr.txt` for 33.3 ms flips and the count of timer-paced flips. That also shows whether the baseline's 13 ms scenes now hold 33.3 ms.

**Step 4 result (2026-11-10).** The human played about 5.5 minutes on `local/frame-pacing` (`dfd6828`) with no environment variables and a controller. In their words: "game speed feels normal now, and i didn't see anything wrong with the camera". The log is `analysis\feel_stderr.txt`, not in git.

- **Pacing:** every flip was paced by vblanks (0 on the timer), and presents ran at 240/s. Play averaged 29.0 fps including hitches, against 37+ before the fix.
- **Hitches**, for the gap list's hitches row (gap 3 in the baseline):
  - 1.6% of play frames took over 40 ms, and 32 took over 100 ms (worst 1.0 s and 3.0 s at loads).
  - Most coincide with new images in the texture cache, but 16 half-second windows had a slow frame with no new image.
  - In 14 of the 326 windows the average frame took more than 33.3 ms (up to 45 ms), so parts of the Introduction run short of 30 fps on this PC.
  - The human didn't comment on any of it.
- **Camera:** the snapping measured in [09](09-sim-step-bursts.md) wasn't noticed in normal play.
