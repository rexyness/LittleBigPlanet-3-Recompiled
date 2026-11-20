# Streaked photos and costume icons above 1080p (fix/capture-pitch)

2026-11-14. The user, on the best build at 2560x1440: "if i save a costume it would have a wacky image. same thing with taking a picture". Runtime `fix/capture-pitch` `9d3404c`, fast-forwarded into fork `main` and built into `lbp3-wt\best\build\lbp3.exe` (old exe kept as `lbp3.exe.pre_capfix_20261114_1300`). shadPS4 unchanged (`9423d341`).

## What the user saw

- In the Pod's Costumes page the costume they had just saved ("Arap") showed a green tile crossed by slanted chrome bars instead of a Sackboy portrait (a grab of their running game).
- Their profile (`bigfart45`) holds 15 Popit photos. Six (#3, 5, 6, 8, 10, 11) are horizontal streaks; the rest are normal pictures.

## Cause

The bloom chain (render-target table entries 0x26/0x27/0x28, 1280x720 / 640x360 / 320x180 at 1080p) is also the capture chain: photos, saved-costume icons, level icons and the profile picture `ICON0.PNG` are drawn into it and copied out by the CPU four frames later at `0x4014f0` (m2/photo.md). That copy takes a centred photo- or icon-sized cut and steps rows by **the table's width x 4**.

The `bloom` group of the resolution patches (graphics lane `c3242e5`, best/README.md; perf/best_gfx.md section 1) scaled the chain at 1440p to 1707x960, 853x480, 427x240. The GPU pads a linear 32-bit target's rows to 64 bytes, so the targets were laid out with pitches 1712 / 864 / 432 while the game read 1707 / 853 / 427 pixels a row. Every row starts 11 (or 5) pixels further along than the game thinks, so vertical edges turn into near-horizontal streaks.

Evidence:
- **Un-shearing.** Photo #10 rebuilt with the game's 853-pixel stride against candidate GPU pitches (scratch `deshear.py`): only 864 gives a picture (the user's Sackboy in the Arap robe, in the Pod by the controller). `ICON0.PNG` (from the 427-wide target, swap-alpha undone) un-shears only at 432. Both fit 64-byte row alignment, not 32-pixel (448) or 8-pixel (856).
- **Not intermittent.** Each photo's byte size matched to the `[jpeg] encoded ... -> N bytes` lines of the archived play logs: all six streaked photos come from sessions with `[res] ... bloom ... -> 853` (11/13 09:06, 11:22, 14:21; 11/14 00:55, 11/14 midday); all the good ones from sessions without the bloom patch (11/11, and 11/12 at 1080p and at 1440p before the bloom lane).
- **Also cropped.** The capture fills the whole (bigger) target and the game still cuts a centred 640x360, so even un-sheared a 1440p photo showed only the middle 3/4 of the on-screen photo frame (baseline run: Sackboy, at the bottom of the frame, cut off at the photo's bottom edge). Derived, not run on the old build: at 4K (a 2560x1440 chain, already aligned) photos would not be streaked but would show only the middle half.
- Derived, not run on the old build: the wide sizes' old chains were unaligned too (853 at 2560x1080, 1147 at 3440x1440), so their photos would have been streaked as well. resolutions.md's "they encode, save and show fine" checked the encoder line and the readback count, not the pixels.

## Fix (`9d3404c`)

`core/res_patches.cpp`: the bloom sites get their own kinds. Heights stay 720/360/180; widths are the 1080p ones at every 16:9 size and widen only with the aspect on wide screens (photos there are 360 x aspect wide), rounded up to 16 pixels:

| Size | Chain (was) | Chain (now) |
|---|---|---|
| 1280x720, 1920x1080 | 1280/640/320 | 1280/640/320 |
| 2560x1440 | 1707x960 / 853x480 / 427x240 | 1280x720 / 640x360 / 320x180 |
| 3840x2160 | 2560x1440 / 1280x720 / 640x360 | 1280x720 / 640x360 / 320x180 |
| 2560x1080 | 1707 / 853 / 427 wide | 1712 / 864 / 432 wide (heights 720/360/180) |
| 3440x1440 | 2293x960 / 1147x480 / 573x240 | 1728x720 / 864x360 / 432x180 |
| 3840x1600 | 2560x1067 / 1280x533 / 640x267 | 1728 / 864 / 432 wide |
| 3840x1080, 4480x1260 | 2560 / 1280 / 640 wide | 2560 / 1280 / 640 wide |

At 1440p the log now says `80 sites patched` (the bloom edits change nothing). The launcher's "Finer glow above 1080p" toggle (`bloom_1440` -> `LBP3_RES_SKIP=bloom`) is retired (an old `launcher.ini` line is ignored), and so is `play_best.bat`'s `BLOOM_1440` (a local, untracked file): at 16:9 it would change nothing, and on a wide screen skipping the group makes the copy read past the 640-wide target.

`restest` pins it: widths multiples of 16, 1080p heights, the 1080p chain at every 16:9 size, the half-size target at least 360 x aspect wide, the 4:2:1 steps within 16 pixels; 1440p equals the old table except the bloom rows, which are now no-ops. 2655 checks pass; `launchertest` 394.

## Verification

Script `lbp3-wt\_tools\capfix_lane.sh <tag> <WxH>` (res_lane.sh's route: Pod, Edit Now, Popit -> Take A Photo; test saves via saves.sh, the game lock, a shader-cache copy `analysis\capfix_cache`), now with a pixel check, `_tools\capfix_photo_check.py`: a streak score (mean |d/dx| / mean |d/dy|; the user's six streaked photos score 0.24-0.32, their nine good ones 0.75-1.06; minimum 0.5) for the photo and ICON0, and at 16:9 the grey correlation with the on-screen photo frame (minimum 0.8).

| Run | Exe | Photo | Frame correlation | ICON0 | Result |
|---|---|---|---|---|---|
| `base1440` | the user's best exe before the fix | 640x360, streak 0.36 | 0.28 (mean abs diff 41.0) | streak 0.39 | FAIL (reproduces) |
| `fix1440` | res worktree, `9d3404c` | streak 0.93 | 0.94 (12.5) | 0.97 | PASS |
| `fixuw` (3440x1440) | res worktree | 860x360, streak 0.88 | wide: by eye, matches its frame | 0.97 | PASS |
| `best1440` | rebuilt `best\build\lbp3.exe` | streak 0.93 | 0.94 (12.5) | 0.97 | PASS |

12.5 is in line with the original photo fix's 11.1 (m2/photo.md). Sheets: `analysis\screenshots\capfix_<tag>_check.png`.

- **Costume save** (`_tools\capfix_session.sh`, driven by hand, 2560x1440, fixed exe): Edit Now -> Popit -> Customize Character -> Save Costume. The new "A Costume" tile is a clean portrait, highlighted and in the list (`m2_capfix_cos_7save.png`, `m2_capfix_cos_8tile.png`); one capture, `3 read back`.
- **Glow at 1440p:** before vs after, mean abs difference 2.9 (Pod) and 1.2 (Create level) levels, the same as two runs of the same code (3.7, 1.1); luma p50/p90/p99 equal. As the bloom lane found, the scaled chain made no visible difference there. At 4K the scaled chain made the glow tighter than at 1080p (resolutions.md, the 1080p paragraph); with the 1080p chain 4K's glow goes back to the 1080p (PS4) proportions. Not looked at in game.
- **Not run:** the full regression (`m1_checks.sh` and the milestone 2 scripts) on `9d3404c`; the change only touches the six bloom sites, and at 1440p it is the chain every 1440p session used before 11/13.
- Every run: 0 GPU warnings, 0 crashes; the user's saves put back identical to `analysis\savedata_backups\human_play_20261114_1241_full` (taken after their session).

## Left over

- The six streaked photos and the "Arap" costume icon in the user's profile stay as they are (the shear dropped pixels; nothing to recover). Retake / re-save them.
- v0.1.3 friends' builds default to 2560x1440 and have the bug; a v0.1.4 release needs the user's OK.
- `best\build\LBP3Launcher.exe` (the one the user runs: its link failed with "permission denied" exactly while their launcher was open) was relinked once they closed it; old copy `LBP3Launcher.exe.pre_capfix_20261114_1300`. `--print-launch` with their settings: `best\build\lbp3.exe`, `LBP3_RES=1440`, `LBP3_RES_SHADERS=1`, frame generation, no `LBP3_RES_SKIP`. `release\build\LBP3Launcher.exe` (the v0.1.3 package's) still shows the toggle; with it on it sets nothing, off it sets `LBP3_RES_SKIP=bloom`, which changes nothing at 16:9 now.
- 4K and 2560x1080 not run in game (the same rule; restest pins the values). Run `capfix_lane.sh` at 3840x2160 before a v0.1.4.
