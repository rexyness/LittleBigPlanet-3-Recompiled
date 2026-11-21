# LittleBigPlanet 3 Recompiled

A native Windows runtime for LittleBigPlanet 3 (PS4, CUSA00473). It loads the game's own
executable and modules from **your own dump of the game**, links them against
high-level reimplementations of the PS4 system libraries (`runtime/src/hle`), and
renders through [shadPS4](https://github.com/shadps4-emu/shadPS4)'s GPU emulation.

Made with [Claude Code](https://claude.com/claude-code) on the Opus 5.5 model. `NOTES.md` is
the running log of that work (what was tried, found and fixed), useful for picking it up.

**No game files are included or will be accepted.** You need a legally obtained dump of
your own copy (base game CUSA00473 and its update).

**To play**, download the latest release from
[Releases](https://github.com/rexyness/LittleBigPlanet-3-Recompiled/releases) and start
`LBP3Launcher.exe`; its `README-FRIENDS.md` walks through the first start. The rest of this
page is for building from source.

## Setup

1. Clone with the shadPS4 fork: `git clone --recursive <this repo>` (or run
   `git submodule update --init --recursive` after cloning).
2. Download SDL3 3.4.16 for Visual Studio (`SDL3-devel-3.4.16-VC.zip` from
   [SDL's releases](https://github.com/libsdl-org/SDL/releases)) and extract it so that
   `third_party/SDL3/cmake` exists.
3. Put your dump in a folder that holds `CUSA00473`: the base game, with update 01.28 either
   merged in or kept apart in `CUSA00473-patch` next to it. Optionally, extract your own
   DLC into `CUSA00473-DLC` next to them. By default the runtime looks in `LBP3 Game Files`
   next to this project folder. Anywhere else: write the folder's path on the first line of
   `game_dir.txt` in the project root (git-ignored), or set `LBP3_GAME_DIR`.
4. Build with Visual Studio 2026 (C++ workload with Clang): `build.bat`. It builds the game
   (`build\lbp3.exe`), the launcher (`build\LBP3Launcher.exe`, the settings window the
   releases start from) and the test programs.
5. Run: `play.bat` (its options are at the top of the file). Logs go to
   `analysis\play_log.txt`.

Controls are configured in `user\config\input.ini` (written on first start).

## Layout

| Path | What |
|---|---|
| `runtime/` | The runtime: ELF loader and linker, HLE libraries, input, video, audio, online play |
| `runtime/launcher` | The launcher (settings, accounts, DLC, updates) |
| `runtime/tests` | Test programs for single parts (input, audio, the on-screen keyboard, co-op...) |
| `third_party/shadPS4` | [Our shadPS4 fork](https://github.com/rexyness/shadPS4/tree/lbp3) (GPU command processor, Vulkan renderer), branch `lbp3` |
| `third_party/SDL3` | SDL3 (window, input), downloaded separately |
| `data/` | The on-screen keyboard's word list |
| `tools/` | Test and analysis scripts (self-driving test runs, disassembly helpers); `tools/package` holds the release's docs |
| `docs/` | Decision records (`docs/adr`), research write-ups, and the docs agents work from |
| `.scratch/` | Planning notes, the old local issue tracker and its test tools |
| `NOTES.md` | Detailed working notes: what was found, fixed and still open |
| `GLOSSARY.md` | The project's terms |

Local output (`build/`, `analysis/`, `user/` saves and caches) is git-ignored and can
contain game data; don't commit it.

## Status

The Prologue has been played through (to Titan Released) and Manglewood, the first world, has
been started; the Pod, its menus and create mode work. The port also has resolutions up to 4K
and ultrawide, 60 frames a second on screen (frame generation), local co-op for up to four
controllers, your own DLC, and the LBP1/LBP2 item import. Online play uses the community
servers at play.lbppc.com: community levels, profiles, friends, and online co-op with friends
(on this port, or on LBPPC's own client with a launcher switch). Bugs and plans are tracked in
[GitHub Issues](https://github.com/rexyness/LittleBigPlanet-3-Recompiled/issues); see
`NOTES.md` and the release's known issues for what's still open.

## License

GPL-3.0-or-later (see `LICENSE`). This covers the project's own code only; LittleBigPlanet 3
and its files belong to Sony Interactive Entertainment.

Up to v0.1.2 the project was GPL-2.0-or-later. PurpleGitty's original runtime and shadPS4 (the
`third_party/shadPS4` submodule) are GPL-2.0-or-later and are used here under version 3. Code
taken from LBPPC (emurphy04, GPL-3.0-or-later) keeps its notices and is credited where it's used.
