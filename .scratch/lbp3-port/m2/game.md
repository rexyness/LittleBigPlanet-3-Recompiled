# Milestone 2: game runs (gap 17 and routes to the milestone 2 libraries)

Build: `D:\Recomps\lbp3-recompiled` on `port` `18984f6` (`build\lbp3.exe`, not rebuilt), the libraries of milestone 2 still stubs. All runs by remote input (`tools\remote_start.ps1`), 1280x720 window, screenshots in `analysis\screenshots\m2_*.png`, logs in `analysis\m2_*`.

## The human's saves

Before any run, `user\savedata\CUSA00473` was copied to `analysis\savedata_backups\before_m2_20261110_2119` (4 files, sizes and contents compared equal; md5 list next to it, `before_m2_20261110_2119.md5`):

- `LBPxSAVE/00000001/bigfart10` 8128
- `LBPxSAVEx12x349/00000001/playfart_12_349_27` 20992
- `LBPxSAVExLITTLE/00000001/littlefart118` 48044
- `LBPxSAVExLITTLE/ICON0.PNG` 1063

Every run starts from a baseline copy (`saves.sh`, or `m2_lib.sh`'s `use_baseline` in hand-driven sessions, followed by `human_restore`).

## Helpers

`tools\m2_lib.sh` (sourced after `cd /d/Recomps/lbp3-recompiled`): `cmdq` (queues input, waiting until the game has taken the previous batch), `shot <name>` (`m2_<name>.png`), `launch`, `wait_log`, `stubs` (the milestone 2 libraries' `[stub]` lines so far), `saves_ls` (name, size, md5 prefix), `save_settle <s>` (waits until no save file changed for that long), and `human_check`/`human_restore` against the backup above.

## Create mode controls learned (Popit cursor, 2026-11-10)

In the baseline level (`savedata_thruster_on`), hand-driven (`m2_x1_*.png`):

- Square opens the Popit; its centre item is Popit Cursor (cross). The t10 stick path puts the cursor on the Thruster (`m2_x1_cursor.png`).
- Over an object, square opens its tweak menu (also while holding it); circle closes the tweak menu and keeps the hold. Circle in cursor mode goes back to the Popit menu.
- Cross grabs, the left stick drags (the camera follows), cross drops (`m2_x1_e.png`: the Thruster dragged left next to the entrance).
- No delete button was found in a few tries, so the gap 17 edit is a move. Over the hovered or held Thruster, triangle does nothing visible, R1 and L1 nothing either, R2 shows an info panel (Layer Range 3, Weight 16.8, Grouped Objects 0, Static/Dynamic), and L2 turns the Thruster into a see-through ghost, probably a layer view (`m2_x2_sheet.png`, `m2_x2_hover3.png`). In the Popit's item pages triangle is Delete (the hint bar in My Pictures).
- The hint bar shows L3 Preview, R3 Freeze Camera, right stick Scale/Rotate while holding, d-pad Undo.

Side finding: the hand-driven session that never saved still rewrote `LBPxSAVExLITTLE/00000001/littlefart83` (47020) as `littlefart85` (47028); the other two files stayed.

## Gap 17 (a): an edited level

**Result: passed** (runs `e1`, `e3` and `e4`, the last with the script as delivered; `e2` lost its Save Changes press and serves as one more unsaved case). Evidence sheet: `analysis\screenshots\m2_g17a_evidence.png`.

Script: `tools\t17_edit.sh <tag>` (baseline `savedata_thruster_on`, restored once before launch A; three launches on the same saves; the human's saves come back at the end through `saves.sh`).

- **A:** Edit Now, the Popit cursor onto the Thruster (the t10 stick path), cross, left stick full left 1.5 s, cross: the Thruster sits right beside the entrance (`m2_e3_A_moved.png`). The game is killed without saving.
- **B:** relaunch, Edit Now, the same move, Popit closed, options → Change to Play Mode → Save Changes, wait until the save files are quiet for 15 s, kill.
- **C:** relaunch, Edit Now (start view), then options → Change to Play Mode → Discard Changes (the game asks even with no change) → play view.

What the runs showed:

- **The unsaved move is gone after the relaunch**, in all three runs: A's and B's start views match (Thruster spot mean difference 4.1, 6.6 and 3.5 of 255 in `e1`, `e2`, `e3`). In `e2`, where the save never happened, C's start view matched too (5.0), with `bigfart7` untouched.
- **The saved move is back after the relaunch** (`e1`, `e3`). C's start view (`m2_e3_C_level.png`, `m2_e1_C_level2.png`) shows the Thruster nearer the entrance. The camera differs from the baseline's start view (the left wall is seen from another angle), identically in `e1` and `e3`; whether the game stores the Create camera with the level or frames the scene from its contents isn't known, so pixel differences against A (61.9, 60.7) mix the move and the camera. Measured instead in entrance diameters (entrance about 110 px across, Thruster base about 202 px in both views, so the scale matches): the Thruster's centre is 3.7 diameters right of the entrance's in the baseline view, 2.8 in B's view just before the save (`m2_e3_B_moved.png`, measured on `m2_e3_B_moved_grid.png`: entrance centre about x 325, base about x 535–740), and 2.9 in C's after the reload. In play mode (`m2_e3_C_play.png`) the Thruster stands right beside the entrance, as in B's play view straight after the save in `e1` (`m2_e1_B_saving.png`).
- **Save files** (`analysis\m2_e1_saves.txt`, `m2_e3_saves.txt`):
  - baseline: `LBPxSAVE/00000001/bigfart7` 2736, `LBPxSAVEx12x349/00000001/playfart_12_349_14` 864, `LBPxSAVExLITTLE/00000001/littlefart83` 47020, `LBPxSAVExLITTLE/ICON0.PNG` 1063.
  - A (move, no save; the same after the kill): only `littlefart83` → `littlefart85` (47028). `bigfart7` unchanged (md5 `c94640d3`).
  - B after Save Changes: `bigfart7` → `bigfart8` (2736 bytes, md5 `4b3def37` in `e1`; 2732 bytes, `097fa65d` in `e3`), and `littlefart` moves on (`littlefart89` 47012 in `e1`, `littlefart88` 47028 in `e3`). `playfart` and `ICON0.PNG` unchanged.
  - C (load, discard): `littlefart` moves on again (`littlefart91`); `bigfart8` unchanged.
  - So the level lives in `LBPxSAVE` (`bigfart`), which only Save Changes rewrote. `LBPxSAVExLITTLE` (`littlefart`) is rewritten on every launch that enters Create, with or without a save.
- No milestone 2 library was called in any of these sessions, besides the `sceImeDialogGetStatus` polling every run has.

Surprises:

- **A lost press in `e2`.** The 100 ms cross on the Save/Discard/Cancel dialog didn't register, and the dialog stayed up for the rest of the session. The frames around it were uneven (30-frame windows with averages up to 57.8 ms and single frames up to 176 ms, `m2_e2_B_stderr.txt`), so a 100 ms press can fall between pad reads. The script now presses 300 ms and repeats while `LBPxSAVE` is unchanged.
- **A white screen after saving with the Popit menu open (`e3`, once).** In `e3` the press closing the Popit was also lost, so Save Changes was taken with the Popit menu still on screen. The save went through (`bigfart8`), but the screen then stayed a washed-out white still of the dialog and the Popit for 25 s and more, until the kill (`m2_e3_B_saving.png`, `m2_e3_B_saved.png`), while frames went on at 33.3 ms with none over 40 ms. Right after the save the log has 17 `[shader warning] vk_rasterizer.cpp:1090 BindTextures: Rejecting invalid S# max_aniso=5, filter_mode=1, mip_filter=0, border_color_type=0, border_color_base=0x16aff0100`. No other `m2_*` log has that line (all 21+ were checked), but older logs from the Thruster save runs do (`ab_trace_*`, `t10C`/`t10D`/`t10e`, `t07_ctl1`, `ab_oldreset_1`, `feel`). `e1` (Popit cursor active at the save, not the menu) went to play mode normally. Whether the game would have come back from the white screen is unknown, since the run killed it. Not reproduced. A lead: the photo tool's flash (`m2_x4_snap.png`) looks the same, a white wash with grey bands at the top and bottom, so leaving Create may take a picture (the level's photo) that ends differently with the Popit menu open.
- In Create, frames over 40 ms are common once the level is open (up to 21 in a 30-frame window), the known Create-scene spikes of milestone 5.
- One stuck GPU wait, which recovered: in the Text Chat session `x3`, shortly after Edit Now opened the level (frame about 930, one frame of 198.9 ms just before it), the log has `ReportStuckWait: WaitRegMem stuck over 1 s on compute: memory 0x1e1303d10 value=0x207a func=3 ref=0x20f8`, in the 8-slot label ring of ticket 10 (`0x1e1303cd8…d10`), with nearby compute `RELEASE_MEM` writes listed (`m2_x3_stderr.txt:685`). The game went on normally (33.3 ms frames, the rest of the session). No other `m2_*` log has a stuck wait or a device loss.
- After `e3` the script changed: 250 ms presses to close the Popit and on the pause menu, and the Save Changes retry now watches `LBPxSAVE` alone (`LBPxSAVExLITTLE` changes without any save, so it can't tell). **Run `e4` (22:34–22:40) checked the delivered script:** the Popit was closed before the save (`m2_e4_B_edited.png`), Save Changes took on the first press (`bigfart7` → `bigfart8`, 2732 bytes, md5 `50013127`), and B's play view right after the save shows the Thruster beside the entrance (`m2_e4_B_saved.png`). A and B start views match (6.4); C's start view equals `e1`'s and `e3`'s. There was no white screen and no `S#`, stuck or device-lost line (`m2_e4_sheet.png`). C's Change to Play Mode → Discard step left the game in Create (`m2_e4_C_play.png`; the press probably landed on Cancel), so C's play view is unreliable; judge by C's start view and B's post-save play view.

## Gap 17 (b): story progress

**Result: passed** (run `s1`, 21:49–21:55). Evidence sheet: `analysis\screenshots\m2_g17b_evidence.png`.

Script: `tools\t17_story.sh <tag>`, adapted from `t08_bunkum.sh`: baseline `savedata_thruster_on` restored once, before launch 1; launch 2 uses the saves launch 1 left; the human's saves come back at the end. Home is checked by the colour of the Play label (`home_on_play`), because the menu reopens on the last item used: launch 1 opened on Create (up, then cross), launch 2 on Play (cross).

- **Launch 1:** Home → Play → Adventure → Prologue → map, a screenshot at each (`m2_s1_1_{home,play,world,map}.png`); cross on the Legend of Bunkum (auto-skip off). All 16 voice lines played, and the game opened Needlepoint Peaks by itself 147 s after the start (`m2_s1_1_needle.png`). The save files were quiet for 30 s at 212 s; then the kill.
- **Launch 2:** the same menus (`m2_s1_2_*.png`), then d-pad down on the map (`m2_s1_2_map_down.png`).

| Screen | Before the play (launch 1) | After the relaunch (launch 2) |
|---|---|---|
| Play → Adventure Story | 2% complete, next level "The Legend of Bun…" | 4% complete, next level "Needlepoint Peaks" |
| Prologue | 1 of 5 levels finished | 1 of 5 levels finished (a guess: the Legend, a cutscene, doesn't count as a level) |
| Map | cursor on the Legend of Bunkum; the node above it shows the padlock picture | cursor on Needlepoint Peaks, which now shows its level picture and "0% goodies and prizes"; one node down is the Legend of Bunkum |

Save files (`analysis\m2_s1_saves.txt`):

- baseline: `bigfart7` 2736, `playfart_12_349_14` 864, `littlefart83` 47020, `ICON0.PNG` 1063.
- on the map before the play: `littlefart83` → `littlefart84` (47044); nothing else.
- after the autosave: `LBPxSAVEx12x349/00000001/playfart_12_349_14` (864) → `playfart_12_349_17` (1768), `littlefart84` → `littlefart87` (47048). `bigfart7` and `ICON0.PNG` unchanged. The log shows `playfart_12_349_16` written at the end of the cutscene and `_17` after Needlepoint Peaks opened, as in `m1port_bunkum_stderr.txt`.
- after launch 2: only `littlefart87` → `littlefart88` (47064).
- So story progress lives in `LBPxSAVEx12x349` (`playfart`), levels in `LBPxSAVE` (`bigfart`), and `LBPxSAVExLITTLE` (`littlefart`) is rewritten in every session.

## Routes to the milestone 2 libraries

All routes start from the baseline level (`savedata_thruster_on`, Pod → Home on Create → square, Edit Now). Hand-driven searches: `m2_x3_*` and `m2_x4_*` screenshots, logs `analysis\m2_x3_stderr.txt` and `m2_x4_stderr.txt`. One step of a search is `tools\m2_probe.sh <name> <wait s> '<input>'`: it sends the input, takes a screenshot, and prints the new milestone 2 `[stub]` lines. Two lines show up in every run and are noise: `sceImeDialogGetStatus`, which is polled about 3 times a frame from boot (10,910 calls by frame 3,700, F10 report), and `sceCommonDialogInitialize`. The Popit grid is: Character, Goodies Bag, Global Stuff / Personal ("Tools and Used Objects"), Popit Cursor (centre, where it opens), Stickers & Decorations / Text Chat, Tools Bag.

### libSceImeDialog (text entry): two routes found

1. **Popit → Text Chat** (`m2_x3_tc*.png`): square, left (Personal), down (Text Chat), cross → `[stub] libSceImeDialog :: sceImeDialogInit` (`m2_x3_stderr.txt:852`). The human's hit in the Pod (`play_log.1.txt:665`) may have come the same way, but the Pod wasn't checked (there, square is "Use Pod Computer"). No keyboard appears. The Text Chat icon turns into two text lines, and the Popit then stays on Text Chat for good: d-pad, circle, square and the stick are ignored (`m2_x3_tc_after.png`). Options still opens the pause menu, but that menu ignores up and cross (`m2_x3_tc_after2.png`, `m2_x3_pm.png`), and options again returns to the stuck Popit (`m2_x3_pm45.png`). Frames go on at 33.3 ms. So it's a soft lock, waiting on a dialog status that never finishes. No input tried gets out (circle, d-pad right and up, square, cross, the left stick, options twice); the run ended with a kill.
2. **Create pause menu → Edit Details → level name** (`m2_x4_ed*.png`, `m2_x4_name*.png`): options, up ×3 (Edit Details), cross (the page "About This Level", with "SET THE NAME!" pointing at "Unnamed Level"), right (the name field turns green), cross → `[stub] libSceImeDialog :: sceImeDialogInit` (`m2_x4_stderr.txt:818`). The field loses its highlight and no keyboard appears; down and circle are ignored. Options closes the page and the game calls `[stub] libSceImeDialog :: sceImeDialogAbort` (`m2_x4_stderr.txt:874`), back in Create, which then works normally (Sackboy moves, the Popit opens; `m2_x4_name_after.png`, `m2_x4_rec.png`). The name stays "Unnamed Level".

### libSceJpegEnc: one route found

**Popit → Stickers & Decorations → Take A Photo** (`m2_x4_st3.png`, `m2_x4_ph.png`, `m2_x4_snap*.png`): square, right (Stickers & Decorations), cross. The page opens on its Functions row (Sticker & Decoration Edit Tool, Take A Photo, Sticker Cutter, Paint, …). Then right (Take A Photo), cross (USE!: a photo frame with "X Take Photo"), cross → `[stub] libSceJpegEnc :: sceJpegEncQueryMemorySize` (`m2_x4_stderr.txt:1748`), and nothing else from JpegEnc. A white flash follows. The game then writes the profile at once (`LBPxSAVE/00000001/bigfart9`, 3436 bytes against the baseline's 2736) and shows "My Pictures" with one entry, "A Photo", whose tile is black (`m2_x4_snap2.png`). The game goes on normally. So with the stub the photo is kept, but empty: the stub's 0 for the memory size apparently stops the encode before `sceJpegEncCreate`/`Encode`. USE! on the photo gives a black sticker on the cursor, which turns invisible over a wall; no JpegDec call (`m2_x4_use.png`, `m2_x4_pl.png`).

The level icon on the Edit Details page (cross on the icon) opens a sticker picker ("Stickers", Colors, Decorative; More! → Organize), not a photo; no JPEG call (`m2_x4_icon_sheet.png`, `m2_x4_more.png`). The Personal page's tools (Corner Editor, Advanced Glue, Material Changer, UV Tool, Capture Object, Paint, Sticker Cutter, Sticker & Decoration Edit Tool, Rail Editor, Dephysicalize, Slice 'N Dice) have no camera; Capture Object needs a held cross while the stick drags the marquee, which the remote queue can't do (buttons and sticks are separate commands), so it wasn't completed.

The eboot links its own libjpeg (strings "Corrupt JPEG data: bad Huffman code" and others), so some decoding may not go through libSceJpegDec at all.

### libSceMsgDialog, libSceSaveDataDialog

The struck-out online items lead to game-drawn dialogs, not system ones. Create pause menu → (down to the bottom row) LBP Messages → "To use this function you have to be connected to the internet. Do you want to go online?" Yes/No (`m2_x4_msg.png`). Yes → a connecting icon, `[np] reporting sign-in state 1 to the game`, then "There is a problem with your network connection. Please check your connection and try again." Close (`m2_x4_yes34.png`). No `libSceMsgDialog` or `libSceSaveDataDialog` line. (`libSceErrorDialog` isn't in the stubbed imports listed in `remote_stdout.txt`, so a missing line for it proves nothing.)

The same prompt comes from Home → Community and from Home → Store (`m2_x5_comm_pair.png`, `m2_x5_store_pair.png`, log `m2_x5_stderr.txt`); still no system dialog.

**Damaged save at boot** (`tools\t_m2_dialogs.sh <tag> big|little|play|nobig`; only the test copy is damaged, after `saves_baseline`; not a remote-input-only route):

- `big` (`bigfart7` cut to half, its second quarter overwritten): a full-screen game-drawn page, "PROFILE IS CORRUPT. Part of your profile appears to be corrupt, please make sure you have the latest updates installed. If you continue, it will be deleted. You will lose the levels on your moon, but not your game progression.", with X Delete / O Retry (`m2_m2dlg_big_sheet.png`). The script's cross chose Delete: the game wrote a new empty `LBPxSAVE/00000001/bigfart1` (224 bytes) and went on to the Pod and its offline network message.
- `little` (`littlefart83` damaged): the same page, saying "You will lose your game progression, but not the levels on your moon"; Delete led to a new `littlefart1` (40396), a rewritten `ICON0.PNG` (1063 → 48464 bytes) and the first-boot brightness calibration (`m2_m2dlg_little_sheet.png`).
- Neither run called `libSceSaveDataDialog` or `libSceMsgDialog` (`m2_m2dlg_big_stderr.txt`, `m2_m2dlg_little_stderr.txt`).

**Not found:** no offline route reached `libSceMsgDialog` or `libSceSaveDataDialog`. Tried: the Create pause menu's struck-out items (LBP Messages, then Yes to going online), Home → Community and Store, a corrupt `LBPxSAVE` and a corrupt `LBPxSAVExLITTLE` at boot, and, in all the runs above, the offline network message at boot, Save Changes, the photo save and Text Chat. Every message the game showed was drawn by the game itself. The PlayStation®Camera sticker page was only viewed (`t_m2_photo.sh` phase 2), with no call. Untried: a full disk (a save mount failing for lack of space; the runtime's savedata layer would have to fake it), deleting a level from My Moon, Save As, and the `play` and `nobig` cases of `t_m2_dialogs.sh`.

**Not found: libSceJpegDec.** Not called when the empty photo is shown in My Pictures, used as a sticker, or shown again after a relaunch (`t_m2_photo.sh` phase 2, `m2_m2photo_2_stderr.txt`). It probably needs a photo with real JPEG data, which only a working encoder gives; the eboot also links its own libjpeg.

### Reusable route scripts (all from `savedata_thruster_on`; the human's saves come back at the end)

| Script | Library | Logged line | What the game does next on the stub build |
|---|---|---|---|
| `tools\t_m2_ime.sh <tag> name` | libSceImeDialog | `[stub] libSceImeDialog :: sceImeDialogInit`, then `… :: sceImeDialogAbort` on options | Edit Details page, the name field unhighlighted, no keyboard; down and circle ignored; options closes the page (Abort) and Create works again (`m2_m2ime_name_sheet.png`) |
| `tools\t_m2_ime.sh <tag> chat` | libSceImeDialog | `[stub] libSceImeDialog :: sceImeDialogInit` | the Popit stays on Text Chat for good; options opens a pause menu that takes no other input; a soft lock (`m2_m2ime_chat_sheet.png`) |
| `tools\t_m2_photo.sh <tag>` | libSceJpegEnc | `[stub] libSceJpegEnc :: sceJpegEncQueryMemorySize` | the photo flash or self-timer, then a profile save (`bigfart7` → `bigfart8`, 2736 → 3420 bytes) and My Pictures with a black "A Photo" tile; no hang; after a relaunch the black tile is still there, with no JpegDec call (`m2_m2photo_sheet.png`) |
| `tools\t_m2_dialogs.sh <tag> big\|little\|play\|nobig` | (SaveData/Msg dialogs) | none | game-drawn "PROFILE IS CORRUPT" page; Delete makes a fresh save (only `big` and `little` were run) |

Each script was run once after the hand-driven search and reproduced its line: `m2ime_name`, `m2ime_chat` and `m2photo` (output in `analysis\m2_routes_out.txt`), and `m2dlg_big` and `m2dlg_little` (`analysis\m2_dlg_out.txt`).

### Note for the library work

The user pointed to a PS4 firmware dump at `D:\Recomps\ROMs\PS4\Firmware Dump` (436 files; only listed here, nothing written). It holds `libSceImeDialog.sprx`, `libSceIme.sprx`, `libSceImeBackend.sprx`, `libSceJpegDec.sprx`, `libSceJpegEnc.sprx`, `libSceJpegParser.sprx`, `libSceMsgDialog.sprx`, `libSceSaveDataDialog.sprx`, `libSceCommonDialog.sprx`, `libSceErrorDialog.sprx` and `libScePngDec/Enc.sprx`.

## End state

The human's `user\savedata\CUSA00473` was restored from `analysis\savedata_backups\before_m2_20261110_2119` after the last run. `diff -r` against the backup finds no difference, the md5 list matches `before_m2_20261110_2119.md5`, `analysis\savedata_human_kept` doesn't exist, and no `lbp3.exe` is running (checked again after `e4`). The baseline `analysis\savedata_thruster_on` is intact (`bigfart7` md5 `c94640d3…`). The clone is still on `18984f6` with no tracked changes. Nothing was written to the game dump. The debugger (x64dbg) wasn't used.

## Integration on m2/verify

Build: `D:\Recomps\lbp3-recompiled` on `m2/verify` `91523ad` (`build\lbp3.exe` as built by the coordinator, 22:42), with the real libSceImeDialog/libSceIme, libSceJpegDec/Enc and libSceMsgDialog/SaveDataDialog/CommonDialog/GameCustomDataDialog. Before the first run the human's saves were copied to `analysis\savedata_backups\before_m2v_20261110_2243` (the same 4 files as `before_m2_20261110_2119`, compared equal; md5 list `before_m2v_20261110_2243.md5`), and `m2_lib.sh`'s `human_check`/`human_restore` now use that copy. No run used `LBP3_AUTOPRESS`.

**Display.** Runs up to and including `v5` (the export/import session, started 23:02) began on the 240 Hz display (`\\.\DISPLAY1`). The human switched to the 165 Hz display during `v5`; every later run is on `\\.\DISPLAY2` (165 Hz, 125% scaling).

**New tools.**
- `tools\capture_dialog.ps1 -Out <png>`: PrintWindow of the game window, with the process's other top-level windows (the dialogs) drawn at their real screen positions, and the dialog's and game's centres printed. It never shows other applications. A plain screen grab (`tools\capture_screen.ps1`, `sshot` in `m2_lib.sh`) picked up whatever covered the game, including the human's own windows; those captures were deleted.
- `tools\capture.ps1` (the clone's) takes the process's `MainWindowHandle`, which is the dialog while one is open, so during a dialog it saves the dialog alone (600×135 for the name dialog, `m2_m2v_name_B_dialog_window.png`). That's worth knowing for other scripts; it isn't a game bug.
- `m2_probe.sh` now also prints `[ime]`, `[jpeg]`, `[dialog]` and `[modal]` lines; `m2v_lines <n>` in `m2_lib.sh` prints them after stderr line n.

### 1. Boot: passed

`tools\t_m2v_boot.sh m2v_boot` (output `analysis\m2v_boot_out.txt`; stdout `analysis\m2_m2v_boot_stdout.txt`, compared with the port build's `analysis\m2v_port_stdout_ref.txt`).
- `imports: 708 from game modules, 447 from HLE, 135 functions stubbed` (port build: 411 from HLE, 171 stubbed), and `missing (136 unique)` (was 172).
- Gone from the missing list: libSceCommonDialog (1), libSceGameCustomDataDialog (6), libSceIme (3), libSceImeDialog (5), libSceJpegDec (5), libSceJpegEnc (4), libSceMsgDialog (4), libSceSaveDataDialog (8). That's 36 functions, exactly the difference; no other library changed.
- No `[stub]` line from these libraries in stderr. `[ime] keyboard opened for user 1 (no keyboard events are delivered)` once. No `[dialog]` line at boot.
- The Pod comes up with the usual offline network message, then Home on Create (`m2_m2v_boot_pair.png`). No GPU warning.

### 2. Text entry, level name: passed

`tools\t_m2v_name.sh m2v_name` (output `analysis\m2_m2v_name_out.txt`, sheet `m2_m2v_name_sheet.png`).
- **Set:** `ime M2 Test Level` queued, then Cross on the name field (Create pause menu → Edit Details → right):
  ```
  [ime] scripted text queued: "M2 Test Level"
  [ime] dialog opened: "Enter level name", up to 64 characters, text "" (type 0, options 0)
  [modal] ime opened (1 open)
  [ime] scripted: typing "M2 Test Level" and confirming
  [ime] dialog confirmed: "M2 Test Level" (13 characters)
  [modal] closed (0 open)
  ```
  Edit Details shows "M2 Test Level" and "20% filled in", and the hint moves on to "SET THE ICON!" (`m2_m2v_name_A_named.png`).
- **Saved by itself:** the game wrote `bigfart8` (2764 bytes) right after the confirm (stderr line 753, after the confirm at 735), before any Save Changes. So Change to Play Mode then went straight to play mode, with a brief white flash (`m2_m2v_name_A_dialog.png`) and no save/discard question. The script's three Save Changes presses landed in play mode, which explains its "LBPxSAVE unchanged" warning.
- **Kept after a relaunch:** Home's Recently Edited card reads "M2 Test Level" (`m2_m2v_name_B_home.png`), and so does Edit Details (`m2_m2v_name_B_field.png`).
- **ime-cancel:** `[ime] dialog opened: ... text "M2 Test Level"`, `[ime] scripted: cancel`, `[ime] dialog cancelled`, `[modal] closed (0 open)`; the name stays (`m2_m2v_name_B_cancelled.png`).
- **Opened with nothing queued:** the dialog stays open ("Enter level name", the old name prefilled, "13 / 64", OK/Cancel; `m2_m2v_name_B_dialog_screen.png`) until `ime-cancel` closes it. The name stays, and the page takes input afterwards (down moves to the labels box, `m2_m2v_name_B_input.png`).

### 3. Photo: the encoder works, but the photo is black (bug)

`tools\t_m2v_photo.sh m2v_photo` and `m2v_photo2` (PHASE2=0, keeps the test save as `analysis\m2_m2v_photo2_testsaves`). Sheet `m2_m2v_photo_sheet.png`.
- `[jpeg] encoded 640x360 BGRA, quality 70 -> 4291 bytes (buffer 230400)`, once per photo, in both runs. 640×360 is half of this window's 1280×720 render size (the notes' 960×540 assumes 1080p).
- The game then saves at once (`bigfart7` → `bigfart8`, 2736 → 7828 bytes; with the stub it was 3420).
- **But the My Pictures tile and the picture sticker are still black** (`m2_m2v_photo_pictures.png`, `m2_m2v_photo_sticker.png`), and still black after a relaunch (`m2_m2v_photo_2_pictures.png`).
- The JPEG stored in `bigfart8` (found at byte 2366, 4291 bytes, extracted to `analysis\screenshots\m2_m2v_photo2_extracted.jpg`) is a valid JFIF (starts `FF D8 FF E0`, 640×360), but every pixel is (1, 1, 1): the encoder was given an all-black frame. 4291 bytes is about what a flat 640×360 image takes at quality 70.
- So the encoder's output is a correct JPEG of an all-black image. **That the frame handed to it was black is an inference: the encoder's input was never observed.** Two things support it:
  - The logged width, height, quality and buffer size (640, 360, 70, 230400 = 640×360) come from the same parameter block as the image pointer, so the library reads the game's structure correctly.
  - The likely cause is the half-screen capture's readback of the render target (the game reads the pixels back four frames after queuing the capture), which the port's video core may not fill.
- One more point needs checking: `jpeg.md` says the half-size thumbnail is a DXT texture made without the encoder when no `quality 90` line appears, and none did. The black tile may be that thumbnail, which would mean the capture itself is black either way. Whether the tile is the JPEG or the thumbnail wasn't checked.
- No `[jpeg] decoded` line in any run, as expected (the game decodes its own photos with its linked libjpeg).

### 4. Text Chat: passed (the stub's soft lock is gone)

`tools\t_m2v_chat.sh m2v_chat` (sheet `m2_m2v_chat_sheet.png`, log `analysis\m2_m2v_chat_stderr.txt`).
- Popit → Text Chat with `ime hello`: `[ime] dialog opened: "Enter Your Message", up to 96 characters, text ""`, `[ime] scripted: typing "hello" and confirming`, `[ime] dialog confirmed: "hello" (5 characters)`, `[modal] closed (0 open)`.
- The game shows "hello" for a few seconds in a speech bubble above Sackboy with a "Close" (circle) prompt, then as a line in the chat log at the top left ("<player name>: hello"). The Popit closes.
- A second Text Chat with nothing queued: the dialog sits centred over the game ("Enter Your Message", "0 / 96", OK/Cancel; `m2_m2v_chat_dialog_screen.png`) until `ime-cancel` (`[ime] dialog cancelled`, `[modal] closed (0 open)`).
- Afterwards the Popit moves (right → Tools Bag) and the pause menu moves (up → Save As) (`m2_m2v_chat_popit_right.png`, `_pause_up.png`).

### 5. Save-data dialogs: passed (level export and import)

The route came from the translation strings ("Select a level on your moon with <icon square> and use the backup tool"):
- **Export:** Home → Create → My Moon → the level → square (More!) → the level page's Back up button (row Move / Copy / Delete / Back up).
- **Import:** My Moon → an empty slot → square (More!) → Import (under Create).
- The profile items ("Back up Profile", "Import Profile") weren't looked for.

Hand-driven session `v5` (log `analysis\m2_v5_stderr.txt`, the test saves at the end in `analysis\m2_v5_testsaves_end`):
- **Export to a new backup:** `[dialog] savedata: list (save), 0 saved data + new` with `[modal] savedata opened (1 open)`. The list window ("Choose where to save.", "[New saved data] New Save", Save/Cancel) appears over the game. Cross gives `answered (result 0, button 0)`, `progress bar: Saving...`, `closed by the game`, and a new folder `CUSA00473x00LEVEL6AF37968` with `L0` (2728 bytes) and an empty `sce_sys` in the test saves (`[savedata] mount 'CUSA00473x00LEVEL6AF37968' mode 0x6 ... (new)`).
- **Export onto it again:** `list (save), 1 saved data + new`, opening on the backup (with its date). Cross gives `answered (result 0, button 0, CUSA00473x00LEVEL6AF37968)`, then `system message 3: Do you want to overwrite this saved data?` with No focused (`m2_v5_bk2_overwrite_dialog.png`: the dialog over the game, its centre at (640, 345), the game's at (640, 360)). Circle gives `answered (result 1, button 0)`; the backup's files, sizes, times and md5 are unchanged.
- **Import, cancelled:** `list (load), 1 saved data`, listing only the level backup (no main saves) and opening on it (`m2_v5_import_list.png`). Circle gives `answered (result 1, button 0)`, `progress bar: Loading...`, `closed by the game`. The slot is still "Empty Level" and no save file changed (`m2_v5_import_cancelled.png`).
- **A race in the progress window (minor):** on the first export the save took so little time that the game closed the progress dialog before its window existed: `closed by the game` (line 1046), `[modal] closed (0 open)`, then `[dialog] savedata: couldn't create the window (1400)` (line 1048). The gate is balanced and nothing hangs, but no "Saving..." window ever showed. The import's "Loading..." didn't log this.
- Pressing Cross on the level page once landed on Trailer instead (an extra right had moved the highlight): "0 Videos ... Use the SHARE button" (`m2_v5_backup_screen.png`), a content-search page; circle backs out.

**Display switch during `v5` (not a game bug, but a pacing gap).**
- At the switch the vblank source flapped: `vblank source: \\.\DISPLAY2`, then `\\.\DISPLAY1` with `display refresh 239.999 Hz`, then `vblank source: \\.\DISPLAY2` again, with no new `display refresh` line (`m2_v5_stderr.txt` lines 1156–1239).
- From then on the log shows `[pace] 165.0 presents/s, 165.0 vblanks/s (vblank), refresh 240.00 Hz` and 48.5 ms frames (`15600 frames flipped (avg 48.5 ms, worst 48.8 ms, 30 over 40 ms`). So when the window lands on a display with another refresh rate, the pacer keeps the old rate.
- The PrintWindow captures after the switch also show the game image shifted up by about 38 px, with a black band at the bottom (`m2_v5_m3.png`, `m2_v5_import_list.png`).
- Both are to be checked against fresh runs on the 165 Hz display (below).

**Fresh runs on the 165 Hz display pace correctly.** Every run after `v5` logs `[video] display refresh 165.007 Hz` and `vblank source: \\.\DISPLAY2`, with 33.3 ms frames, and its captures are no longer shifted. So the 48.5 ms frames and the shifted image were left over from the switch inside `v5`. The pacer's not re-reading the refresh rate when the window changes display remains as an observation; the coordinator's display switch (`LBP3_DISPLAY`) is the planned answer.

### 6. Gap 17 on this build: passed

- **(a) `t17_edit.sh m2v_e`** (23:13–23:18, all three launches at 165 Hz; sheet `m2_m2v_e_sheet.png`, `analysis\m2_m2v_e_saves.txt`):
  - A's and B's start views match (Thruster spot 3.1 of 255): the unsaved move is gone.
  - Save Changes took on the first press: `bigfart7` → `bigfart8`, 2732 bytes, md5 `50013127`, the same bytes as `e4` on the port build.
  - C's start view shows the moved Thruster (the same view as `e1`/`e3`/`e4`). This time C's Discard step reached play mode, and C's play view matches B's play view after the save almost exactly (whole-view difference 1.6 of 255).
  - No `S#`, stuck or device-lost line.
- **(b) `t17_story.sh m2v_s`** (23:18–23:24, both launches at 165 Hz; `m2_m2v_s_evidence.png`, `analysis\m2_m2v_s_saves.txt`):
  - All 16 voice lines played, and Needlepoint Peaks opened after 148 s.
  - After the relaunch: Adventure Story 2% → 4%, next level "Needlepoint Peaks", and the Needlepoint Peaks node unlocked with the cursor on it.
  - `playfart_12_349_14` (864) → `playfart_12_349_17` (1768).
  - No GPU warning.

### 7. Milestone 1 regression: passed

`sh tools/m1_checks.sh m2v` (23:24–23:38, output `analysis\m2v_m1_checks_out.txt`). Every run logs `display refresh 165.007 Hz` (vblank pacing on `\\.\DISPLAY2`), and none has an `S#`, stuck or device-lost line or any `[ime]`/`[jpeg]`/`[dialog]` dialog line. Compared with the 165 Hz dry run in ticket 04 (which used timer pacing):

| Check | Dry run (165 Hz, `58e7c93`) | m2/verify `91523ad` (165 Hz) |
|---|---|---|
| 1. Introduction | 33.33 ms frames; steps ≠ 1 0.25% | run 1: 1603 flips in the level, steps ≠ 1 0.19% (3: two 2s, one 0), frame averages 33.3–35.0 ms; run 2: 1606 flips, 0.25% (4), 33.3–35.6 ms. 3 frames over 40 ms after the level opened in each run (`analysis\pace_m2v_intro{1,2}_stderr.txt`) |
| 2. Thruster save recipe | 3 of 3 OK | 3 of 3 OK, 20 savedata mounts each (`analysis\ab_m2v_{1,2,3}_stderr.txt`) |
| 3. Legend of Bunkum, auto-skip off | 16 of 16, Needlepoint Peaks loads | 16 of 16, Needlepoint Peaks loads, 0 watchdog lines (`analysis\m2v_bunkum_stderr.txt`) |
| 4. Create sky | (109, 160, 164) cyan | (121, 177, 182) cyan (`analysis\screenshots\ab_m2v_1_level.png`) |

The same measures on the `port` build's own milestone 1 run (`analysis\pace_m1port_intro{2,3}_stderr.txt`, 240 Hz display; `intro1` never reached the level), computed with `m1_checks.sh`'s code:
- steps ≠ 1: 0.69% and 0.44%
- frame averages after the level opened: 33.3–35.3 and 33.3–34.7 ms
- frames over 40 ms: 3 and 2

So m2/verify's 33.3–35.6 ms and 3 frames over 40 ms match the `port` build, and its steps ≠ 1 (0.19%, 0.25%) are no worse. The sky of `ab_m1port_1_level.png` measures (121, 177, 182), exactly as on m2/verify. The dry run's (109, 160, 164) came from another build.

### 8. The white screen after Save Changes with the Popit open: not reproduced

`tools\t_m2v_white.sh <tag> [attempts]`: baseline, Edit Now, the t17 Thruster move, circle back to the Popit menu (left open), options → Change to Play Mode → Save Changes, then a screenshot every 5 s for 40 s.
- Two attempts (`m2v_white` 1 and `m2v_white2` 1, both at 165 Hz). The Popit menu was open at the save each time (`m2_m2v_white_1_popit.png`, `m2_m2v_white2_pair.png`), and the save/discard dialog came up over it (`m2_m2v_white_1_sheet.png`).
- Both times the save went through and the game was in play mode within 5 s, with 33.3 ms frames and no `S#`, stuck or device-lost line.
- The one white screen (`e3`) was on the port build at 240 Hz; it hasn't come back.
- (The first run made only one attempt: `cmdq` in `m2_lib.sh` uses a global `n`, which overwrote the script's attempt count. The variable is renamed now.)

**Warnings over all 21 logs of this build** (`m2_m2v_*`, `m2_v5`, `pace_m2v_*`, `ab_m2v_*`, `m2v_bunkum`): no `Rejecting invalid S#`, no stuck wait, no device loss, no crash, and no `[stub]` line from the eight libraries. The only error line is the progress window's `[dialog] savedata: couldn't create the window (1400)` in `v5`.

### Bugs and gaps found on m2/verify

1. **Photos are black (most likely the frame capture, not the JPEG library; inferred).** `[jpeg] encoded 640x360 BGRA, quality 70 -> 4291 bytes (buffer 230400)`, but the JPEG the game stores is all (1, 1, 1), so the frame handed to the encoder was most likely empty. Evidence: `analysis\screenshots\m2_m2v_photo2_extracted.jpg` (pulled out of `analysis\m2_m2v_photo2_testsaves\LBPxSAVE\00000001\bigfart8` at byte 2366), `m2_m2v_photo_sheet.png`. Probably the half-screen capture's readback from the render target.
2. **The save-data progress window can lose a race (minor).** When the game closes a progress bar before its window exists: `[dialog] savedata: closed by the game`, `[modal] closed (0 open)`, then `[dialog] savedata: couldn't create the window (1400)` (`m2_v5_stderr.txt` lines 1046–1048). The gate stays balanced; only the "Saving..." window is never seen.
3. **The pacer keeps the old refresh rate when the window changes display** (seen only at the human's mid-run display switch). After `vblank source: \\.\DISPLAY2` with no new `display refresh` line: `[pace] 165.0 presents/s, 165.0 vblanks/s (vblank), refresh 240.00 Hz` and 48.5 ms frames (`m2_v5_stderr.txt` from line 1239). Fresh runs on that display are fine.
4. **Tooling, not the game:** the clone's `tools\capture.ps1` captures the dialog window instead of the game while a dialog is open (it uses `MainWindowHandle`). `tools\capture_dialog.ps1` (scratch tools) avoids that, and never shows other applications.

### End state (m2/verify session)

The human's saves were restored after every hand-driven session and returned by `saves.sh` after every script. See the final check below.

**Final check (after the last run):** no `lbp3.exe` running; `user\savedata\CUSA00473` is identical to `analysis\savedata_backups\before_m2v_20261110_2243` (`diff -r` and the md5 list) and to the earlier `before_m2_20261110_2119`; no `analysis\savedata_human_kept`; the baseline `savedata_thruster_on` is intact (`bigfart7` `c94640d3…`); the clone is on `m2/verify` `91523ad` with no tracked changes; nothing was written to the game dump (the level export wrote a temp file under the clone's `user\temp0\hdd1\CUSA00473_00\output\`). The level backups made during `v5` lived only in the test saves (`analysis\m2_v5_testsaves_end`).

### Reruns on the 165 Hz display (after review)

- **Item 5 as a script, fresh launch:** `tools\t_m2v_backup.sh m2v_bk2` (output `analysis\m2_m2v_bk2_out.txt`, log `m2_m2v_bk2_stderr.txt`, `display refresh 165.007 Hz`). It checks by colour that Back up / Import is highlighted before each Cross.
  - **Export to a new backup:** `list (save), 0 saved data + new`, then `answered (result 0, button 0)`, `progress bar: Saving...`, `closed by the game`, and `CUSA00473x00LEVEL6AF384B9/L0` (2728 bytes) in the test saves. Picture: `m2_m2v_bk2_1_list_dialog.png`.
  - **Export onto it again:** `list (save), 1 saved data + new`, then `answered (result 0, button 0, CUSA00473x00LEVEL6AF384B9)`, then `system message 3: Do you want to overwrite this saved data?`. Circle gives `answered (result 1, button 0)`, and the backup is unchanged. Pictures: `m2_m2v_bk2_2_list_dialog.png`, `m2_m2v_bk2_2_overwrite_dialog.png`.
  - **Import, cancelled:** `list (load), 1 saved data`, listing only the backup and opening on it (`m2_m2v_bk2_3_list_dialog.png`). Circle gives `answered (result 1, button 0)`, `progress bar: Loading...`, `closed by the game`, and no save file changed.
  - Every dialog is centred horizontally over the game: the window centre is at (640, 345), the game's client centre at (640, 360).
  - The first run of the script (`m2v_bk`) did steps 1 and 2 the same way, but skipped the import: its colour box for the Import button had been measured on a `v5` capture that the display switch had shifted.
- **The progress-window race reproduces:** `[dialog] savedata: couldn't create the window (1400)` after `closed by the game` in all 3 fresh exports (`v5`, `m2v_bk`, `m2v_bk2`). So the "Saving..." window has never been seen; the "Loading..." one didn't log it. No progress-bar window was pictured in any run.
- **Item 4 again:** `tools\t_m2v_chat.sh m2v_chat2` (165 Hz) repeats the same lines: `dialog confirmed: "hello"` and, on the second open, `dialog cancelled`. Its dialog picture `m2_m2v_chat2_dialog.png` shows the text-entry window (class `LBP3ImeDialog`, 618×182) centred over the game (centre (640, 345) vs (640, 360)).
- **Scripts changed after review:**
  - `t_m2v_name.sh` and `t_m2v_chat.sh` now use `dshot` (`capture_dialog.ps1`) instead of screen grabs. `t_m2v_name.sh` drops the Save Changes step, because the name saves itself; it now checks for the `bigfart` write after the confirm. `t_m2v_name.sh` wasn't rerun after the edit.
  - `capture_screen.ps1` carries a warning and no script calls it.

**Displays per run:**
- 240 Hz (`\\.\DISPLAY1`): the boot check, item 2 (`m2v_name`), item 3 (`m2v_photo`, `m2v_photo2`) and item 4 (`m2v_chat`).
- Spanning the switch: the export/import session `v5`.
- 165 Hz (`\\.\DISPLAY2`): everything after it, i.e. items 6–8, `m2v_bk`, `m2v_bk2` and `m2v_chat2`.
- No timing measurement was taken from a 240 Hz run.

**Not reached or not tried on this build:**
- libSceMsgDialog: its system messages need parental-control restrictions or an empty Store, neither seen offline.
- libSceGameCustomDataDialog: online only.
- Profile backup and import ("Back up Profile", "Import Profile"): not looked for.
- The no-data, corrupt and no-space save dialogs: not tried.

**Final check repeated after the reruns:** no `lbp3.exe`; the human's saves are identical to `before_m2v_20261110_2243` (`diff -r` and md5); no kept folder; `savedata_thruster_on` intact; the clone is still on `m2/verify` `91523ad`, clean; nothing new in the game dump; x64dbg not used.
