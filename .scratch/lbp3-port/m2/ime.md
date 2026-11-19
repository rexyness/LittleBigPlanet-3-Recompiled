# Milestone 2, gap 12: text entry (libSceImeDialog)

Worktree `D:\Recomps\lbp3-wt\ime`, branch `fix/ime-dialog` (from `m2/scaffold` `baab2bf`), commits `adbe927` (the library, remote commands, harness) and `6820629` (Enter/Esc held across the dialog, pad cases in the harness). Not run in the game yet (another agent had the game); tested offline with `build\imetest.exe`.

## How the game drives the dialog (eboot.bin offsets)

Imports and PLT thunks: Init `0xe0faf0`, Abort `0xe0fb00`, GetStatus `0xe0fb10`, GetResult `0xe0fb20`, Term `0xe0fb30` (GOT `0x10cd840`..`0x10cd860`). libSceIme: KeyboardOpen `0xe0faa0`, KeyboardClose `0xe0fac0`, Update `0xe0fad0`.

All ImeDialog calls sit in one module around `0x559000`:

- Requests go into a table of 4 entries of `0x50` bytes at `0x13abd80`. `0x559710` adds one: (user, title, initial text, callback, context, direct flag, max length). Entry fields: +0 used, +1 pending, +2 direct callback, +3 local user, +4 user, +8 callback, +0x10 context, +0x18 title string, +0x30 text string, +0x48 max length.
- `0x559000` (each frame, from `0x649a3`) waits 3 frames, then `0x559060` opens the pending entry. That is the only `sceImeDialogInit` call (`0x55918f`).
- `0x559800` (each frame, from `0x6fe1e`, while byte `0x13abec4` is 1, which it is from boot) is the only GetStatus call. It returns unless the status is >= 2. At exactly 2 it zeroes a 16-byte result, calls GetResult (return value not checked), calls `0x55c8e0`, takes the text from its buffer only if `endstatus == 0` (otherwise an empty string), calls Term, then the request's callback with (text, cancelled = endstatus != 0). A status above 2 counts as a cancel, with no GetResult or Term.
- Abort is called at `0x55921b`, `0x559947`, `0x5599f7`, `0x559b5c` and `0x559d1c`, when the requester goes away (player leaves, the requesting object or menu closes, shutdown). Each one calls the callback at once with "" and cancelled. GetResult and Term come from the next poll, which sees FINISHED.
- If Init returns non-zero, the callback gets "" and cancelled.

### The param as `0x559060` fills it

A `0x60`-byte stack struct, zeroed with three 32-byte stores, then:

| offset | field | value |
|---|---|---|
| +0x00 | userId | `[player + 0xe8]` |
| +0x08 | supportedLanguages | one bit, from table `0xe1e500` indexed by the system language (English US = `0x4`) |
| +0x24 | maxTextLength | the request's max length, or `0x200` if that is 0 |
| +0x28 | inputTextBuffer | global `0x13abed0`, prefilled from the request's text (copy limited to `0x201` units; empty if there is none) |
| +0x30 / +0x34 | posx / posy | 960.0 / 540.0 |
| +0x38 / +0x3c | horizontal / vertical alignment | 1 / 1 (centre) |
| +0x48 | title | the request's title (UTF-16; the localised strings start with U+FEFF) |

Everything else stays zero: type (default), enter label (default), input method, text filter (none), option (0: single line, no password), placeholder (none). `extended` is NULL. So no filter callback and no extended parameters reach the library; nothing in game code would need calling from a host thread.

## Which features open it

Titles are translation keys, looked up in `gamedata/languages/american.trans` (extracted with `tools\farc.py`; the format is a big-endian count, then (key, offset) pairs, then UTF-16BE strings).

1. **Text chat**, requests at `0x21a3d7`, `0x21a536` and `0x55ddb5`. Title key `0x3f939659`, "Enter Your Message". Max length 96. The `0x55ddb5` request is prefilled from the chat input object. Chat belongs to online play, so it is probably not reachable offline (inferred, not checked).
2. **Popit text entry in Create mode**: the request at `0x70bc40`, in `0x70aef0` (a virtual; its vtable entry is at `0x1089e70`). Scripts start it with `BeginTextEntry` (native `0x823e80`) and an `EPoppetTextEntry` type. The max length is the global `0x1062f34`, which is 256. Titles by type:
   - 2: "Speech" (speech bubble)
   - 3, 5, 10, 11: "Item Name"
   - 4, 13: "Note"
   - 7: "Sequencer Name"
   - 8: "Prompt Name"
   - 9: "Actor Name"
   - 14: "Suffix"
   - 6 and 12 take their titles elsewhere.

   Scripts that use it: `trigger_speech_bubble`, `post_it_note`, `progress_board`, `microchip`, `circuitboard`, `controlinator`, `controlpad`, `tweakable`, `tweakinstrument`, `tweaksequencer`, `tweaksequencergame`, `tweakquestchip`, `poppet`.
3. **Menu text fields** (the scripts' Gooey `DoTextEntry*`, `DoSpaceButtonTextEntry*`, `DoListButtonTextEntry`, `EndFrameTextEntry`, `GetTextEntryString`, `CancelTextEntry`). A menu object at +`0x2a0` is built by `0x559a70` (called from `0x647192`, `0x64cc06` and `0x64d606`) and sent through `0x650eb8` → `0x559c50` → `0x559710`. The title and max length come from the script. Scripts that use it:
   - `podstatetweaklevel` (level settings: name, description)
   - `leveldetailspage`
   - `podstatesearchlist` (search)
   - `podstatecommunitycarouselmenu`
   - `podstatestore`, `podstatestoremenu`
   - `podstateplaylist`
   - `podstatetweakplayer`
   - `commentset`, `reviewlist`
   - `modaloverlaystategriefreport`
   - `questlogpage`
   - `inventoryinfobubble`, `inventoryinfocategorybubble`
   - `adventureitems`

The Init in the human session (`analysis\play_log.1.txt:665`, Pod menus) was most likely one of the menu fields (inferred from where the session was, not from the log). Which one isn't in the log. With the stubs, Init returned 0 and GetStatus stayed 0, so that field never got an answer.

## libSceIme (keyboard)

- KeyboardOpen is called at boot (`0x4bddd7`) and again when a reopen flag is set (`0x4be1a3`). The param has option 1 (repeat) and handler `0x4bde30`. The boot call is retried in a loop while it returns `0x80BC0008`, so that code must never be returned.
- `sceImeUpdate(0x4bde30)` runs every frame (`0x4be1ba`).
- KeyboardClose is called at `0x4be037` and `0x4be354`.

All three now return OK and send no events. The handler would deliver USB keyboard typing (for chat). On PC the keyboard reaches the game as actions and through this dialog instead. One `[ime] keyboard opened for user N` line is logged per open.

## What's implemented (`runtime/src/hle/ime_dialog.cpp`, `ime_dialog.h`)

**Status and results.**
- GetStatus returns NONE (0) when idle, with a bare atomic load.
- Init moves to RUNNING (1). The status becomes FINISHED (2) when the window closes or Abort is called.
- GetResult reports OK (0), user cancelled (1) or aborted (2).
- Term goes back to NONE, and only from FINISHED.

**Errors.**
- BUSY `0x80BC0001`: Init while RUNNING, or while FINISHED before Term.
- NOT_IN_USE `0x80BC0107`: GetResult, Abort or Term while idle.
- NOT_FINISHED `0x80BC0106`: GetResult or Term while RUNNING.
- NOT_RUNNING `0x80BC0105`: Abort after the dialog finished.
- INVALID_ADDRESS `0x80BC0031`: a null param or result pointer.
- INVALID_INPUT_TEXT_BUFFER `0x80BC0017`: a null text buffer.
- INVALID_MAX_TEXT_LENGTH `0x80BC0016`: a max length of 0 or over 2048.

Every refused Init is logged.

**Text written back.**
- The text goes into the game's buffer on a game thread, at the FINISHED transition (in GetStatus or GetResult), and only on OK.
- It is clamped to the max length in UTF-16 units, without splitting a surrogate pair, and null-terminated. The library never writes more than max + 1 units.
- Cancel and abort leave the buffer as the game left it.
- Each dialog is a session object shared with its window thread. A late-closing aborted window only touches its own session, so it can't overwrite the next dialog's text.

**The window.**
- A native window on its own thread. The game keeps running.
- Contents: the title (BOM stripped) above an edit box prefilled with the game's text, caret at the end, an "n / max" counter, and OK and Cancel buttons. The OK label follows the enter label (OK, Send, Search, Go).
- `EM_SETLIMITTEXT` caps typing at the max length.
- Enter confirms and Esc cancels, through `IsDialogMessage`, so IME composition keeps its own Enter. The close box also cancels.
- Uses the system message font and scales with DPI (checked at 120 dpi).
- Windows' IME works, because it's a standard EDIT control.
- Multiline, password and number fields are honoured if a game asks for them. LBP3 never does.
- Placement: centred over the game window (the biggest visible top-level window of the process that isn't a console or a dialog), kept on the monitor's work area, topmost and brought to the foreground. With no game window (the harness) it is centred on the work area, not topmost and not activated.

**The pad.**
- GetStatus, which runs on a game thread, reads the real pad with `input::Read(0)` while the dialog is running.
- A fresh Cross press confirms and a fresh Circle press cancels. A press that was already held when the dialog opened doesn't count.

**The modal gate.**
- `hle::modal::Begin("ime")` is called at Init. End is called once, from GetStatus: after the dialog has finished, and when Cross and Circle read released and neither Enter nor Esc is down (`GetAsyncKeyState`), or 1 s after it finished.
- The game saw a neutral pad while the dialog was up. A Cross still held from confirming would otherwise reach the game as a fresh press and reopen the field.
- Enter and Esc are the keyboard's Confirm and Back actions. An Enter still held when the game window gets the focus back would probably reach SDL as a key-down and the game as Confirm. This is inferred, not seen; holding the gate covers it.
- If the next Init comes while the gate is still up, the gate is kept and not opened twice.

**Keys held into the dialog.**
- Enter selects a menu item, so it may still be down when the dialog takes the focus. The dialog then gets only auto-repeat key-downs (bit 30 set).
- The window ignores repeated Enter and Esc key-downs, so a held Enter can't confirm the prefill at once. A fresh press works as normal.

**Scripted input** (`hle::ime_dialog::ScriptConfirm(utf8)` and `ScriptCancel()`): queued actions. Each one applies to the open dialog, or the next one to open, 150 ms after its window appears. Confirm puts the text in the edit box in place of what's there and goes through the same OK path, including the clamp.

**Remote control** (`input_cmd.txt`, `LBP3_REMOTE_INPUT=1`), documented above `remote::Parse`:
- `ime <text>`: the rest of the line exactly as written (one separator removed, CR stripped), in UTF-8.
- `ime-cancel`.

**Log lines:**
- `[ime] dialog opened: "<title>", up to N characters, text "<prefill>" (type T, options O)`
- `[modal] ime opened (1 open)`
- `[ime] Cross on the pad: confirm` / `[ime] Circle on the pad: cancel` / `[ime] scripted: typing "..." and confirming`
- `[ime] dialog confirmed: "<text>" (N characters)` / `[ime] dialog cancelled` / `[ime] dialog aborted by the game`
- `[modal] closed (0 open)`

## Tests

`runtime/tests/imetest.cpp` → `build\imetest.exe`, linked like padtest. It fills the param the way `0x559060` does, polls and finishes the way `0x559800` does, and drives the window with window messages.

Results: 244 checks in the main process, plus 11 in each of the two pad child processes. All pass, about 7 s a run. With the final code it passed 10 runs in a row. Earlier there was one failed run (4 checks) out of about 25. Its output was overwritten before I read it, so the cause isn't known. The likely cause is the new Enter/Esc condition: `GetAsyncKeyState` reads this PC's real keyboard, so someone typing elsewhere during the run holds the gate. After that run the gate check in the harness waits up to the 1 s cap instead of checking once.

What it covers:

- Idle: GetStatus is NONE 10 000 times in a row; GetResult, Abort and Term return NOT_IN_USE; the gate is down.
- Bad params are refused and open nothing.
- Status sequence NONE → RUNNING → FINISHED → NONE through the scripted API; the text is in the buffer when FINISHED is reported; Init while FINISHED returns BUSY; Abort after finish returns NOT_RUNNING; Term leaves the buffer alone.
- Real window:
  - the prefill and the title (BOM stripped) are shown, the OK label is right, `EM_GETLIMITTEXT` is 96
  - while running: Init returns BUSY, GetResult and Term return NOT_FINISHED, the gate is up
  - `WM_SETTEXT` then an OK click (`WM_COMMAND IDOK`) confirms
- Auto-repeated Enter and Esc key-downs (bit 30 set) don't close the dialog. A mutation run with that filter disabled fails this check.
- Enter (`WM_KEYDOWN VK_RETURN` to the edit box) confirms. Esc, the Cancel button and `WM_CLOSE` cancel, and the buffer keeps the game's text, with the guard intact past max + 1.
- Scripted cancel.
- Abort while running: FINISHED at once, a second Abort returns NOT_RUNNING, Init returns BUSY, the result is ABORTED, the buffer is kept, the window goes away. Also abort before the window exists, and Init right after an abort's Term (the new dialog gets the script, the old one writes nothing).
- Max length:
  - 13 characters into 8, by script and by window, give 8, with nothing written past index 8
  - a prefill longer than the field is shown cut
  - with U+1F600 straddling a limit of 5 the result is "abcd"; with a limit of 6 the pair is kept whole
- UTF-16 round trip: a non-ASCII prefill is shown exactly; non-ASCII text in through UTF-8 script and through the window (Latin-1, CJK, Greek, Arabic, Hangul, emoji) comes back exactly.
- Remote control: `ime  two spaces...\r` keeps the text exactly, UTF-8 `ime Café ☕` works, and `ime-cancel` cancels.
- The modal gate is balanced (down) after every case.
- Pad, in child processes (`imetest --pad cross|circle`, which the main run starts). `LBP3_AUTOPRESS` is read once per process. With it, `input::Init()` and a throwaway working folder for `input.ini`, the pad goes through `input::Read` as in a game run. The tap already held at open is ignored and the next tap confirms (Cross) or cancels (Circle), with the prefill kept. Right after FINISHED the button is still held and the gate is still up; the gate never comes down while the button reads held, and comes down within 1.5 s.

`imetest --manual` makes itself per-monitor DPI aware (as SDL3 does for the game), opens one dialog like text chat's and waits for a person. Use it to try the layout, typing and Windows' IME by hand. It prints the end status and the buffer.

## Limits and open questions

Never ran:
- Registration and linking inside `lbp3.exe`. The NIDs that `HLE_REG` hashes match the eboot's imports (`tools\ps4_nid.py` for all eight names), but no game run has linked them.
- The window's over-the-game branch: centring on the game window, topmost, `SetForegroundWindow`. The harness has no game window, so it only ran the work-area branch.
- IME composition (CJK). The edit box is a standard EDIT control, so it should work, but nobody has typed into it with an IME.
- A physical pad. The pad cases used `LBP3_AUTOPRESS` through `input::Read`, the same path, but no hardware.

Other:
- There's no on-screen keyboard. A pad-only player can confirm the prefilled text with Cross or cancel with Circle; typing needs a keyboard (or Windows' touch keyboard).
- `LBP3_AUTOPRESS` taps Cross, so automated runs with it confirm any dialog's prefilled text after up to 2 s. The remote `cross` command does the same.
- The window is topmost while the game is visible, so it stays above other apps if the player alt-tabs away while it is open.
- Which menu opened the dialog at `play_log.1.txt:665` is unknown.
- `0x55de80` (before Init) and `0x55c8e0` (after GetResult and Abort) act on an object at `[[0x119a588]] + 0x450`. They look like pause and resume of something (input or UI). Not needed here.

## In-game check to do

**Route A, a menu field, offline.** Pod → My Moon (or the level list) → a level → its details or settings (`podstatetweaklevel` / `leveldetailspage`) → the name field. With remote input, queue `ime IME test level` and then press Cross on the field. Expected in stderr:

```
[ime] scripted text queued: "IME test level"
[ime] dialog opened: "<title>", up to N characters, text "<old name>" (type 0, options 0)
[modal] ime opened (1 open)
[ime] scripted: typing "IME test level" and confirming
[ime] dialog confirmed: "IME test level" (14 characters)
[modal] closed (0 open)
```

The new name should then show in the menu. There should be no `[stub] libSceImeDialog` or `[stub] libSceIme` lines; the only Ime line at boot is `[ime] keyboard opened for user 1 ...`.

**Route B, Create mode.** In an Edit Now level, open the Popit, place a Speech Bubble (or a sticky note or microchip), tweak it with Square and choose its text. The dialog title is "Speech", "Note" or "Item Name", with a max length of 256. The confirmed text should show on the object.

**Remote-input hazards.**
- After the Cross that opens the field, queue `wait 500` before anything else. A second `cross` queued right behind it lands as a fresh press about 210 ms later. That races the 150 ms script delay, confirms the old text, and leaves the `ime` line waiting.
- `ime` lines that aren't used stay queued and fill the next dialog that opens.
- Don't run with `LBP3_AUTOPRESS`: it confirms the prefill.

**By hand, with a pad.**
1. Open a field, type with the keyboard, press Enter: the text is taken.
2. Open it again and press Cross, keeping it held for a second. The dialog must close, `[modal] closed` should appear only after release, and the field must not reopen.
   - Do the same with Enter: confirm with Enter, hold it a second. The field must not reopen.
   - Select the field with Enter and keep holding it as the dialog opens. The dialog must stay open with the prefill until a fresh Enter.
3. Open it again and press Circle: cancelled, with the old text kept.
4. Optionally switch to a Japanese IME and type kana to kanji in the box.
