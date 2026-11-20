# LBP3Launcher 3 (branch `feat/launcher3`)

The user asked to "make the launcher more appealing, some features are definitely missing". The packaging lane also asked for what a friend's portable package needs (see [package.md](package.md), "Spec for the launcher lane"). Launcher 3 does both. It is our own code, apart from two GPL-2.0-or-later pieces: the Quit Game edit (a port of LBPPC's `quit_menu.py`) and shadPS4's shadNet client (the runtime's copy). LBPPC's launcher (GPL-3.0) stays an idea only.

## Branch

Runtime `feat/launcher3`, pushed to the fork. The worktree is `D:\Recomps\lbp3-wt\launcher3`.

| Commit | What |
|---|---|
| `a304203` | feat/package (fast-forward): portable `FindRoot`, the shadNet TLS root fix, `make_package.ps1` |
| `6579d49` | runtime: `LBP3_WINDOW` and F11 |
| `1272cff` | runtime: `pad_*` lines in `input.ini`, and the shared controls table |
| `224a0d2` | launcher core and its tests |
| `f4aed1a` | the new window |
| `cf3a9d9` | small fixes |
| `ccd89c0` | review fixes and scripted clicks in `--shot` |

## The look

- **Fonts:** Windows' own (Segoe UI, or Segoe UI Variable on Windows 11; Bahnschrift for headings; Segoe UI Black for the title and PLAY). Japanese, Chinese, Korean and symbol fallbacks are merged in. Nothing is downloaded or bundled.
- **Background:** the dump's `pic1` covers the window, panning slowly (a 60 s loop, with a little mouse parallax). Without a dump, a cardboard-dot pattern.
- **Cards:** warm cardboard with stitched (dashed) borders, lifting and brightening on hover. Sidebar icons and other shapes are drawn with ImGui.
- **PLAY:** a breathing green button with a stitched border. On hover it grows, with a sheen; while the game runs it reads PLAYING in blue.
- **Status pill:** Offline, Online as `<name>`, Signing in…, Playing: `<level>`, or the game running.
- **Motion:** pages fade and slide in; toasts slide in at the bottom right.
- **Scaling:** DPI-aware as before; the window is 1180x800 at 100%.

## Pages

- **Home:** a greeting, play stats, quick settings, What's new and tips.
- **Community:** the server's levels: Team Picks, Newest, Most Hearted, Top Rated, Most Played, and search.
  - Cards show the level's icon, title, creator, hearts and plays.
  - Icons are fetched one at a time and cached in `user\cache\launcher_icons`.
  - A card opens details with **Queue this level**, which adds it to the in-game queue with a website sign-in kept in memory only.
- **Online:**
  - **Play online:** one switch and the active account.
  - **Test sign-in:** shadNet and the website, results in plain words (a wrong password says so).
  - **Server:** on opening the page, two small requests ask whether it is there and for its news.
  - **Accounts:** every `online_account*.txt`, with "Use this one" (rename-based) and "Change saved password" (only this PC's copy; the old file kept as `.bak`).
  - **New account:** a name, then **Create account**: shadNet and the website natively, no Python.
  - Also server news, the latest port changes, and Discord as before.
- **Friends** (shadNet):
  - signed in only while this page is open and no `lbp3.exe` runs; signed out on leaving the page and before Play;
  - add a friend by name, accept or decline requests, remove (confirmed), cancel sent requests, online dots.
- **Game:** build choice (hidden in portable mode), starting options (including the tray and confirm-on-close), language, menus, the LBP1/LBP2 import.
- **Graphics & sound:** picture and frame generation, then:
  - **Display:** by number, or by refresh rate (`LBP3_DISPLAY`).
  - **Window:** windowed, borderless or full screen (`LBP3_WINDOW`; F11 in the game).
  - **Volume at start:** `LBP3_VOLUME`.
  - **Surround mix-down:** off sets `LBP3_DOWNMIX=sdl`.
  - Then shaders and startup, as before.
- **Controls:** motion and touch, then a **keyboard editor** (Change, Add a key, Default; clashes flagged) and a **pad editor** (a picker or "Press", then the button on the pad). Saving writes `user\config\input.ini` and keeps `input.ini.bak`; unknown lines and comments are kept, and untouched pad buttons are never written.
- **DLC:** as before, plus "Use another folder…", which sets `LBP3_ADDCONT_DIR`.
- **Saves, Controllers, What's new:** as before.
- **Tools:**
  - the file check;
  - **Updates:** releases for a package, git for a dev checkout (a game folder with `.git`);
  - shortcuts;
  - **Screenshots:** opens Windows' `Videos\Captures`. Popit photos live in the save data, and the tooltip says so;
  - **Copy the latest log:** the last 3000 lines, with the account password masked;
  - the report, the checks, and the game folder.
- **Help:** "Run the setup again", "Show the tour", getting around, in-game keys (Insert, F11, F7), troubleshooting, about.

**Setup (first start):**
- **Game files:** choose CUSA00473 or the folder holding it. It checks TITLE_ID CUSA00473, APP_VER 01.28 and `eboot.bin`, with plain messages for another region or version, then writes `game_dir.txt` (tested). "Check the files" is available.
- **DLC:** the default or another folder.
- **Build:** skipped in portable mode.
- **Online:** create an account, enter an existing name and password, or not now.
- **Shaders:** a note.
- **Done.**

**Tour:** five bubbles on the first start (PLAY, the pill, Online, Community, Help). Everything else is shaded around them.

**Small but expected:**
- an opt-in update check at start;
- hiding to the notification area while playing (opt-in; SDL3's tray);
- closing while the game runs asks first, and the window comes back to ask;
- the latest log can be copied to the clipboard.

## For a friend's package (package.md items)

1. **Release updater** (`launcher_release`):
   - reads GitHub `releases/latest` of `rexyness/LittleBigPlanet-3-Recompiled`;
   - compares the tag's `vMAJOR.MINOR.PATCH` with `LBP3_PORT_VERSION` (a CMake option) or `SOURCE.md`'s "Package version";
   - downloads `LBP3-Recompiled-<tag>.zip`, checks it against its `.sha256` if attached, and unpacks it, refusing unsafe paths;
   - installs in place without ever touching `user\`, `analysis\` or `logs\`. Files in use are renamed to `.old` and recorded, then removed at the next start; only recorded files are ever removed;
   - restarts with `--updated <tag>`, and refuses while `lbp3.exe` runs. Tags must be plain names.
2. **Quit Game without Python** (`launcher_quitmenu`): a port of LBPPC's `quit_menu.py`, writing `<game folder>\user\file_edits.bin` before Play (so it works in the flat package). On the real dump its edited script is byte-identical to the Python's once unpacked.
3. **Native account creation** (`launcher_net` over the runtime's shadNet client):
   - the password is saved first, as `online_account_<name>.pending`;
   - shadNet `Create` over TLS (the client's own `TlsHandshake`, so the expired-roots fix from `feat/package` applies);
   - the file becomes an account (`online_account.txt`, or `_<name>.txt` beside an existing one) only once shadNet made it; a refused name leaves no file;
   - then the website: register, then allow game logins.
4. **The wizard for a friend:** see Setup above.
5. **A DLC folder setting:** `LBP3_ADDCONT_DIR`.
6. **Portable mode:** `Portable()`; no build choices, no git, the release updater.

The active account file is now the single source of the online name and server, so a typed name can never be paired with another account's password.

## Runtime changes (compiled, not yet checked in the game)

- **`LBP3_WINDOW`** (`video/presenter.cpp`): `borderless` or `fullscreen`, and F11 to switch while playing. Unset changes nothing.
- **`pad_<button>`** (`input/gamepad_sdl.cpp`): lines such as `pad_cross = east` (also several buttons, or `none`). Absent changes nothing.
- **`input/action_table.h`:** the keyboard actions (same names, defaults and order) and the pad rows, shared with the launcher.

`lbp3.exe` was not linked on this branch. Only the three changed files were compiled. The game was in use, and the online lane holds the game lock.

## Tested

- **`launchertest`: 356 checks, 0 failed** (launcher 2 had 209). New:
  - the v3 settings and their mapping;
  - the account files: list, switch, password with `.bak`, never overwriting, the active file deciding;
  - `game_dir.txt`;
  - Refresh JSON parsing (instance, announcements, levels, paging and query encoding) and the error wording;
  - the shadNet endpoint rules and the friends model;
  - the release logic: versions, release JSON, unsafe tags, SHA-256, a zip made with miniz unpacked and installed over a fake install (with `user\` and `analysis\` untouched, an in-use DLL, recorded `.old` removal), unsafe zip paths;
  - the `input.ini` editor;
  - against a fake shadNet server on 127.0.0.1: Create (made, taken), the sign-in test (good, wrong password), the Friends session (list, accept, request, remove, an unknown name, a refused sign-in), and the whole Create account (file promoted only after shadNet; a refused name leaves nothing).
- **Against the real dump** (`LAUNCHERTEST_DUMP`, read-only): the file check, and the native Quit Game edit compared with the Python's.
- **`--print-launch`** with the user's settings:
  - the active account (`LBP3_ONLINE*` and `LBP3_SHADNET*`);
  - the native Quit Game;
  - every check OK.
- **Screenshots:** `--shot` on the secondary display, never focused, with a fixed clock and `tools/mock_lbp_server.py` on 127.0.0.1 (extended with instance, announcements, levels, generated icons, login and queue).
  - Pages, setup steps, tour steps and motion shots: `D:\Recomps\lbp3-recompiled\analysis\screenshots\v3\launcher_*.png` (git-ignored; they hold the dump's art).
  - Motion shots: the PLAY hover, a page change, toasts, a click on a level card (opening its popup), and keyboard focus.
  - A fresh portable folder (only the exe, `SDL3.dll` and `portable.txt`) rendered the friend's first start. `--shot` wrote nothing there.
- **Not done or not verified:**
  - **in the game:** Play from this launcher, `LBP3_WINDOW`, F11 and the pad lines (the user was playing, and the online lane holds the game lock);
  - **real servers, never contacted:** shadNet TLS Create (only plain TCP against the fake), friends on play.lbppc.com, the website login and queue, the GitHub release download and self-update and restart (no release is published yet);
  - **driven only by the scripts:** tray, quit confirmation, key and pad capture, file dialogs and gamepad navigation.

## For the integrator

- Merge `feat/launcher3` into `bestofboth`, run `build.bat`, then `build\launchertest.exe` (356).
- New targets: `launcher_miniz`, `launcher_net` (links `shadnet_client`); `LBP3Launcher` gains `launcher_theme`, `launcher_pages` and `launcher_online`.
- A package build can set `-DLBP3_PORT_VERSION=v0.1.0-lbp3port`, or ship `SOURCE.md` as `make_package.ps1` does.
