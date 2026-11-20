# Where does LBP3 Recompiled stand against the offline finish line?

Type: research
Status: resolved
Claimed by: research agent (charting session, 2026-11-09)
Findings: branch `research/base-status`, file `docs/research/base-status.md`
Blocked by: none
Map: [LBP3 native port](../map.md)

## Question

For each part of the **offline finish line**, what is the **base**'s current status according to its own record, as of upstream commit `2026-10-28` on [PurpleGitty/LittleBigPlanet-3-Recompiled](https://github.com/PurpleGitty/LittleBigPlanet-3-Recompiled)? The record is its `NOTES.md` (about 680 lines of working notes), its code (`runtime/`, especially `runtime/src/hle`), its `tools/`, and its shadPS4 fork ([PurpleGitty/shadPS4, branch `lbp3`](https://github.com/PurpleGitty/shadPS4/tree/lbp3), based on shadPS4 `0c9ca52`).

Give each part one status: **works** (with evidence), **known broken** (quote the note), **untested**, or **unknown**. The parts:

- Boot, the intro video (`libSceAvPlayer`, `gamedata_orbis/videos`), and the network dialog on the way into the game
- The Pod and its menus
- Story mode, world by world. The notes reach the Prologue (Introduction, Needlepoint Peaks). Say which worlds and playable characters nobody has reached.
- Create mode: tools, gadgets and logic, text entry (`libSceIme`/`libSceImeDialog`), and the Thruster freeze
- Popit, stickers and photos (`libSceJpegDec`, `libSceJpegEnc`, `libScePngDec`). The notes call JpegDec "needs a real decoder".
- Audio: FMOD through `libSceAudioOut`, streamed music, the game's sound banks
- Saves: `libSceSaveData` and its dialog, where saves land on disk, and whether save/reload has been tried
- Controller: SDL3 pads, touchpad and motion
- Frame pacing and performance: the 30 fps cap, hitches, camera snapping, being launched out of the level
- Any stubbed or partly implemented libraries the offline path touches (the notes give 590 stubbed functions at link time; which matter?)

Also find out:

- **Real-PS4 baseline:** what LBP3 does offline on real hardware, so later tickets can tell a port bug from expected behaviour. For example, is "a problem with your network connection" on every boot normal offline? (Web sources, cited.)
- **Dump layout:** what upstream expects. Its README asks for separate `CUSA00473` and `CUSA00473-patch` folders, while this **game dump** is merged (base and patch in one folder, `patch_001.farc` alongside `base_001.farc`). Do `game_dir.txt` and `LBP3_GAME_DIR` handle a merged dump, or does the loader assume two folders?
- **Port family fixes:** any fixes from the port family (gravityrush_pc, issn_pc, iss_pc, bloodborne_pc) that clearly match an open LBP3 problem.

## Answer shape

`docs/research/base-status.md` on the `research/base-status` branch, with a status table (part → status → evidence/quote) and a short list of the open problems as the notes describe them. Cite each claim: file and line in upstream, a commit, or a URL. Reading only: building and running belong to [Build LBP3 Recompiled and run it on this dump](02-baseline-run.md).

## Answer

Upstream `dfc492a`, shadPS4 fork `4640726` (1 commit on top of `0c9ca52`, 7 files in `video_core`). `NOTES.md:N` means upstream's file at that commit.

| Part | Status | Evidence |
|---|---|---|
| Boot to the Pod | works | 14 s to the Pod (`NOTES.md:524`); some boots stall 30-40 s (`:368-370`) |
| Intro video | works, skipped by default | Bink decoded by the game, not AvPlayer (`NOTES.md:235-236`, `:254`); `play.bat:7` skips it |
| Network dialog | works | The game's own "problem with your network connection" dialog, then offline (`NOTES.md:413-415`) |
| Pod and menus | works | "the Pod, menus and planet map run smoothly" (`NOTES.md:457`) |
| Story: Introduction | works | Locked 33.3 ms while walking (`NOTES.md:463-470`) |
| Story: The Legend of Bunkum | known broken, auto-skipped | White checkpoint, "Real cause still open" (`NOTES.md:568-596`); watchdog skip in `game_state.cpp:86-122` |
| Story: Needlepoint Peaks | works, with a known problem | Sackboy launched out of the level, "Recheck after the fix" (`NOTES.md:607-610`) |
| Story: everything after it | untested | No note reaches the rest of the opening, Manglewood, the Ziggurat, Bunkum Lagoon or the finale. Oddsock, Toggle and Swoop have never been played |
| Create: entering and editing | works | "Create mode works (Oct 11 user report)" (`NOTES.md:659`) |
| Create: tools, gadgets, logic | untested beyond two gadgets | Only the Thruster and rockets appear (`NOTES.md:471-495`) |
| Create: Thruster freeze | works (fixed per notes) | "Thrusters fixed by that" (Oct 14, `NOTES.md:481`), after the Oct 11 list that calls it open (`:665`) |
| Create: camera snapping, sky | known broken | Snapping "is game-side" (`NOTES.md:637-645`); sky "yellow/beige" (`:663-664`) |
| Text entry (Ime, ImeDialog) | untested | All 8 imported functions stubbed |
| Popit | works (opens) | `NOTES.md:353-363` |
| Stickers and photos | untested | JpegDec 5/5 and JpegEnc 4/4 stubbed; no PngDec function is imported |
| Audio, banks, music | works | "confirmed working by ear" (`NOTES.md:247`); banks (`:263-264`); music (`:558`) |
| Saves | works | Plain folders in `user\savedata\CUSA00473\<dir>\` under the project folder (`savedata.cpp:1-3`, `:43-47`); the game writes them (`NOTES.md:517-518`) |
| Save dialog | untested | `libSceSaveDataDialog` 8/8 stubbed |
| Save, quit, reload | untested | Only indirect: menus reopen where the last session ended (`NOTES.md:654-656`) |
| Pad buttons and sticks | works | SDL3 pads, checked on one pad (`NOTES.md:372-377`) |
| Touchpad, motion, light bar | untested | Touchpad mapped (`gamepad_sdl.cpp:272-279`); orientation fixed at identity, TODO (`:233`, `:264-270`) |
| 30 fps cap | works | `NOTES.md:421-425`; vblank grid in `videoout.cpp:64-95` |
| Hitches | known broken | 100-400 ms in the Introduction (`NOTES.md:651-653`) |
| Stubbed libraries | 191 imports, not 590 | On the finish line: Ime, ImeDialog, JpegDec, JpegEnc, SaveDataDialog, MsgDialog, CommonDialog, 19 Gnm calls; Es/Gs/Hs/Ls shaders only write a NOP (`gnm_packets.cpp:66-89`) |

**Real-PS4 baseline.** Sony says the campaign and other offline features stay playable, and local levels stay available but can't be shared ([PlayStation](https://www.playstation.com/en-us/legal/gameservers/)). LBP Union adds Story Mode, Create Mode and level-pack DLC ([LBP Union](https://www.lbpunion.com/post/breaking-lbp3-ps4-servers-discontinued/)). No first-hand source gives the exact boot message on a real PS4, so that is unknown. The game's own code tries to connect on every boot and shows its own message before going offline (`NOTES.md:200-202`, `:410-415`), so a network message on each boot is expected behaviour, not a port bug. In the baseline run, the Windows message box is `sceErrorDialog`, which only appears with `LBP3_NET=online`. Don't mistake it for the console's dialog.

**Dump layout.** The merged folder is fine: with `LBP3_GAME_DIR=D:\Recomps\ROMs\PS4`, the missing `CUSA00473-patch` layer is skipped (`main.cpp:71-79`, `vfs.cpp:115-121`). The blocker is the file format. Every module in this dump is a fake-signed SELF (`4F 15 3D 1D`, unencrypted and uncompressed), and the loader throws "is not a decrypted ELF" (`module.cpp:55-56`). They need unwrapping to ELF first. The dump's eboot is the build upstream targets: APP_VER 01.28, 51 libraries and 646 imports, and all 7 byte-checked patch sites match. `tools/gamedir.py:24-25` hard-codes `CUSA00473-patch`.

**Port family fixes.** The strongest match is btsouth/lbp3-pc [`45bb339`](https://github.com/btsouth/lbp3-pc/commit/45bb3390456fcd01491ef733e5a5097b32bd76fa) (bbport-based). It starts guest threads with MXCSR `0x9fc0` and x87 control word `0x037f`, and its commit names the Legend of Bunkum white screen as the symptom. Upstream sets that state for fibers only (`fiber.cpp:240-244`), not for threads (`kernel_pthread.cpp:124-138`). Also:

- gravityrush_pc `scripts/prepare.py:48-95`: SELF to ELF.
- gravityrush_pc and bloodborne_pc `src/runtime_services.c`: ImeDialog, MsgDialog, SaveDataDialog and CommonDialog.
- bloodborne_pc `src/runtime_pad/motion.c`: motion and touchpad.
- No match for JPEG, camera snapping, the sky or hitches.

Full findings: branch `research/base-status`, file `docs/research/base-status.md`.
