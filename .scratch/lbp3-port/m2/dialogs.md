# System dialogs (gap 14): call sites, implementation and tests

Branch `fix/sys-dialogs` in `D:\Recomps\lbp3-wt\dialogs`: commits `d09681e` (save dir search), `b8ff5cb` (the dialogs) and `ab1193b` (pad reader lock, more tests). Addresses are module offsets (`eboot.bin+0x...`) in `D:\Recomps\lbp3-recompiled\analysis\eboot.elf`. The game was built with SDK 2.00.071 (its proc param), which matters for the save dialog results below.

## What the game imports

| Library (import lib) | Functions imported |
|---|---|
| libSceCommonDialog | `sceCommonDialogInitialize` |
| libSceMsgDialog | `Initialize`, `Open`, `UpdateStatus`, `Terminate` |
| libSceSaveDataDialog | `Initialize`, `Open`, `UpdateStatus`, `Close`, `GetResult`, `Terminate`, `IsReadyToDisplay`, `ProgressBarSetValue` |
| libSceGameCustomDataDialog (module libGameCustomDataDialog) | `Initialize`, `Open`, `UpdateStatus`, `GetResult`, `Close`, `Terminate` |

Callers were found by mapping each NID to its JUMP_SLOT relocation, the PLT thunk that jumps through it, and every `call` to the thunk. Flow names come from the job classes' name methods (vtable slot 3) and the script natives that start them.

## libSceCommonDialog

Called twice at boot, always in this order (every log shows it):

1. `+0x50859`, GameDataWrapper init (the save system). Accepts success or 0x80B80004. Any other error records a fatal boot error (`+0x12070`) and skips `sceSaveDataInitialize`.
2. `+0x337934`, NP setup. Accepts success or 0x80B80002 (ALREADY_SYSTEM_INITIALIZED); on anything else it skips `sceNpWebApiInitialize`.

The implementation returns 0 the first time and 0x80B80002 after that, which both sites accept in this order.

## libSceMsgDialog: a queue of system messages

All four functions are called from one state machine at `+0x48370`. Its singleton lives at `0x11da700`, and a per-frame update ticks it at `+0x34ab58`. The game pushes (type, user) pairs with `+0x48590`, and the machine shows them one at a time.

- **Open.** Initialize (its result is ignored), then Open with `mode` 3 (SYSTEM_MSG). Param size is 0x88. The base param is 0x30 bytes, with magic = 0xC0D1A109 + the low 32 bits of the param's address. `sysMsgParam->sysMsgType` is the queued type.
  - If Open fails, the game Terminates and drops the message.
  - The message has already been popped by then.
- **Wait.** It polls UpdateStatus until 3 (FINISHED), Terminates, then polls until 0 (NONE).
- **Next.** It reads byte +4 of `sceSystemServiceGetStatus` (isSystemUiOverlaid). The HLE sets it to 0, so the next message follows straight away.
- **Not used.** No user messages, no progress bars, and no GetResult: the button is never read.

Types pushed:

| Type | Message | Pushed at | Pushed when |
|---|---|---|---|
| 2, TRC_PSN_UGC_RESTRICTION | Content from other players is restricted | 11 sites (`+0x33d257`, `+0x36475b`, `+0x37466c`, `+0x38a35a`, `+0x5237a0`, `+0x52f2ad`, `+0x532983`, `+0x532ccd`, `+0x7c33b1`, `+0x96b638`, `+0x8c3b18`) | The NP parental-control state (`[+0x8c2d60()+0x34]`) says UGC is restricted |
| 1, TRC_PSN_CHAT_RESTRICTION | Chat is restricted | `+0x8c3af9` | The same state says chat is restricted |
| 0, TRC_EMPTY_STORE | The store has no content | `+0xafbb5e`, in the in-game Store | Store browsing returned zero categories/products, right after the game's own in-game popup |

**Offline reachability.** `sceNpGetParentalControlInfo` in `np_offline.cpp` reports nothing restricted, and no past run reached MsgDialog. So types 1 and 2 are not expected offline.

Type 0 needs the Store browse to *succeed* with zero items. Whether the offline commerce stubs can do that is not checked, so it's possible but unconfirmed.

## libSceSaveDataDialog

### Profile and level export/import (offline: yes, user-started)

Four jobs of the SaveGameUtil system ("SaveGameOp" classes) use the dialog. Each runs as a loop on a job thread that polls UpdateStatus without sleeping.

| Job | Run method | Message | Started by script native |
|---|---|---|---|
| SlowSaveProfile | `+0x9c710` | 0x41 | `ExportProfile__` (`+0x821d00` → `+0x4f40e0`) |
| SaveLevel | `+0x90a570` | 0x44 | `ExportLevel__Q6SlotID` (`+0x821ca0` → `+0x4f4330`) |
| SlowLoadProfile | `+0x90c4d0` | 0x48 | very likely `ImportProfile__b` (`+0x822e20`; the 0x48 post is at `+0x4f389c`) |
| LoadLevel | `+0x97e490` | 0x45 | `ImportLevel__Q6SlotID` (`+0x822dc0` → `+0x4f4560`) |

The handlers are registered at `+0x4f2f67..0x4f2fef`.

They use helpers that fill params as follows:

- **`+0x909ee0`, LIST** (mode 1). `animParam {userOK=1, userCancel=0}`. `items {userId, titleId=NULL, dirName[] = search results, dirNameNum, newItem, focusPos=4 (DATALATEST), itemStyle=0}`.
  - `newItem` is present only in the export jobs: `{title = a localised string, no icon}`.
  - The helper opens **only if UpdateStatus is NONE**: it Initializes and Opens. Otherwise it returns 0 without opening.
- **`+0x90a3d0`, SYSTEM_MSG** for one save. `animParam {1, 0}`, `items {userId, dirName, dirNameNum = dirName != NULL}`, `sysMsgParam {type, value=0}`.
- **`+0x50560`, SYSTEM_MSG.** `animParam {0, 0}`, optional newItem, `sysMsgParam {type, value}`.
- **`+0x90a150`, PROGRESS_BAR** (mode 5). `items {userId, dirName}` and `progressBarParam {barType 0, msg NULL, sysMsgType 1}`. It doesn't Initialize.

**Export (SlowSaveProfile, SaveLevel)**, dispType 1 (SAVE):

1. **Search.** The game searches save dirs with `"%xPBACKUPx%"` (profile) or `"%LEVEL%"`/`"%ADV%"` (level) and opens the LIST with a new entry.
2. **List.** It polls until FINISHED, then calls GetResult with a dirName buffer and a 0x530 param buffer.
   - `result != 0` (cancel): close.
   - `result == 0` and the dirName is empty (new entry chosen): the game names it `'%s%s%08X'` and saves.
   - `result == 0` and a dirName is set: it opens SYSTEM_MSG 3 (OVERWRITE).
3. **Overwrite.** It polls until FINISHED, then calls GetResult with `dirName = NULL`. It saves **only if `result == 0 && buttonId == 1`**. Anything else closes without saving.
4. **Progress bar.** If the status isn't RUNNING, it Initializes (only from NONE) and opens a PROGRESS_BAR. It loops until UpdateStatus is RUNNING and IsReadyToDisplay is above 0, then mounts and writes.
   - The write functions `+0x9e3d0`/`+0x9e7e0` call ProgressBarSetValue (`+0x9e764`, `+0x9e796`, `+0x9eec6`).
   - The progress bar must stay RUNNING until the game closes it. If it finished by itself, step 4 would reopen it forever.
5. **Close.** If UpdateStatus is not NONE: Close (with a 0x24-byte close param, all zero), then Terminate.

**Import (SlowLoadProfile, LoadLevel)**, dispType 2 (LOAD), same search:

- **No backups found.** It opens SYSTEM_MSG 1 (NODATA) with `+0x50560` and waits for FINISHED.
  - LoadLevel Initializes first (`+0x97e946`) and waits in a `usleep(10 ms)` loop.
  - SlowLoadProfile calls `+0x50560` **without Initialize** (`+0x90c8b0`), so on a real PS4 that Open fails with NOT_INITIALIZED and nothing shows. The implementation does the same.
- **Backups found.** It opens the LIST with no new entry, then calls GetResult. **Only the return value's sign is checked, not `result`.**
  - A cancelled list therefore goes on to open a progress bar and mount the returned dirName.
  - That dirName is empty, so the mount fails (`savedata.cpp` returns PARAMETER for ""), and the job closes.
  - So a cancel must never put a name in `dirName`, and it doesn't.
- **Mount fails with 0x809F000F (BROKEN).** The game closes the progress bar: it calls Close and spins until the status is NONE or FINISHED (`+0x90ca80`, `+0x97e850`). It then opens SYSTEM_MSG 6 (FILE_CORRUPTED) from FINISHED, waits, and closes.
- **Otherwise** it loads (`+0x9e7e0`, with progress), then calls Close and Terminate.

### GameDataWrapper (the main save) (offline: not reachable with today's savedata.cpp)

- **`+0x51500`, mount wrapper.** If `sceSaveDataMount` returns 0x809F000F, it Initializes, unmounts, and opens SYSTEM_MSG type **9**, dispType 2, `items.dirName` = the save.
  - It waits in a `usleep` loop, Terminates, then calls `sceSaveDataCheckBackupData`/`RestoreBackupData`.
  - Type 9 is not in shadPS4's enum. From this context it is the "corrupted, the backup will be restored" message, and its result is never read.
- **`+0x52220`, per-frame no-space machine** (started by `+0x526e0` after a save fails for lack of space).
  - It Initializes, then opens SYSTEM_MSG 4 (NOSPACE) or 8 (NOSPACE_CONTINUABLE), dispType 1, `value` = blocks still needed.
  - It waits for FINISHED (or the game's own popup), retries the mount, and Terminates.
  - For type 4 it loops back to the dialog while there's still no space. The result is not read.
- **Why unreachable.** `savedata.cpp` never returns BROKEN (0x809F000F) or the no-space codes (0x809F0009/0x809F000A). The HLE can't be made to report them without a change there.

## libSceGameCustomDataDialog (online only)

- **Callers.** `+0xadeda0` (send, mode 1) and `+0xadf040` (mode 2), in `sceNpBasicWrapper_Orbis.cpp`. They're reached from the session/invitation code around `+0x384678` (Match2, ReqJoin, HOME_GAMELAUNCH_SESSION) and `+0x38bf31`.
- **Open.** Initialize, then Open. On success it sets a "dialog open" flag.
- **Update.** `+0xaddf40` (from `+0xaddd76`) runs every frame. While the flag is set it waits for UpdateStatus to be 3, then calls GetResult. It reads `int32` at **+0x0C**: 0 → success, 1 → 0x8002a671 (cancelled), else 0x8002a663. It calls back with the code (event 7 or 8), then Close and Terminate, and clears the flag.
- **Why a stub isn't enough.** A stub returning 0 leaves the flag set forever: later Opens return 0x8002b105 (busy), and the callback the sender waits on never comes.
- **What it does now.** It finishes at once as cancelled: Open → FINISHED, and GetResult writes 1 at +0x0C. It is not expected offline: it needs a PSN session.

## Implementation

- **`runtime/src/hle/common_dialog.{h,cpp}`: the four libraries.**
  - Structs follow shadPS4's layouts, checked against the call sites above (MsgDialog param 0x88, SaveDataDialog param 0x98, items 0x60, result 0x48).
  - Rules modelled from a PS4:
    - `sceCommonDialogInitialize` must come first, otherwise 0x80B80001.
    - One common dialog library at a time (MsgDialog or SaveDataDialog); the other gets BUSY 0x80B80008 until Terminate.
    - A second Initialize gets 0x80B80004.
    - Open needs INITIALIZED or FINISHED: NONE gives 0x80B80003 and RUNNING gives BUSY. Open checks the param size, the base size and the magic (0x80B8000A otherwise).
    - GetResult before FINISHED gives 0x80B80005. Close when not RUNNING gives 0x80B8000B.
    - Close flips RUNNING → FINISHED before it returns.
    - Terminate always returns to NONE and releases only its own hold.
    - The status is NONE/INITIALIZED/RUNNING/FINISHED, and UpdateStatus is a cheap poll that never logs.
  - Every open takes the modal gate (`modal::Begin("msg"/"savedata")`), and it is released exactly once on answer, Close or Terminate.
  - Log lines: `[dialog] savedata: list (load), 3 saved data`, `[dialog] savedata: system message 3: Do you want to overwrite this saved data?`, `[dialog] savedata: answered (result 1, button 0)`, `[dialog] savedata: closed by the game`, `[dialog] msg: system message 2: ...`, `[dialog] savedata: LBP3_AUTOPRESS answers "No"`.
- **`runtime/src/hle/dialog_window.{h,cpp}`: the native window.** A Win32 dialog built from an in-memory template, one thread per dialog, so the game keeps running.
  - Message: icon, text, one or two buttons. List: a list box with Load/Save/Delete and Cancel. Progress: text and a progress bar, which doesn't take focus and has no close box.
  - Windows are Segoe UI 9, topmost, and centred over the game's SDL window. That window is found in this process only, by its `SDL_app` class; with no game window they're centred on the work area.
  - Mouse, Enter and Esc work as in any Windows dialog. The close box acts as Cancel where there is one.
- **`runtime/src/hle/dialogs.cpp`.** `RegisterDialogs` also registers the table from `common_dialog::Exports()`. ErrorDialog is unchanged (only its WIN32_LEAN_AND_MEAN redefinition warning is fixed).
- **`runtime/CMakeLists.txt`.** The new sources, `comctl32` (progress bar), and the `dialogtest` exe in the SDL3 copy loop.

### Texts

- Messages the game supplies (user messages, the new entry's title, progress messages) are shown as given, in UTF-8.
- System types get plain English. For example: "Content created by other players is restricted for this account by its parental control settings.", "There is no saved data.", "Do you want to overwrite this saved data?" (with "Saved data: <dir>"), "The saved data is corrupted and can't be used.", and "There is not enough free space to save. At least 100 MB more is needed." (value × 32 KiB).
- List entries show the dir name and the save folder's last-modified time. Titles aren't available because `sceSaveDataSetParam` doesn't keep them.

### Results (SDK 2.00 behaviour, as shadPS4 models firmware before 4.50)

**SaveDataDialog**

| Dialog | Answer | Result |
|---|---|---|
| LIST | Choose an entry | `result 0`, `dirName` = its name (the new entry gives "") |
| LIST | Cancel | `result 1`, `dirName` "" |
| CONFIRM/OVERWRITE | Yes | `result 0`, `buttonId 1` |
| CONFIRM/OVERWRITE | No | `result 1` (USER_CANCELED), `buttonId 0` |
| NODATA, NOSPACE, NOSPACE_CONTINUABLE, FILE_CORRUPTED, FINISHED | OK | `result 1` (as shadPS4 notes for SDK < 4.50) |
| Type 9, TOTAL_SIZE_EXCEEDED | OK | `result 0`, `buttonId 1` |
| Corrupted 10/11/13 | OK | `result 0`, `buttonId 1` |
| Corrupted 10/11/13 | Cancel | `result 1` |
| Any | Closed by the game | `result 0`, `buttonId 0` |

`GetResult` fills `dirName` and `userData` when given and leaves `param` untouched.

**MsgDialog:** system messages give `buttonId 1`. User messages follow shadPS4: the second button is NO for YESNO and TWO_BUTTONS, and USER_CANCELED for the OK/Cancel kinds.

### Pad

- The pad is read from UpdateStatus, so on the thread that polls. Reads are throttled to one per 10 ms, through `input::Read(0)`, which is unaffected by the modal gate. Only fresh presses count: a button held when the dialog opens must be released first.
- **Cross** presses the focused button, which starts on the default.
- **Circle** cancels: No, Cancel, or OK on single-button messages.
- **D-pad** up/down moves the list selection. In message dialogs the D-pad moves the focus between the buttons.

Default buttons:

- **OVERWRITE:** **No**. The destructive answer is never the default.
- **CONFIRM:** Yes, except for dispType DELETE (No).
- **Corrupted 10/11:** Cancel.
- **Lists:** the entry `focusPos` asks for. DATALATEST is the newest folder by modification time; if no folder has a time, the first data entry. A real PS4 also opens on the latest save.

## LBP3_AUTOPRESS

`AutoPressCross` mixes synthetic presses into `input::Read`, so under `LBP3_AUTOPRESS` the dialogs ignore the pad completely. After 1 s they press a fixed answer, chosen as the one that changes nothing:

| Dialog | Auto-answer | Why |
|---|---|---|
| OVERWRITE, CONFIRM, YESNO user messages | **No** | Yes overwrites, or saves/loads/deletes |
| Every LIST (export and import) | **Cancel** | Import replaces the profile or level in memory. Export could overwrite, or at least leaves new backup saves behind on every scripted run. |
| Corrupted 10/11/13, OK/Cancel user messages | **Cancel** | OK deletes or recreates the save |
| TWO_BUTTONS | Dismiss (cancel) | It has no known "safe" button |
| OK-only messages (all MsgDialog system messages, NODATA, NOSPACE, FILE_CORRUPTED, type 9, error codes) | **OK** | It's the only answer |
| Progress bars, NONE/WAIT messages | Nothing | The game closes them |

Remote input (`LBP3_REMOTE_INPUT`) is a real pad as far as dialogs are concerned, and the auto-answer policy does not apply to it. A test agent can answer with `cross`, `circle` and `down`; the remote queue leaves 60 ms between presses, so repeated presses register.

So a blind Cross from a remote-input script (`tools/nav.py` presses Cross on unknown screens "to dismiss dialogs") presses the focused button:

- On an import list it **loads the newest backup**.
- On an export list it lands on the newest backup, and the OVERWRITE question that follows defaults to No.

Scripts that might reach these menus should press Circle on unknown screens, or run with `LBP3_AUTOPRESS`.

## Fix outside the dialog files: save dir search

`savedata.cpp`'s `Matches` treated `%` as "prefix then anything". The import searches `"%xPBACKUPx%"`, `"%LEVEL%"` and `"%ADV%"` have an empty prefix, so they matched every save. The import list would then have offered `LBPxSAVE` (the main save) and `LBPxSAVExLITTLE` as profile backups, and NODATA could never show.

`%` now matches any run anywhere. The empty pattern, the only one past runs used (171 times), still matches everything. The same file's deprecated `u8path` call is replaced.

This is a separate commit (`d09681e`), but it can't be dropped on its own: the harness's import-list-on-disk test expects the search to find only the 3 backups and fails without it.

## Fix outside the dialog files: one pad reader at a time

Dialogs read the pad from the thread that polls UpdateStatus. For the export/import jobs that is a job thread, while the game's own thread keeps calling `scePadReadState`. `input::Read` runs `SDL_UpdateGamepads` and `RefreshSlots`, and `RefreshSlots` closes and reopens `g_pads[]` entries without a lock, so unplugging a pad while a dialog was up could have used a closed handle.

`input::Read` now takes a static mutex: four lines at its top in `gamepad_sdl.cpp`, nothing else there changed (commit `ab1193b`). `SetVibration`/`SetLightBar` still use `g_pads` unlocked, as before.

## Tests

`runtime/tests/dialogtest.cpp` builds as `build/dialogtest.exe`. It fills params byte-for-byte like the call sites above and answers the windows with real window messages: `WM_COMMAND`/`BN_CLICKED` from the button, Esc's `IDCANCEL`, `WM_CLOSE`, and `LB_SETCURSEL`. It also uses a scripted pad (a reader hook in `dialog_window.h`). `--capture <folder>` saves each dialog as a .bmp.

Covered:

- **Initialisation and busy rules.** Both CommonDialog boot calls; NOT_SYSTEM_INITIALIZED; BUSY in both directions; ALREADY_INITIALIZED; Open without Initialize; bad magic and bad size; Open while running; GetResult before FINISHED.
- **MsgDialog.** The queue sequence (Open, poll, Terminate, NONE) for the UGC message and the empty-store message (by Esc), plus user message results.
- **Export.** The new entry; an existing entry; OVERWRITE answered No by Cross (the default) and Yes by click, with GetResult given a NULL dirName; the progress bar (SetValue 40 seen on the control, a bad target rejected, Cross and Circle ignored, still RUNNING); Close flipping to FINISHED at once, a second Close giving NOT_RUNNING; then the state-10 Close and Terminate.
- **Import.** Cancel by Esc and by the close box (empty dirName); the pad, where a held Cross doesn't count, then Down and Cross pick entry 2; the corrupted path (progress, the Close spin loop, FILE_CORRUPTED from FINISHED); NODATA by Circle.
- **Import list on disk.** Real save folders in a temp user root: the `%xPBACKUPx%` search finds the 3 backups and not the main saves or a level export, and the list opens on the newest one with its date.
- **Boot save messages.** Type 9 (via the close box) and NOSPACE with "100 MB".
- **LBP3_AUTOPRESS.** CONFIRM answered No despite repeated synthetic Crosses; OVERWRITE not answered Yes; both lists cancelled; NODATA OK; the progress bar left RUNNING, then Terminate while open; MsgDialog OK.
- **GameCustomDataDialog.** Finished, with 1 at +0x0C.
- **Registration.** Every NID the game imports from these four libraries is in the export table under the right library name (NIDs from `eboot_imports.json`, hashed from the registered names), and `RegisterDialogs` runs on a real `core::Linker`.
- **Modal gate.** Released after every scenario.
- **Centring.** An off-centre 800×600 SDL window stands in for the game. Its class is `SDL_app`, and the dialog's centre lands within 2 px of the window's centre. The dialog is topmost.
- **The real `LBP3_AUTOPRESS` switch.** The exe runs itself again with `LBP3_AUTOPRESS=cross` in the environment and `--autopress-env`. The child leaves the test override alone, so the `getenv` path is what decides, and it repeats the auto-answer checks (47 checks). The parent checks the child's exit code.

Result: **275 checks in the main run and 47 in the `LBP3_AUTOPRESS` child, 0 failed.** The build has no new warnings.

The captured windows were looked at: layout, icons, default-button highlight, the list with a new entry, and the progress bar.

## Open questions

- **ImportProfile → SlowLoadProfile.** The mapping is inferred: message 0x48 is posted at `+0x4f389c`, but the native's path through `+0x4e01b0` wasn't traced to it.
- **Menu labels.** The labels for the four export/import entries were not checked in-game.
- **Empty-store message offline.** Whether TRC_EMPTY_STORE can show offline depends on how the offline commerce stubs answer a Store browse. Not checked.
- **SDK < 4.50 results.** These follow shadPS4. LBP3 treats `result 1` and `buttonId 2` the same at every site that reads them, so either convention works for this game.
- **Common dialog hold.** The hold is shared by MsgDialog and SaveDataDialog only. ImeDialog (another lane) has its own state; on a PS4 it is a common dialog too.
- **Nothing ran in the game.** This lane must not launch it. The windows, results and pad handling are checked only by the harness.
- **New-entry focus in export lists.** DATALATEST focuses the newest existing backup, so Cross then Cross on an export lands on the OVERWRITE question (default No), not the new entry. That is what a PS4 does.

## In-game check to run

1. **Start.** Launch normally, without `LBP3_AUTOPRESS`, with a profile that has at least one level on My Moon.
2. **Export profile.** Find "Export profile" (likely under the profile/options menus) or "Export level" (a level's options on My Moon).
   - Expect a list window over the game: `[dialog] savedata: list (save), N saved data + new`.
   - Choose `[New saved data]` and press Cross. Expect `[dialog] savedata: answered (result 0, button 0)`, then `[dialog] savedata: progress bar: Saving...` and `[dialog] savedata: closed by the game`.
   - A new folder `user\savedata\CUSA00473\LBPxSAVExPBACKUPx...` should appear (profile).
3. **Export again onto the same entry.**
   - Expect `[dialog] savedata: system message 3: Do you want to overwrite this saved data?` with No focused.
   - Circle should give `answered (result 1, button 0)` and nothing written. Yes should give `answered (result 0, button 1)` and a save.
4. **Import profile / Import level.** Expect a list of only the matching backups (no `LBPxSAVE`), opening on the newest.
   - Cancel should give `result 1`, a brief "Loading..." and `closed by the game`, with nothing loaded.
   - With no backups present, expect `[dialog] savedata: system message 1: There is no saved data.` from LoadLevel. SlowLoadProfile shows nothing (it doesn't Initialize; same as a PS4).
5. **While any dialog is up.** Expect `[modal] savedata opened (1 open)`: the game ignores the pad, and the dialog takes Cross, Circle and the D-pad. Then `[modal] closed (0 open)`.
   - The window should appear above the game window, centred on it, in windowed and in borderless full screen.
   - Answer at least one dialog with a **real pad** (not remote input).
   - When it closes, the game window should get the keyboard back. The "Saving..." window doesn't take focus at all.
6. **Boot.** Should still log no `[stub] libSceCommonDialog` line and no fatal error. The second `sceCommonDialogInitialize` gets 0x80B80002, which the NP setup accepts, and `sceNpWebApiInitialize` follows.
