# 60 fps experiment: forcing the flip rate

2026-11-12, measurement only. Branch `exp/flip60` (runtime `3ddb0a1` on `port` `0570df8`, worktree `D:\Recomps\lbp3-wt\fps60`, local, not pushed) adds `LBP3_FLIP_RATE=0|1|2`: every `sceVideoOutSetFlipRate` call is overridden and logged with the rate the game asked for. Same exe for every arm; only the env differs. All runs on the 165 Hz display ("DQI27165", 165.01 Hz), with `LBP3_TRACE_SIM=1 LBP3_FRAME_SPLIT=1 LBP3_PACE_LOG=1`.

Labels: **(M)** measured in these runs, **(I)** inferred.

## Answer

- **(M) The simulation takes one fixed step per flip at 20, 30 and 60 Hz.** Steps per wall-clock second follow the flip rate: 20.0, 30.0 and 57–60. A whole cutscene takes the same number of steps at 30 and at 60 Hz (4,295 / 4,295 against 4,330 / 4,325).
- **(M) So forcing 60 Hz runs the game at double speed.** Frames are not duplicated or interpolated. The Legend of Bunkum plays in 55–56 s instead of 108 s. The same scripted Introduction walk reaches Newton in 23 s instead of 52 s, and the story save in 64–65 s instead of 128 s. The camera moves the same ~2.0 px per presented frame at both rates, and in 240 frames at 60 Hz no two consecutive frames were identical.
- **(M) Audio does not speed up.** At 60 Hz, 12 of the 15 Bunkum voice lines start before the previous line's file is closed; at 30 Hz, none do. **(I)** The speech plays at normal speed over a cutscene running at double speed, so lines overlap or cut each other off.
- **Correction to my earlier answer.** "Each frame would just show twice" is **wrong**. Tickets 06/09 pointed the right way: the game runs faster, not smoother. The conclusion still holds: raising the cap alone doesn't give a playable 60.
- **(M) No breakage at 60 besides the speed.** There were no crashes, GPU warnings, stuck waits, early releases, dropped draws or `nvlddmkm` events. The game itself asks for 60 Hz during bring-up and switches to 30 Hz before the menus.
- **(M) Frame budget at 60.** Light and medium scenes need 13–16 ms of work against 16.7 ms. The runs averaged 57–58 flips/s, with 6–19% of frames late, in the cutscene and the Introduction. The heavy Needlepoint walk wasn't reached at 60 (see 4). **(I)** It would be about 15–16 ms, at or over the budget.

## 1. What the game asks for (M)

Every run logs the same five calls: `sceVideoOutSetFlipRate` asks for 60 Hz twice at bring-up (`bringup` thread, before the Spu threads), then for 30 Hz three times (before the input config and voice setup). Earlier logs on `port` show the same sequence (`analysis\rtfr_bunkum_stderr.txt`, lines 39, 54, 203, 400, 402). So 30 Hz is the game's own choice for everything after bring-up.

## 2. Sim steps per second and per flip (M)

`[sim]` lines (`GetSimFrame__` per flip) against wall time (the sum of "ms between submits"). Scored by `D:\Recomps\lbp3-wt\_tools\fps60_score.py sim`.

| Scene | 20 Hz | 30 Hz | 60 Hz |
|---|---|---|---|
| Pod, idle (last part of a run) | 20.01 steps/s, 100% one step a flip (93 s) | 30.00 (16 s) | 59.56 (17 s) |
| Bunkum cutscene, line 1 to the Needlepoint load | not reached (see 6) | 29.75 / 29.76 | 58.00 / 57.35 |
| Introduction walk, start to Newton's line | — | 29.98 / 30.00 | 58.23 / 57.34 |

**Steps per flip:**
- **Bunkum cutscene:** at 30 Hz, 99.2% / 99.3% of flips take one step. At 60 Hz, 87.9% / 89.8% take one, 6.6% / 5.6% take none and 5.5% / 4.5% take two (patterns like `1021102111`).
- **Introduction walk:** 99.3–99.9% of flips take one step at both rates.
- **Whole cutscene:** 4,295 steps in each 30 Hz run; 4,330 / 4,325 at 60 Hz. The cutscene's length is counted in steps, not seconds.

## 3. Real game speed, without step counts (M)

**The Legend of Bunkum**, from the stamped log (`intro_drive.py stamp`, QPC time):

| | 30 Hz (`f60b1a`, `f60b1b`) | 60 Hz (`f60b0a`, `f60b0b`) | Ratio |
|---|---|---|---|
| First to last voice line opened | 107.92 / 107.91 s | 55.05 / 55.73 s | 1.95 |
| First line to the Needlepoint load | 122.55 / 122.51 s | 62.65 / 63.32 s | 1.95 |
| Gap from line 10 to line 20 | 9.01 s | 4.51 s | |
| Line 10's file open to close | 7.33 s | 9.21 s | |
| Lines opened before the previous one closed | 0 of 15 / 0 of 15 | 12 of 15 / 12 of 15 | |

All 16 voice lines played and Needlepoint loaded in every 30 and 60 Hz run. **(I)** The file's close marks the end of the line's playback (the stream is read as it plays); that is how "overlap" is read here.

**The Introduction walk.** `intro_walk.sh` gives the same wall-clock input in both arms: move right, with a jump every 3.6 s.

| Event, seconds after the walk starts | 30 Hz (`f60w1a`, `f60w1b`) | 60 Hz (`f60w0a`, `f60w0b`) |
|---|---|---|
| Tutorial video (`video_costumes_narrator.bik`) | 27.71 / 27.62 | 10.02 / 10.08 |
| Newton's line (`np_tu01b_01_newton.mp3`) | 52.25 / 52.22 | 23.23 / 23.56 |
| Story save (`playfart_12_349_16`) | 128.32 / 128.31 | 64.32 / 65.15 |

**The camera.** These are frames recorded at the Introduction spawn while walking right (`pace_run_intro.ps1`'s `camera_check.py 240`), re-scored by `D:\Recomps\lbp3-wt\_tools\fps60_cam.py`:

| | Hand-over interval | Median scroll, moving frames | Identical consecutive frames |
|---|---|---|---|
| 30 Hz (`f60c1a`) | 33.33 ms | 2.04 px a frame | 0 of 239 |
| 60 Hz (`f60c0a`) | 19.25 ms (the frame capture slows it) | 2.03 px a frame | 0 of 239 |

Each presented frame moves the camera by one full step's worth, so the speed per second follows the frame rate.

## 4. Frame budget at 60 (M)

The frame-split windows come from the stamped logs (`fps60_score.py window` plus `tools/frame_split.py`). Work = flip-to-flip minus the slot wait.

| Scene | 30 Hz work | 60 Hz work | 60 Hz period: mean, p95, late frames |
|---|---|---|---|
| Bunkum cutscene | 17.6 ms (`f60b1a`) | 14.4 / 14.7 ms | 17.00 / 17.17 ms; p95 18.5 / 19.6; 6.3% / 10.7% over 18 ms |
| Introduction walk | 17.6 / 18.5 ms | 15.8 / 16.3 ms | 17.18 / 17.49 ms; p95 19.3 / 23.7; 12.4% / 18.6% over 18 ms |
| Needlepoint, standing at the spawn (14–27 s after the level opens; from `[video]` lines) | 15.78 ms (`f60r1a`) | 12.83 / 12.89 ms | 16.70 ms |
| Needlepoint, heavy walk to the basket | 18.99 ms (`f60r1a`) | not reached | — |

- **(M) Where the frame time goes in the Introduction walk:**
  - The CPU phases are the same per frame at both rates: main thread 4.75 / 5.01 ms at 60 against 4.67 / 4.77 ms at 30; patch and copy 7.4–7.6 ms at both.
  - Only the GPU phase is shorter at 60: 2.46 / 2.62 ms against 4.45 / 5.20 ms.
  - **(I)** The GPU keeps higher clocks when it is busy twice as often.
- **(M) The heavy walk wasn't measured at 60.** The `m5_np_route.sh` walk ended short of the basket in both 60 Hz runs (screenshot `analysis\screenshots\m2_m5_f60r0a_heavy.png`, at the ice bucket, against `..._f60r1a_heavy.png` at the basket). At double speed, the wall-clock jumps land in other places.
- **(I) Heavy walk estimate:** about 15.6 ms at 60, from 18.99 ms × the 0.82 spawn ratio. That is at or over the 16.7 ms budget.
- **(M) Pacing:** 60 Hz on the 165 Hz display uses the timer grid every flip (165/60 isn't a whole number); in the Pod it holds 59.6 flips/s. **(I)** Without VRR, a 165 Hz display can't show 60 fps evenly (2.75 refreshes a frame). The 240 Hz display's vblank path (4:1) wasn't tested.

## 5. What breaks at 60 (M)

Counted in the four 60 Hz cutscene and walk logs (`f60b0a`, `f60b0b`, `f60w0a`, `f60w0b`), plus the two routes and the camera run:
- **Runtime:** no crash, `stuck` wait, `device lost` or `Rejecting invalid` lines; no early releases in any `[video]` window; 0 dropped draws.
- **System log:** 0 `nvlddmkm` events from 10:08 to 10:54.
- **Game:** the story save was written, and the cutscene and level loads complete.
- **What's wrong:** everything runs at double speed, and the voice lines overlap (3 above).
- **0/2 steps:** in the cutscene at 60 they rose to 10–12% of flips, from 0.7% at 30 (2 above), so the camera snaps the frame-pacing fix removed would partly return at 60. **(I)**

## 6. 20 Hz (M)

`f60b2a` (`LBP3_FLIP_RATE=2`) never left the Pod. Its menu presses are timed for 30 Hz and came before the slower game was ready (`analysis\screenshots\f60b2a_end.png`: the network message still open). Over the last 93 s in the Pod it took exactly one step per flip at 20.0 flips/s. **(I)** At 20 Hz the game runs at two-thirds speed, so speed follows the flip rate both ways.

## What this means for a real 60 (I)

- **The game has no variable time step to turn up.** At every rate, the step count follows the flip count. Cutscenes, at least, are timed in steps (the same ~4,300 steps at both rates).
- **A native 60 means one of these:**
  - **Interpolated drawing** between 30 Hz steps, which is new code in the game's renderer.
  - **Half-length steps.** That means finding every per-step constant (physics, animation, logic timers, cutscene step counts) so that 60 steps take one second.
  - Both are reverse-engineering projects with no source. The speed and audio results above show how much depends on the step.
- **Frame generation on top of the 30 Hz game** needs none of that. The 30 Hz budget has room (heavy walk 19 ms of work against 33.3 ms).

## Files

- **Logs:** `D:\Recomps\lbp3-recompiled\analysis\fps60\` (`f60b*`, `f60c*`, `f60r*`: stderr, stamped logs, `frame_split` and pace CSVs, camera frames) and `analysis\intro_stalls\f60w*`. The run log is `analysis\fps60\summary.txt`.
- **Scripts:** `D:\Recomps\lbp3-wt\_tools\fps60_runs.sh` (the exe swap with a restore trap; runs Bunkum, the walk, the route and the camera check), `fps60_score.py` (sim / voice / newton / window) and `fps60_cam.py`.
- **State after the runs:** the clone is on `port` with its own exe (md5 `0dfda1b9…`, checked at restore). No game, marker or kept copy is left. The human's saves are identical to `analysis\savedata_backups\human_play_20261111_2104`.
