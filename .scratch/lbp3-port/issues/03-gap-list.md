# Which gaps stand between LBP3 Recompiled and the offline finish line?

Type: grilling
Status: resolved
Claimed by: gap-list session (2026-11-09)
Blocked by: 01, 02
Map: [LBP3 native port](../map.md)

## Question

Which **gaps** separate the **base**, as it runs on this machine and this **game dump**, from the **offline finish line**? For each gap, decide:

- **Scope:** in scope for this effort, out of scope (record why in the map's Out of scope section), or unclear (it needs its own investigation ticket before it can be sized).
- **Kind:** a missing or stubbed library (a **library verdict** to change, such as a real JPEG decoder), a runtime or video-core bug, an untested area to cover, or something specific to running this merged dump.
- **Evidence:** a link to the research or the run that shows it.

Inputs: [Where does LBP3 Recompiled stand against the offline finish line?](01-base-status.md) and [Build LBP3 Recompiled and run it on this dump](02-baseline-run.md).

Known open problems from upstream's notes, to start from:

- the camera snapping back while running
- the yellow or beige sky in Create mode
- the game freezing when a Thruster is switched on
- 100–400 ms hitches in the Introduction
- Sackboy launched out of Needlepoint Peaks onto the Create-mode floor
- JpegDec stubbed
- story worlds after the Prologue never reached

## Answer shape

A gap table in the resolution: gap → kind → scope → evidence → the investigation ticket opened, if one was needed. Every gap that comes out unclear becomes its own research or prototype ticket, and this ticket blocks on none of them: the milestone ladder ticket does.

## Answer

The agent settled this on 2026-11-09, using its own recommendations as the map's standing preference allows. The inputs were [Where does LBP3 Recompiled stand against the offline finish line?](01-base-status.md) and [Build LBP3 Recompiled and run it on this dump](02-baseline-run.md), plus checks of upstream's code and of the baseline logs in `D:\Recomps\lbp3-recompiled\analysis\`, which aren't in git. `runtime/…` paths and `NOTES.md:N` refer to upstream at `dfc492a`.

An investigation ticket was opened only where its answer could change the shape of the milestone ladder. That came to three: [Why doesn't the 30 fps cap hold on this PC?](06-frame-pacing.md), [What hangs the GPU when a Thruster fires?](07-thruster-freeze.md) and [How does LBP3 look and play on a real PS4 where the port is in doubt?](08-ps4-reference.md).

**Gaps.** The kinds are: *dump*, specific to running this merged dump; *runtime* or *video core*, a bug in that part; *library*, a stubbed library whose **library verdict** changes; and *untested*, an area nobody has covered yet.

| # | Gap | Kind | Scope | Evidence | Investigation |
|---|---|---|---|---|---|
| 1 | Every module in the dump is a fake-signed SELF, and upstream loads only ELF | dump | in | 02 Answer; `UnwrapSelf` on `local/self-unwrap` (`ba83dcb`) | none: the fix works and has to land in the fork |
| 2 | `tools/gamedir.py` `eboot()` assumes a `CUSA00473-patch` folder, which breaks `boot_steps.py` and `farc.py` | dump | in (tooling) | `tools/gamedir.py:24-25`; 02 comments | none: a one-line fix |
| 3 | The game runs too fast: frames take 13–26 ms, not 33.3 ms. Presents aren't held to vsync on this PC (8 presents in 13.3 ms), so the pacer, which counts presents, lets flips through early | runtime | in | 02 Answer; `play_log.1.txt` (13.3–14 ms on average for minutes); `presenter.cpp:113-138`; the human: "game seem too fast" | [Why doesn't the 30 fps cap hold on this PC?](06-frame-pacing.md) |
| 4 | The camera snaps back while running | runtime | in; probably the same cause as gap 3 | `NOTES.md:622-650`, `:661-662`. The human noticed nothing in a short session | part of 06 |
| 5 | Switching on a Thruster freezes rendering, because a GPU submission never finishes | video core | in | 02 gap 8; `analysis\perf\perf01.txt` | [What hangs the GPU when a Thruster fires?](07-thruster-freeze.md) |
| 6 | The Create-mode sky is yellow-beige | video core, if a PS4 shows it differently | in if it's a bug | 02 Answer; `NOTES.md:663-664`; `analysis\screenshots\s37.png`, `freeze1.png` | [How does LBP3 look and play on a real PS4 where the port is in doubt?](08-ps4-reference.md) |
| 7 | The Popit's Tools Bag and Gadgets panels open mostly past the right edge | **not a bug** (2026-11-10: the screenshots were cropped; see 08 Comments) | in if it's a bug | 02 comments; `s37.png`. The human placed a Thruster through the Popit without trouble | part of 08 |
| 8 | The Legend of Bunkum ends on a white screen. The cutscene auto-skip that works around it fires in every cutscene level | runtime | in: skipping story cutscenes falls short of "start to finish" | `NOTES.md:568-596`; `game_state.cpp:86-122`. The likely cure is btsouth/lbp3-pc `45bb339`, which sets MXCSR `0x9fc0` and the x87 control word `0x037f` on every guest thread. Upstream sets them for fibers only (`fiber.cpp:242-243`), and nothing else in `runtime/src` touches MXCSR | none: the fix is known. The ladder applies it early, retests with `LBP3_CUTSCENE_AUTOSKIP=off`, and makes the auto-skip off by default once the fix holds |
| 9 | Sackboy is launched out of Needlepoint Peaks onto the Create-mode floor | runtime | in | `NOTES.md:607-610`: it happened during slow stretches, which upstream has since fixed. btsouth's commit ties denormals to NaNs in the physics | none: recheck after gaps 3 and 8 |
| 10 | Hitches and load stalls: first loads stall for 2.5–6.3 s, and frames spike to 189–420 ms in play when new images appear | video core (performance) | in, but late on the ladder: nobody noticed them in play, and they block nothing on the finish line | 02 Answer table; `NOTES.md:651-653` | none for now; measure again after 06 changes the pacing |
| 11 | Audio port 3 underruns constantly | none | not a gap for the finish line. Port 3 is a Voice port (type 2 in the shadPS4 fork's `audioout.h:48-51`), voice chat is out of scope, and the human heard nothing wrong | `play_log.txt` (`[audio] port 3: type 2, 1024 frames, 1 ch float`); `NOTES.md:643-644` (the voice-chat thread sleeps 32 ms) | none: quiet the log, and check that the port carries only silence when audio work comes up |
| 12 | Text entry: `libSceIme` and `libSceImeDialog` are stubs. The game opened a keyboard dialog in the human's first session (`sceImeDialogInit`, `play_log.1.txt:665`), and with the stub no keyboard can appear | library | in: naming levels and writing speech bubbles in Create mode need it | 01 Answer; `play_log.1.txt:665`; `sceImeDialogGetStatus` was polled 34,808 times in one session (`play_log.txt:1789`) | none: implement (see the verdicts below) |
| 13 | Stickers and photos: `libSceJpegDec` and `libSceJpegEnc` are stubs | library | in: the Popit's photos and picture stickers belong to Create mode | 01 Answer; `NOTES.md:22-23` | none: implement |
| 14 | System dialogs: `libSceSaveDataDialog`, `libSceMsgDialog` and `libSceCommonDialog` are stubs | library | in, wherever the offline path reaches them | 01 Answer. Only `sceCommonDialogInitialize` was called in the baseline runs | none: implement |
| 15 | Geometry and tessellation stages: `sceGnmSetEs/Gs/Hs/LsShader` only write a NOP | video core | in if any level uses them; unknown | `gnm_packets.cpp:66-89`; `NOTES.md:198` | 07 checks whether the Thruster uses one; story coverage watches for others |
| 16 | Story mode after Needlepoint Peaks, and the other three characters, have never been played | untested | in | 01 Answer | none: [What milestones take the fork from today's base to the offline finish line?](04-milestone-ladder.md) decides how the story gets covered |
| 17 | Save, quit and reload | untested | in | 01 Answer; saves land in `user\savedata\CUSA00473\` in the clone | none: a check on the ladder |
| 18 | The intro movie (Bink), when it isn't skipped | untested | in | `play.bat:7` skips it by default; 01 Answer | none: a check on the ladder |
| 19 | Touchpad and motion: the touchpad is mapped, but the orientation stays fixed | untested | in, only where the story or Create mode needs them | upstream's `runtime/src/input/gamepad_sdl.cpp:233`, `:264-270`; 01 Answer | 08 finds out where they're needed |

**Library verdicts.** These cover every library whose stubs come into play: the ones the finish line needs, plus every stubbed library the baseline runs actually called (`[stub]` lines in `analysis\*.txt`).

| Library | Verdict | How, or why |
|---|---|---|
| `libSceIme`, `libSceImeDialog` | implement | Port the ImeDialog from gravityrush_pc or bloodborne_pc `src/runtime_services.c`, where text is typed into the game window |
| `libSceJpegDec`, `libSceJpegEnc` | implement | Decode and encode with stb_image and stb_image_write: small, single-file, and licensed public domain or MIT, which fits GPL-2.0 |
| `libSceSaveDataDialog`, `libSceMsgDialog`, `libSceCommonDialog` | implement | The port family's versions for an offline, single-user console, from the same `src/runtime_services.c` |
| `libSceGnmDriver` (19 of 50 stubbed) | inherit | None of the stubs was called in the baseline runs; revisit if 07 or the story coverage hits one |
| `libSceAppContent` (1 of 7 stubbed) | inherit | DLC is reported absent by design, and the dump has none |
| `libSceCamera`, `libSceMove`, `libSceMoveTracker`, `libSceVoice` | stub | PS Move, the PlayStation Camera and voice chat are out of scope |
| `libSceNpWebApi`, `libSceNpSignaling` | stub | Online play is out of scope |
| `libSceGameLiveStreaming`, `libSceCompanionHttpd`, `libSceContentSearch` | stub | Broadcasting, the companion app and content search have no part in offline play. They are now on the map's Out of scope list |
| `libSceSysmodule`, `libSceCoredump`, `sceVideoOutGetDeviceCapabilityInfo_`, `sceVideoOutSetWindowModeMargins`, `_sigprocmask` | stub | The stubs return success, and the baseline runs show no effect from them |
| Everything else the runtime provides | inherit | |

**Not gaps.** The game's "problem with your network connection" message on every boot is expected behaviour: the game tries to connect at each boot and then carries on offline (01 Answer). Two tooling quirks feed the ladder's story-coverage decision but aren't gaps: remote presses of 250 ms or longer register twice, and `tools/nav.py` needs reference screenshots that each user takes themselves.

**Newly out of scope** (recorded on the map): DLC and level packs, because the dump has none and the base reports DLC absent; and broadcasting, the companion app and content search.

**For the milestone ladder.** It now blocks on 06, 07 and 08. Gaps 1, 2, 8 and 12–14 have known fixes, and gaps 16–19 are checks the ladder places. None of them needs another investigation first.

## Comments

- 2026-11-09, gap candidates from [Where does LBP3 Recompiled stand against the offline finish line?](01-base-status.md) (full findings on `research/base-status`, `docs/research/base-status.md`):
  1. Upstream only loads plain ELF, but dumps are SELF.
  2. The Legend of Bunkum white screen. btsouth/lbp3-pc `45bb339` set the PS4 floating-point control state (MXCSR) on guest threads and cured the same symptom; upstream sets it only on fibers. Retest with `LBP3_CUTSCENE_AUTOSKIP=off`.
  3. The cutscene auto-skip fires in every cutscene level, a risk for later worlds.
  4. Text entry and the system dialogs (IME, save and message dialogs) are stubs. gravityrush_pc and bloodborne_pc have implementations.
  5. JpegDec is a stub, which affects stickers and photos.
  - Also: the Thruster freeze is recorded as fixed upstream (Oct 14, `NOTES.md:481`), so recheck it rather than assume it's open. 191 imports are still stubbed, not 590.
- 2026-11-09, from [Build LBP3 Recompiled and run it on this dump](02-baseline-run.md) (its Answer has the evidence):
  - **Measured on this PC:** the dump needs the SELF unwrap (`local/self-unwrap`). The game runs too fast because the 30 fps cap doesn't hold (14–26 ms frames, and the human confirms it). Loads stall 2.5–6.3 s, with 189–420 ms spikes in play. Audio port 3 underruns constantly. The Create sky is yellow. The Popit panels may open off-screen (unconfirmed). `tools/gamedir.py` assumes a patch folder.
  - **Rechecked:** the Thruster freeze still happens. Rendering froze in the human's session with a Thruster firing, because a GPU submission never finished; see gap 8 in that Answer. Keep it on the known problems above. The human noticed no stutter and no camera snapping in a short session.
  - **Still untested:** the Legend of Bunkum with `LBP3_CUTSCENE_AUTOSKIP=off`, Needlepoint Peaks and later, save and reload, text entry, and stickers and photos.
