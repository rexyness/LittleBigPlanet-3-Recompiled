# Packaging the port for friends (feat/package)

The human wants to give the launcher, meaning the whole "best of both worlds" port, to friends. The coordinator
reports their choice: a **public GitHub Release on rexyness/LittleBigPlanet-3-Recompiled**, with the zip as
its only asset. Built by `tools/make_package.ps1` on `feat/package` (from `bestofboth` `481589c`).

## What tied the build to this PC (audit, 2026-11-13)

| Thing | Finding | Handled |
|---|---|---|
| Where the launcher looks for the game folder | `FindRoot`: the launcher's parent (it lived in `<root>\build\`), or the main checkout of a worktree. In a flat package it found nothing. | `portable.txt` beside the launcher makes its own folder the game folder (`launcher_sys.cpp` `FindRoot`). |
| Which lbp3.exe | The default is "this launcher's build", i.e. `lbp3.exe` beside the launcher. "Best" and "Port" assume `..\lbp3-wt\best` and `<root>\build`. | The package uses the default; the other choices simply aren't found there. |
| The dump | `game_dir.txt` in the game folder (the parent of `CUSA00473`) or `LBP3_GAME_DIR`; the runtime and launcher agree. | The wizard writes `game_dir.txt`. |
| DLC | Runtime and launcher: `<dump parent>\CUSA00473-DLC`; `LBP3_ADDCONT_DIR` overrides it in the runtime only. | Fine for a friend who keeps DLC beside the dump. Spec item 5 below. |
| The shader cache | `LBP3_SHADER_CACHE_DIR`, else `user\shader_cache` (working folder). `user\shader_cache_best` exists only in the dev layout. | The package starts with an empty `user\shader_cache`; never ships one. |
| Logs, save backups, test marker | `<game folder>\analysis\` (`play_launcher_log.txt`, `savedata_backups\`, `savedata_live_is_test`). | Kept as `analysis\` (made on first use). A rename to `logs\` would touch the launcher and the test scripts; it's spec item 6, optional. |
| Other run-time files | `tools\nid_names.txt` (optional names for logs); the overlay fonts come from `C:\Windows\Fonts`; `user\config\*` is created as needed. | `tools\nid_names.txt` ships (our own list of export names). |
| DLLs | `lbp3.exe`: SDL3.dll plus the **VC++ runtime** (MSVCP140, MSVCP140_ATOMIC_WAIT, VCRUNTIME140, VCRUNTIME140_1), and Windows' own. `LBP3Launcher.exe`: the same. The Vulkan loader (vulkan-1.dll) comes with GPU drivers and is loaded at run time. | SDL3.dll and the VC++ runtime DLLs are copied app-local (Visual Studio's `Redist\MSVC\14.50.35710\x64\Microsoft.VC145.CRT`), so no redistributable install is needed. |
| The Windows version | `VirtualAlloc2` (Windows 10 1803) and `CreateFileMapping2` (**Windows 10 1903**). | README: Windows 10 1903+ or Windows 11, x64. |
| Python | Quit Game's setup (`tools\quit_menu_setup.py`, found via the exe's parent's parent) and "Create account" (`create_online_account.py`) need Python. A friend without Python loses both: the pause-menu Quit Game (the in-game menu can still quit) and account creation. | Spec items 2 and 3: native C++. |
| pkg_extractor.exe | The user's own third-party tool, with an unknown licence. | Never shipped; the friend brings one (README). |
| GPU | Vulkan 1.3 via shadPS4's core; tested only on an RTX 3090 (and its driver). | README states it; other GPUs are untested. |
| TLS | shadNet's TLS loads trust from the Windows ROOT and CA stores. PCs that kept the expired cross-signed "ISRG Root X2" (2025-09-15) fail with "certificate has expired" (the bug behind LBPPC 0.1.2's hotfix). | **Changed, not proven:** certificates that have expired are skipped when the Windows stores are loaded (`net/shadnet/client.cpp`, a few lines in a file the online lane also works in). Offline `shadnettest` 55/0 and `onlinetest` 20/0 pass. It hasn't been tried against the real server, or on a PC that has the expired certificate. Refresh and the launcher use WinHTTP, which builds the chain with Windows itself. |
| App-local DLLs | `llvm-readobj --coff-imports` on every binary in the staged folder. | Every import that isn't part of Windows 10 (MSVCP140, MSVCP140_ATOMIC_WAIT, VCRUNTIME140, VCRUNTIME140_1, SDL3) is in the package. The `api-ms-win-crt-*` imports are the Universal CRT, which comes with Windows 10. |

## Spec for the launcher lane (feat/launcher3)

1. **A release updater.** Check `https://api.github.com/repos/rexyness/LittleBigPlanet-3-Recompiled/releases/latest` (on click or opt-in at start).
   - Compare its `tag_name` with the build's version: `LBP3_PORT_VERSION`, compiled in, or `SOURCE.md`'s "Package version". Tags look like `v0.1.0-lbp3port`; parse `vMAJOR.MINOR.PATCH`, ignoring the suffix.
   - Download the asset `LBP3-Recompiled-<tag>.zip` and check it against `<asset>.sha256` if attached.
   - Unpack it into a temp folder, then replace the program files in place (exes, DLLs, `tools\`, docs, `licenses\`), **never touching `user\` or `analysis\`**.
   - Refuse while any lbp3* process runs.
   - The launcher can't overwrite its own exe while running: rename it to `LBP3Launcher.old.exe`, then restart.
   - Keep the git "Update and build" for dev checkouts only, shown when the game folder has a `.git`.
2. **A native Quit Game writer.** Port the logic of LBPPC's `scripts/quit_menu.py` (GPL-2.0-or-later, keep the attribution) to C++. The launcher writes `user\file_edits.bin` before Play when Quit Game is on, without Python, and the runtime reads it through `LBP3_FILE_EDITS`, as now.
3. **Native account creation.** Port `tools/create_online_account.py`:
   - shadNet Create over TLS to `shadnet.lbppc.com:31313`: packet header `<BHIQ` (type, command 2, total size, id), payload `u32 len` + protobuf fields 1 name, 2 password, 4 email; wait for ServerInfo (type 3) first; reply code 0 means OK;
   - then Refresh `POST /api/v3/register` (username, emailAddress `<name>@lbppc.invalid`, passwordSha512);
   - then `PATCH /api/v3/users/me` (psnAuthenticationAllowed and rpcnAuthenticationAllowed true) with the token;
   - save the account file BEFORE sending, and never overwrite one.
   - For TLS on the shadNet socket, use Windows SChannel or the runtime's LibreSSL code, skipping expired roots as in `client.cpp`.
4. **The first-run wizard for a friend.**
   - **Find the dump:** the folder that CONTAINS `CUSA00473`, and accept the `CUSA00473` folder itself (use its parent).
   - **Verify US 01.28:** `CUSA00473\sce_sys\param.sfo` has TITLE_ID `CUSA00473`, APP_VER `01.28` (or VERSION), and `eboot.bin` exists. Say clearly if it's another region or version.
   - **The DLC folder:** default `<parent>\CUSA00473-DLC`. Optional "Install DLC from .pkg…" with their own extractor.
   - **An online account:** create one (item 3) or enter an existing name and password (writes `user\config\online_account.txt`); then Online on or off.
   - **Shader note:** first-visit pauses happen once.
5. **A DLC folder setting.** When the user picks a DLC folder elsewhere, pass `LBP3_ADDCONT_DIR` on Play (the runtime supports it).
6. **Optional:** in portable mode (`portable.txt`), name the log folder `logs\` instead of `analysis\`.
7. **Hide the dev build choices in portable mode.** The wizard's Build step (and the Game page) offer "Best of
   both worlds" and "Port (as play.bat)", which point into this PC's worktrees. A friend should see only "This
   launcher's build", or no Build step at all.
8. **Quit Game's paths.** `p.worktree` is `exe\..\..`, which in the flat package is the folder ABOVE the
   package. The Python script is looked for there, and `file_edits.bin` would be written there. Use
   `p.root\user\file_edits.bin` (and the native writer from item 2).

## The package (built 2026-11-13)

- `D:\Recomps\lbp3-dist\LBP3-Recompiled-v0.1.0-lbp3port.zip`: **5.9 MB, 45 files** (feat/package `e393228`), with its `.zip.sha256`.
  The unzipped copy is in the folder of the same name.
- **Contents:**
  - `LBP3Launcher.exe`, `lbp3.exe`, `SDL3.dll`;
  - the VC++ runtime (`msvcp140*.dll`, `vcruntime140*.dll`);
  - `tools\nid_names.txt`, `portable.txt`;
  - `README-FRIENDS.md`, `CREDITS.md`, `SOURCE.md` (the exact commits);
  - `licenses\`: this port's GPL, GPL-3.0 (the Apache-2.0 parts make the combined binaries GPLv3), shadPS4 with its REUSE texts, and the licences of the 18 linked libraries plus SDL3 and stb.
- **Not in it:** PDBs, test programs, game files, DLC, shader caches, saves, account files, `pkg_extractor`.
  `make_package.ps1` scans the staged folder and refuses to zip if any such file slipped in.

## Clean-folder test (2026-11-13)

Report: `D:\Recomps\lbp3-dist\test-clean\report\README.md`, with screenshots and logs. The package ran with
PATH reduced to the Windows folders and no `LBP3_*` variables inherited:

- **The launcher:** `portable.txt` → game folder = the package folder. The wizard asked for the dump. With
  `game_dir.txt` pointing at the real dump (read-only), every check passed except Quit Game (no Python script,
  a soft fail).
- **The game,** as the launcher starts it, with a fresh `user\` in the test folder:
  - the new-profile brightness screen, then the **Pod**, with the offline notice (expected with online off);
  - the first saves in the package's own `user\savedata`, the logs in its `analysis\`;
  - no crash;
  - `user\temp0` (the game's own hdd cache) is 613 MB after the first start.
- **Safety:** the human's saves had the same checksum before and after. Nothing in the dump or DLC folder
  changed. The lock was held and released, and the generated files were deleted (report kept).
- **Not covered:**
  - Play was started from `--print-launch`'s exe, cwd, args and environment, not by pressing PLAY.
  - `game_dir.txt` was written by hand, so the wizard's folder picker wasn't exercised.
  - The friend-without-DLC path wasn't run. Code read: PLAY needs only the exe, the dump and the game folder (`EssentialsOk`), so a missing DLC folder doesn't block it.
  - This PC has the VC++ runtime installed, so only the import check above shows the app-local DLLs are complete.
  - Logs land in `analysis\`, not `logs\` (spec item 6).

## Release (prepared, NOT published)

See [release_notes_v0.1.0.md](release_notes_v0.1.0.md) for the notes and the exact `gh` command. The human
publishes after the launcher v3 merge and a final check.

## Launcher v3 compatibility (2026-11-13, after its report at `ccd89c0`)

- `feat/launcher3` contains `a304203`, so it has the portable `FindRoot` and the expired-roots TLS change.
  `feat/package`'s later commits touch only `tools/`. A test merge of `feat/package` `b335bd6` with
  `ccd89c0` (`git merge-tree`) has no conflicts.
- The updater reads what `make_package.ps1` writes:
  - `ParseVersion("v0.1.0-lbp3port")` gives 0.1.0, and the tag passes its plain-name check (letters, digits, `.`, `_`, `-`).
  - `InstalledVersion` finds `Package version: v0.1.0-lbp3port.` in SOURCE.md mid-line and strips the period.
  - The asset names `LBP3-Recompiled-<tag>.zip` and `.zip.sha256` match.
- v3 compiles in nlohmann/json, which is MIT; its licence is now in the package list (`b335bd6`). miniz was already listed.
- v3 needs no extra files in the package.

## Release test, clean folder 2 (2026-11-13, release lane)

The release zip is built from bestofboth `b37790b`. The test report, with screenshots, is
`D:/Recomps/lbp3-dist/test-clean2/report/README.md`.

- **Found and fixed (`aa9dbc6`): Create account failed against the real server**, with "that doesn't look like a shadNet
  server".
  - Cause: the launcher's Create socket reuses the runtime's TLS handshake, which clears SSL_MODE_AUTO_RETRY.
  - The server's TLS 1.3 session ticket then made the first SSL_read return WANT_READ.
  - After the fix, the taken name "Kistopawa" is refused cleanly ("that online name is taken"), with no files left behind.
- Exercised this time, the gaps of the first clean test:
  - PLAY pressed in the launcher;
  - the wizard (except the native folder dialog);
  - native Quit Game, with no Python, where `file_edits.bin` lands in the package's own `user\`;
  - the updater with no releases ("no release has been published yet");
  - an online run as Kistopawa: shadNet and Refresh signed in, Plus authorized, no 403.
- Gaps at the time, all closed later the same day (see release_notes_v0.1.0.md and test-clean3/4/5 reports):
  - a brand-new account: Kistopawa3 was created through the packaged launcher (test-clean3);
  - the Windows folder dialog: the setup now also takes a typed or pasted path (`ae530eb`), tested;
  - the DLC count after a hand-edited `game_dir.txt`: the typed path refreshes it like the dialog does;
  - `make_package.ps1` and `NoDefaultCurrentDirectoryInExePath`: it calls `build.bat` by full path (`c8602d0`).

## v0.1.2-lbp3port (published 2026-11-14)

- **Release:** https://github.com/rexyness/LittleBigPlanet-3-Recompiled/releases/tag/v0.1.2-lbp3port, target `main` `0c9c83d` (= `3b0b958` + package docs), shadPS4 `54abc447` (tag `v0.1.2-lbp3port` on both forks). Zip `LBP3-Recompiled-v0.1.2-lbp3port.zip`, 46 entries (the same names as v0.1.1), sha256 `d592cee125f054ab8d0bbd245a306e15524cf8a19299a6a599365c9374da5ee4`; the downloaded asset matches its `.sha256`, and `releases/latest` names it.
- **New:** boot loading screen and cache read-ahead (perf/boot_screen.md), nine render sizes (perf/resolutions.md), the online account's name in game (`330a1bc`).
- **Built** by `tools/make_package.ps1 -Version v0.1.2-lbp3port` in `D:\Recomps\lbp3-wt\release` (detached at `0c9c83d`, `release-shadps4` at `54abc447`). That also rebuilt the launcher the user clicks (`release\build\LBP3Launcher.exe`); its `--print-launch` still gives best's exe, 1440p and `shader_cache_best`.
- **Clean-folder test** (`D:\Recomps\lbp3-dist\test-v012\report`, `_tools\pkg_test.sh` / `pkg_burst.sh`): unzipped by Windows' `tar.exe`; Play's plan from `--print-launch`; a fresh profile at 1440p (brightness screen, then the Pod with the offline notice), then 3440x1440 (94 sites, wide view): loading screen in every run, 0 crash lines, 0 GPU warnings.
- **One scrambled frame** was captured at 3440x1440, 30 s into the first wide run (the Pod, a real frame, nothing in the log); 74 later captures, a second apart, cold and warm shader caches, were all clean. Not reproduced; noted, not blocking. The user's own ultrawide session saw nothing like it.
- **Not tested for this release:** the in-launcher update from v0.1.1 to v0.1.2 (the updater and packaging code are unchanged since v0.1.1; the v0.1.0 updater was tested end to end against the real GitHub release), two-player co-op with an ultrawide player.

## v0.1.3-lbp3port (published 2026-11-14, ~11:35)

- **Release:** https://github.com/rexyness/LittleBigPlanet-3-Recompiled/releases/tag/v0.1.3-lbp3port, target `main` `e65e9fe` (exact maths `419b87e`, background shader builds `0959862`/`f1ad9f4`, docs), shadPS4 `9423d341` (tag `v0.1.3-lbp3port` on both forks). Zip 46 entries (the same names as v0.1.2), sha256 `441f8689db37bf3ad7877a3effd9f7ce1c6e901514672d507cb570c096bc7d88`; the downloaded asset matches; `releases/latest` names it.
- **New:** exact maths online (perf/lbppc_ports.md §1): **both co-op players need v0.1.3**, an older version is refused at connect; background shader builds as a launcher switch, off by default (§2); GPL-3.0-or-later.
- **The user's own build moved with it:** `lbp3-wt\best` on `e65e9fe` rebuilt (previous exe `best\build\lbp3.exe.pre_v013_20261114_1121`); the launcher the user clicks was rebuilt by the package build (same commit); its `--print-launch` gives best's exe, 1440p, `shader_cache_best`.
- **Tested before publishing:** the clean-folder test (`test-v013\report`: a fresh profile at 1440p, then 3440x1440; 0 crashes, 0 GPU warnings, the loading screen each time) and a boot with `LBP3_EXACT_MATH=1 LBP3_ASYNC_PIPELINES=1` (all 3,373 sites patched; the Pod normal).
- **Not run (the user's call: "we can run the regression later"):** the full regression on the combined build. Exact maths had it on its own branch (all suites pass, forced on); the background-build branch passed keyboard, photo, backup and co-op before the run was stopped for the user's session; milestone 1's checks on the combined build are still to do.
