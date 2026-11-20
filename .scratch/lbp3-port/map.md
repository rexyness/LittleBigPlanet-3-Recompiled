# LBP3 native port

Label: wayfinder:map
Status: done (2026-11-10): the [port spec](spec.md) is final, and milestone 1 is ready for a build session. Added since: [local co-op](issues/13-local-co-op.md) (2026-11-11), a build ticket that closes with milestone 3.

## Destination

A **port spec** (`.scratch/lbp3-port/spec.md`) for taking a local fork of [LBP3 Recompiled](https://github.com/PurpleGitty/LittleBigPlanet-3-Recompiled) from where it stands on this dump to the **offline finish line**: the gaps measured and triaged, and a milestone ladder whose first milestone a build session can start on. Building the port happens after this map.

## Notes

- **Domain.** A native PC port of LittleBigPlanet 3 (PS4, US `UP9000-CUSA00473`, version 01.28). The **game dump** is `D:\Recomps\ROMs\PS4\CUSA00473`: base and patch merged into one folder, read-only, and never committed or copied into a repo. Vocabulary is in [GLOSSARY.md](../../GLOSSARY.md); lasting decisions are in [docs/adr](../../docs/adr).
- **Decisions made while charting** (2026-11-09):
  - "Recomp" means a **native port**: the original eboot runs on the PC's processor, a runtime replaces the PS4 libraries, and a shadPS4-derived video core handles graphics.
  - The **base** is LBP3 Recompiled, cloned locally and tracking upstream ([ADR 0001](../../docs/adr/0001-native-port-on-a-forked-base.md)). Publishing a GitHub fork or sending pull requests upstream is outward-facing and is the user's call. (On 2026-11-10 the user had the forks published: `rexyness/LittleBigPlanet-3-Recompiled`, `rexyness/shadPS4`, and this repo as the private `rexyness/lbp3-pc`. Pull requests: "not yet".)
  - The supported dump is US 01.28 only, on Windows only.
  - The toolchain follows the base: Visual Studio 2026 with clang-cl, CMake/Ninja and SDL3 3.4.16. The sibling ports' MSYS2 setup does not apply.
  - The **offline finish line** is story mode start to finish, plus Create mode and the Pod, with audio, saves and a controller, run from a dev build.
  - (2026-11-11, at the human's request) The finish line also covers **local co-op**: up to four players with their own controllers on one PC ([13](issues/13-local-co-op.md)). Online play stays out of scope.
- **This PC.** Windows 10 Pro 19045, RTX 3090 (driver 621.01), Visual Studio Community 18.5 with the C++ workload and CMake. Two displays: the primary is 2560×1440 at 239.999 Hz (max 360), and a second runs at 165 Hz to its left. Meta and SudoMaker virtual displays were attached on 2026-11-09 but didn't enumerate on 2026-11-10. The NVIDIA global profile forces vertical sync off. The port has to cope with that setting, so leave it as it is ([the frame-pacing ticket](issues/06-frame-pacing.md)). Upstream is cloned to `D:\Recomps\lbp3-recompiled\` and builds there ([Build LBP3 Recompiled and run it on this dump](issues/02-baseline-run.md)).
- **Facts gathered while charting:**
  - The eboot imports 50 PS4 libraries. Upstream counts 646 functions, 475 of them covered by shadPS4.
  - The game-shipped modules (`libc.prx`, `libSceFios2.prx`, `spu.prx` = the PS3 SPU job system on x86, and the FMOD plugins) are loaded as-is by the base.
  - In the shadPS4 compatibility tracker, no LBP3 report is past menus.
  - Other LBP3 efforts: btsouth/lbp3-pc (Linux, EU 1.00), TheNIKITO/LittleBigPlanet-3---PC (stuck on "Connecting"), MafraLP/lbp3-native-windows (research only).
- **Standing preference.** The user accepts the agent's recommended answers. Grilling tickets are settled by the agent with its recommendation, written into the resolution. The agent stops for the user only when absolutely necessary: steps only the user can take (installs needing administrator rights, playing and listening), or outward-facing actions.
- **Skills.**
  - `reverse-engineering` for any work on the binaries
  - `research` for research tickets
  - `domain-modeling` to keep GLOSSARY.md current
  - The port family's code (gravityrush_pc, issn_pc, iss_pc, bloodborne_pc) is a source of fixes, not the base.

## Decisions so far

<!-- one line per closed ticket: [title](issues/NN-slug.md): gist -->
- [Where does LBP3 Recompiled stand against the offline finish line?](issues/01-base-status.md): The record covers boot, the Pod, the Introduction, Needlepoint Peaks and Create mode. Nothing after Needlepoint Peaks has been tried. The Legend of Bunkum is auto-skipped, and btsouth's MXCSR fix is a likely cure. Text entry, JPEG and the system dialogs are still stubs, and upstream can't load this dump's SELF-wrapped modules until they are unwrapped to ELF.
- [Build LBP3 Recompiled and run it on this dump](issues/02-baseline-run.md): Upstream builds in 60 s with the installed clang-cl 20.1.8. With a small loader change that unwraps the SELF modules, it runs this dump through the Pod, the Introduction and Create mode, and leaves the dump untouched. Sound and controls are fine. The game runs too fast because the 30 fps cap doesn't hold on this PC (14–26 ms frames). Switching on a Thruster still freezes rendering, because a GPU submission never finishes. Loads stall, audio port 3 underruns, and the Create sky is yellow.
- [Which gaps stand between LBP3 Recompiled and the offline finish line?](issues/03-gap-list.md): 19 gaps, each with a library verdict where a library is involved. Three need investigating before the ladder: [the frame pacing](issues/06-frame-pacing.md), [the Thruster freeze](issues/07-thruster-freeze.md), and [a real-PS4 reference](issues/08-ps4-reference.md) for the sky, the Popit panels, touchpad and motion. The rest either have known fixes (the SELF unwrap, MXCSR on game threads for the Legend of Bunkum, and implementing text entry, JPEG and the system dialogs) or are checks the ladder places.
- [How does LBP3 look and play on a real PS4 where the port is in doubt?](issues/08-ps4-reference.md): On a real PS4 the default Create sky is pale cyan and the Popit's bag pages sit fully on screen at the right, so the port's yellow sky (red and blue swapped in the sky alone) is a port bug; the off-screen pages were a screenshot crop, not a bug (corrected 2026-11-10, 08 Comments); the touchpad is only an optional Create-mode input with stick alternatives (expected behaviour), and whether any story level needs motion stays unknown until the full story run.
- [Why doesn't the 30 fps cap hold on this PC?](issues/06-frame-pacing.md): Presents don't block on this PC, most likely because the NVIDIA global profile forces vsync off, and upstream counted each present as a vblank. Waiting on the window's DXGI output for real vblanks holds 33.3 ms and follows the window between monitors (`local/frame-pacing`, half a session to land). The human confirmed normal speed and a camera that looks right. Measured 0/2 sim steps persist and are game-side, low priority ([09](issues/09-sim-step-bursts.md)). The play log also shows hitches and some over-budget sections, which the ladder places as performance work.
- [What hangs the GPU when a Thruster fires?](issues/07-thruster-freeze.md): The graphics queue, after the runtime's submit race (ticket 10) leaves garbage descriptors; every reset had rejected samplers with the same bad pointer, and the fluid data is the likely victim. In the human's sessions at 240 Hz the old wait reset the GPU the moment water started firing, and ticket 10's fix ran clean at both pacings. No change of its own; it lands with 10's patch in milestone 1.
- [Why does the sim run 0 or 2 steps per frame at a steady 33.3 ms?](issues/09-sim-step-bursts.md): The runtime's epoll wait truncated the host's 33.3 ms tick timeout and overslept, so the tick drifted across the main thread's read. Precise timeouts cut steps ≠ 1 in the real Introduction from about 10–12% to 0.1–0.4% at 165 and 240 Hz, and the human's walk was smooth between transitions. The stutter they felt at transitions is load stalls, filed under gap 10.
- [Why does the game hang with the main thread in a job barrier and the compute queue waiting on a label?](issues/10-compute-wait-deadlock.md): The runtime released the game after a graphics submit as soon as any queue was blocked, so the next frame-begin could reset the flag the new buffer waited on. Waiting for the graphics queue alone fixes it: 14 of 14 recipe runs at 165 Hz, 6 of 6 at 240 Hz, and the human's 23-minute session with 3 Thrusters, water and a save. Half a session to land in milestone 1.
- [What milestones take the fork from today's base to the offline finish line?](issues/04-milestone-ladder.md): Seven milestones: Foundation (gaps 1–6, 8, 11; ready to build), Create mode, the Prologue and first world, every story world, performance, the intro movie (done early), and the finish line. Story coverage by human playthroughs with save snapshots. The fork lives in the clone with published GitHub forks and a `port` integration branch; pushing `port` and upstream pull requests are confirmed with the human first.
- [Assemble the port spec](issues/05-assemble-port-spec.md): The [port spec](spec.md) is final: scope, base and fork layout, toolchain and test tools, the gap table, the ladder with milestone 1's steps and checks, and the known risks.

## Not yet specified

- **What the rest of the story will reveal.** Nobody has reported getting past Needlepoint Peaks. Later worlds and the other playable characters (Oddsock, Toggle, Swoop) may expose problems that can't be named until someone gets there.

## Out of scope

- **Online play and community servers.** Sony took the servers offline in January 2024, [indefinitely](https://www.playstation.com/en-us/legal/gameservers/). Project Lighthouse lists LBP3 on PS4 as incompatible. Refresh support for PS4 is only an open draft ([PR #1138](https://github.com/LittleBigRefresh/Refresh/pull/1138)) that needs a plugin on a jailbroken console. A later effort could start from there.
- **Other releases and patch levels:** EU CUSA00063 and others. The base and its address patches target US 01.28.
- **Linux and Steam Deck.** btsouth/lbp3-pc covers Linux.
- **PS Move, PlayStation Camera, voice chat and trophies:** not part of the offline finish line.
- **DLC and level packs:** the dump contains none, and the base reports DLC absent by design ([the gap list](issues/03-gap-list.md)).
- **Broadcasting, the companion app and content search** (`libSceGameLiveStreaming`, `libSceCompanionHttpd`, `libSceContentSearch`): console services with no part in offline play. They stay stubs ([the gap list](issues/03-gap-list.md)).
- **A launcher, release packaging, and enhancements past what the base has** (60 fps, higher resolution, new mouse and keyboard work).
- **Static recompilation, decompilation, or targeting the shadPS4 emulator itself** ([ADR 0001](../../docs/adr/0001-native-port-on-a-forked-base.md)).
- **Building the port:** it starts from the spec now that this map is done, beginning with milestone 1.
