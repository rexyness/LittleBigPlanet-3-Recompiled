# Verification of `port` 2cd464a (2026-11-12 night)

This is the first full regression since `7d04e28`. It covers 1440p, the audio downmix, DLC (on by default), My Unlocks (off by default) and the `play.bat` switches. It ran from 22:09 to 23:18 on the 165 Hz display (the only display attached), using the clone's own `build\lbp3.exe` (md5 `458ececb…`). The human's saves were kept by `saves.sh` throughout. Reference snapshot: `human_play_20261112_2208`. Run data: `analysis\verify_def\`, `analysis\verify_play\`, `analysis\verify_opt\`.

## The Options check (the user's crash at eboot+0x1bd1a5)

No crash in 4 sessions and about 45 Options presses, with or without `LBP3_MY_UNLOCKS`. All at `play.bat`'s settings (1440p, frame generation, DLC):

| Session | Profile | Switch | What was done | Crash |
|---|---|---|---|---|
| o1 | the imported test profile (`analysis\unl_saves\after_import`) | off | Options ×3 in the Pod, ×3 in Needlepoint Peaks, pause menu → Return to Map, ×3 on the map, ×2 back in the Pod; 19 save writes | none |
| o2 | the same | `lbp1,lbp2` | import, then the same sequence | none |
| o3 | a fresh story save (`savedata_story_20261111`) | `lbp1,lbp2` | import, then Options ×4 | none |
| o4 | the same, 25 s in the Pod before Options | `lbp1,lbp2` | import, then Options ×3 | none |

**New finding: the import failed in o3 and o4.** The game said "Failed to import objects" on both fresh story saves. It said the same in o2, which is expected there because that profile was already imported. Unexpected for o3/o4: the earlier lane's import on the story save succeeded (`unl3`), and so did the human's.
- **What differs (log evidence):** in o3/o4, right after "import started" the game mounts `LBPxSAVEx12x350` mode 0x2, which has no "mounted" line (the folder doesn't exist), and the import then fails. The successful imports (`unl3`, `unl4`, the human's at 21:26) never mounted 12x350 during the import.
- **Possible link to the crash:** the human's crashing session mounted `LBPxSAVEx12x350` mode 0x2 three times (lines 12968, 12991 and 15043), the last one just before the crash.
- **Inferred, not traced:** the `LBPxSAVEx12x350` mount, and what makes the game try it, is the common thread between the failed imports and the crash.

## Runtime defaults (step 1): everything passes

DLC was on in every run (256 packs listed) and no route broke, so nothing was pinned to `LBP3_ADDCONT=off`.

| Check | Result |
|---|---|
| `m1_checks.sh` | Introduction steps ≠ 1 0.26% / 0.85% (frame averages 33.3–36.1 / 33.3–35.0 ms; 4 / 3 frames over 40 ms after the level opened); Thruster 3/3; Bunkum 16/16, Needlepoint loads; sky cyan (121, 177, 182) |
| `t_m2v_photo.sh` | PASS: a 640×360 JPEG in the save, mean (132.0, 190.3, 200.2) |
| `t_m2v_backup.sh` | PASS: a new backup; overwrite No and import Cancel change nothing; 6 dialogs on the overlay |
| `t_m2v_osk.sh` | PASS |
| `t13_coop.sh` | PASS (10/10) |
| `m5_np_route.sh` | walk work 19.94 ms, 0 frames over 40 ms (worst 33.6); standing 17.03 ms; 0 GPU warnings |

## `play.bat`'s settings (step 2): everything passes

The settings were `LBP3_RES=1440`, `LBP3_RES_SHADERS=1`, `LBP3_FRAMEGEN=1`, `LBP3_FRAMEGEN_MODE=warp`, with DLC on.

| Check | Result |
|---|---|
| `t_m2v_photo.sh` (run first) | PASS. I looked at the JPEG (`m2_vplay_photo_photo_0.jpg`): it matches the 1080p photo in framing and colour, mean (131.9, 190.2, 200.1), and is not black, stretched or cropped. |
| `m1_checks.sh` | Introduction 0.65% / 0.39% (frame averages 33.3–35.6 / 33.3–35.6 ms; 4 / 3 frames over 40 ms); Thruster 3/3; Bunkum 16/16, Needlepoint loads; sky cyan |
| `t_m2v_backup.sh`, `t_m2v_osk.sh`, `t13_coop.sh` | PASS |
| `m5_np_route.sh` | walk work 22.70 ms (+2.8 against defaults), 0 frames over 40 ms (worst 38.3); standing 20.70 ms; 0 GPU warnings; 99.1% of real frames in the level got a generated one (2,586 of 2,610) |

**The night's "38.3 ms window" in the Introduction was noise.** Today's 1440p Introductions show the same slow windows as defaults: one per run at a load or the story save, where frame work is low (13.5–14.9 ms), and the same 4 / 3 frames over 40 ms. The steady 1440p cost is in the mean frame work: 20.6–21.2 ms against 16.9–17.9 ms, about +3.4 ms, all well inside 33.3 ms.

**nvlddmkm events:** 0 in the 75 minutes covering both steps.

## `play.bat` itself (step 4)

Booted `cmd /d /c play.bat` (with `LBP3_DISPLAY=165hz` in the environment) on the human's real saves, with no input:
- **Settings took effect:** 1440p display buffers, `[framegen] GPU warp on`, 256 DLC packs, no `[unlocks]` line.
- **Close:** with WM_CLOSE it was gone in 2 s; the log ends with `[video] window closed`, no crash.
- **Saves:** the only change is the game's profile rewrite (`littlefart1600`, 419,700 bytes, became `littlefart1601`, 419,616 bytes). New snapshot `human_play_20261112_2318`.
- **Side note:** the audio device at that moment was "Headset Earphone (INZONE Buds - Chat)", a headset's hands-free endpoint (2 ch). That's whatever Windows had as default, not a port issue.

## New tool

`tools/stop_cleanup.sh [expected exe md5]` kills an interrupted test chain's scripts and the game, restores a swapped `build\lbp3.exe` from its `*_bak` by md5, runs `human_restore` if there's a kept copy, removes an empty kept-guests folder, and prints the state. Its first version matched any command line containing the clone's path and killed two unrelated harness shells; it now only matches shells running a tools script, `remote_start.ps1`, or `tail -f` on analysis files.
