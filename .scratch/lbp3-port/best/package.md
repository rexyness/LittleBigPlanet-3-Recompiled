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
- **Not run (the user's call: "we can run the regression later"):** the full regression on the combined build. Exact maths had it on its own branch (all suites pass, forced on); the background-build branch passed keyboard, photo, backup and co-op before the run was stopped for the user's session; milestone 1's checks on the combined build are still to do. (Done with v0.1.4, below.)

## v0.1.4-lbp3port (published 2026-11-14, ~16:00)

- **Release:** https://github.com/rexyness/LittleBigPlanet-3-Recompiled/releases/tag/v0.1.4-lbp3port, target `main` `9f20506` (capture fix `9d3404c`, update check `5e56c8b`, docs `9f20506`), shadPS4 `9423d341` unchanged (tag `v0.1.4-lbp3port` on both forks). Zip 46 entries (the same names as v0.1.3), sha256 `3d78c0feb1e82b349d81110a060e37f76d5447f1f5b3ae06332102cb6b18b956`; the downloaded asset matches; `releases/latest` names it.
- **New:** photos, saved-costume icons, level icons and the profile picture no longer streaked or cropped above 1080p (perf/capture_pitch.md; the bug shipped in every release since v0.1.0); the launcher checks for updates when it opens, on by default, with a one-time switch-on for older `launcher.ini` files (`settings_version=2`) and a status-line notice "vX is out (you have vY). [Update]" that opens Tools (nothing installs without the button); the "Finer glow above 1080p" switch is gone. Co-op unchanged (HELLO capabilities, not version numbers): v0.1.3 and v0.1.4 players can play together (not tested with two players).
- **Tested before publishing:**
  - `capfix_lane.sh` with the pixel check at 2560x1440, 3440x1440 and 3840x2160: all PASS (frame correlation 0.94 / wide / 0.93); the pre-fix build FAILs it.
  - **The full regression** (`_tools\rel_regress.sh` via `rel014_run.sh`, output `analysis\rel014_regress_out.txt`), at last including milestone 1's checks: A, the runtime's defaults: keyboard, photo, backup (6 dialogs on the overlay, no file changed), co-op PASS; Introduction 0.52% / 0.73% (inside the earlier spread: 0.69% / 0.44% at `m1port`); Thruster 3/3; Bunkum 16/16 with Needlepoint; sky cyan. B, the launcher's defaults (1440p, rescale, frame generation): all suites PASS; Introduction 0.00% / 0.25%; Thruster 3/3; Bunkum 16/16; sky cyan; "80 sites patched" in every run. 0 GPU warnings, 0 crashes, no nvlddmkm/Display events in either.
  - Clean-folder test (`test-v014\report`): 1440p fresh profile and 3440x1440, 80 / 91 sites, loading screen, 0 crashes, 0 GPU warnings; the seeded old-style `launcher.ini` (with `bloom_1440=1`) loads.
  - Update notice, live (`test-v014-notice`): a throwaway launcher built as v0.1.2 (`_tools\build_vtest_launcher.cmd`, `release\build_vtest`) with a v0.1.3-style `launcher.ini` (`update_on_start=0`, no `settings_version`) showed "v0.1.3-lbp3port is out (you have v0.1.2-lbp3port). [Update]" under the title, and on closing wrote `settings_version=2`, `update_on_start=1`, without the `bloom_1440` line.
- **Incident while packaging:** Windows restarted twice at 14:15 and 14:16, about two minutes after the release worktree was checked out at `9f20506`. Its `.git\worktrees\release\HEAD` came back as 41 zero bytes ("not a git repository" from PowerShell's git), and was written back by hand. A scan of every file written from 12:30 to 14:17 in the clone (`user\`, `.git`, `build`, `analysis`), the worktrees, `_tools`, the planning repo and the memory found no other zero-filled file. The user's saves still equal `human_play_20261114_1241_full`.
- **The user's own build:** `lbp3-wt\best` on `9f20506` rebuilt (launcher `0a92e869`, old copy `LBP3Launcher.exe.pre_v014_20261114_1600`; `lbp3.exe` unchanged since the capture fix, `f2839413`). Its `launcher.ini` switches the check on at the next start; as a development checkout it runs the git check (fetch only), not the release check.

## v0.1.5-lbp3port (published 2026-11-15, ~00:25)

- **Release:** https://github.com/rexyness/LittleBigPlanet-3-Recompiled/releases/tag/v0.1.5-lbp3port, target `main` `519f220` (= `8fe0e90`, the LBP1/LBP2 import in the in-game menu, + release notes), shadPS4 `9423d341` unchanged (tag `v0.1.5-lbp3port` on both forks, lightweight like v0.1.4's).
  - Zip: 46 entries, the same names as v0.1.4; sha256 `acdf76d6d74e51b8707fab6fe2b25bfac247122cc2b1f7f78f3d8bffb597ade4`.
  - The downloaded asset is byte-identical to the local zip, and `releases/latest` names it.
  - The `.sha256` ends in CRLF, as v0.1.4's did, so `sha256sum -c` in Git Bash can't open the file name; compare the hashes directly.
- **New:** "Import LBP1/LBP2 items" in the L3+R3 / Insert menu (issues/19 for the details); the launcher's My Unlocks switch is gone. Co-op unchanged: v0.1.3, v0.1.4 and v0.1.5 players can play together (not tested with two players).
- **Built** by `tools/make_package.ps1 -Version v0.1.5-lbp3port` in `D:\Recomps\lbp3-wt\release` (detached at `519f220`), at below-normal priority while the human played. Zip `lbp3.exe` md5 `8b82bb3c…`, equal to `release\build`.
- **Tested before publishing:**
  - The full regression in both configurations (`_tools\menuunl_regress.sh`), the in-game import checks (`menuunl_session.sh`), and the offline suites, all on `8fe0e90`. That's the same source, built in the `res` worktree (`lbp3.exe` md5 `776783c4…`): the same code, not the same file.
  - The zip itself: the entry list, the hash, and `SOURCE.md`'s "Package version: v0.1.5-lbp3port".
- **Published before the clean-folder test** at the human's request ("publish now"; their game was running, so the test couldn't start). `_tools\rel015_test.sh` runs it on the published zip once the game has been closed for 2 minutes: output `D:\Recomps\lbp3-dist\test-v015_run.txt`, report `test-v015\report`.
- **Clean-folder test after publishing: PASS** (00:25-00:35, the human closed the game at about 00:25; `test-v015\report`, `test-v015_run.txt`). The published zip was unpacked with Windows' `tar.exe` (16 top-level entries) and Play's plan taken from `--print-launch`:
  - 1440p: 80 sites patched, loading screen, first picture after 0.8 s.
  - 3440x1440: 91 sites patched (wide view), loading screen, first picture after 1.0 s.
  - Both reach the Pod with a fresh profile; 0 crash lines, 0 GPU warnings.

## v0.1.8-lbp3port (published 2026-11-20, ~08:10)

**Decision, 2026-11-20 ~06:15: v0.1.8 includes Batch 2.** Shown the pacing data below (the worst run is about 12 one-frame hitches in 51 s, mostly within one second; cause unproven), the user said "include batch 2, package it". The release branch is **`release/v0.1.8-batch2`**: main `9a4d760` + `feat/seth-batch2` (merge `de9dc0c`, shadPS4 `b0242a25`) + the pad fix `99e2ff3` + the release notes and credits, with a known issue for the occasional judder. The package and its tests are at the end of this section. Everything below down to it describes the keyboard-only candidate, `release/v0.1.8` (shadPS4 `37508fd9`). That one is kept as the fallback: its zip is moved to `D:\Recomps\lbp3-dist\v018-keyboard-only\` (sha256 `c93f9643…`, tested as described).

- **Branch (keyboard-only fallback):** `release/v0.1.8` = main `9a4d760` + `7910407` (scripted runs hide physical pads) + the release notes, shadPS4 `37508fd9` (as v0.1.7). Pushed to the fork; not merged into main; no tag.
- **New:** the on-screen keyboard's word suggestions and moving it (main `a9f54aa` to `9a4d760`). Since v0.1.7 nothing changed in `runtime/src` outside `ui/`, nor in shadPS4: co-op stays compatible (v0.1.3 and later) and shader caches carry over.
- **Batch 2 held.** The user asked for Batch 2 in v0.1.8 if its regression was good. `release/v0.1.8-batch2` (main + `feat/seth-batch2`, shadPS4 `b0242a25`) passed every suite except the Introduction's pacing gate. Details: perf/seth_ports.md, "Batch 2 in v0.1.8: held".
- **Tested before publishing,** on the release worktree's exe (md5 `f7b79a0a…`, built from `7910407`'s source; the notes commits change no built file):
  - offline suites from an empty folder: 18 of 18, 0 failed (osktest 434, launchertest 418, restest 2,655, exactmathtest 90,068, …);
  - **the full regression** (`_tools/rel018k_run.sh`, output `analysis\rel018k_regress_out.txt`), muted, physical pads hidden, on `analysis\rel018_ctl_cache` (a copy of `em_cache`). **A**, runtime defaults: keyboard, photo, backup (6 dialogs on the overlay, no save changed), co-op PASS; Introduction 0.26% / 0.13%; Thruster 3/3; Bunkum 16/16 with Needlepoint; sky cyan. **B**, launcher defaults (1440p, shader rescale, frame generation): all suites PASS; Introduction 0.50% / 0.25%; Thruster 3/3; Bunkum 16/16; sky cyan; 80 sites patched in every run. 0 GPU warnings and 0 crashes in all 24 runs, no nvlddmkm or Display events, the clone's exe restored (md5-checked).
- **The keyboard suite was broken by a connected controller, not by code.** Last night's three keyboard runs, the control on the old build among them, failed because the user's DualSense was connected over Bluetooth: the Edit Now route then opened the Popit. Every passing run before had no physical pad. `7910407` hides physical pads from scripted runs (SDL's `SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT`, set from the environment). Tonight's runs set it in `rel018*_run.sh`, the DualSense still connected, and 0 controller lines reached the logs.
- **Open decision, the user's:** the word list is unfiltered at their request (`9a4d760`). The shipped list ranks the f-word 316th and a racial slur at about 4,900th. The slur is the 11th of the "ni…" words, so the bar can show it after a few letters. Suggestions are on by default. The release notes say the list "isn't filtered, so it includes swear words" and how to turn it off.
- **Package built (not published):** `tools/make_package.ps1 -Version v0.1.8-lbp3port` in `D:\Recomps\lbp3-wt\release` at `bf04593` (pushed; "ninja: no work to do", so the zip's `lbp3.exe` is the regressed file, md5 `f7b79a0a…`). `D:\Recomps\lbp3-dist\LBP3-Recompiled-v0.1.8-lbp3port.zip`: 49 entries (v0.1.7's 46 + `data/osk/` words_en.txt, NOTICE.md, SCOWL-Copyright.txt), 8,081,615 bytes, sha256 `c93f9643a2e410ef8194a7cd2d6db4061ab7c1bcdd60afc53736b5a0a308309d`. `SOURCE.md` names branch `release/v0.1.8`, commit `bf04593`, shadPS4 `37508fd9`. Rebuild it after a fast-forward of main if `SOURCE.md` should say `main`. Tag `v0.1.8-lbp3port` at `bf04593` on both forks (shadPS4 at `37508fd9`, the same as v0.1.7's) when publishing.
- **Clean-folder test: PASS** (`_tools/pkg_test.sh`, report `D:\Recomps\lbp3-dist\test-v018\report`, output `test-v018_run.txt`), on that zip unpacked with Windows' `tar.exe` (17 top-level entries), physical pads hidden:
  - 1440p: 80 sites patched, loading screen, first picture after 0.8 s;
  - 3440x1440: 91 sites (wide view), first picture after 1.0 s;
  - 0 crash lines, 0 GPU warnings, alive at the end of both.
- **An update from v0.1.7 in place** brings `data\osk`: `ApplyUpdate` installs every file of the release outside `user\`, `analysis\` and `logs\`, new folders included. launchertest_v3's update check installs `tools/readme.txt` into an install without `tools\`, and it passes on this build. Not run on a real v0.1.7 folder.
- **Word suggestions in the game, on the packaged exe: PASS.** The regression's keyboard suite runs the plain keyboard (`LBP3_OSK_PLAIN=1`), so a separate check: last night's `t_osk_words.sh` (copied from that session's temp folder; not in the repo) with `OSK_EXE=D:\Recomps\lbp3-dist\test-v018\unz\lbp3.exe`, suggestions on, pads hidden, under the game lock. The user's own `learned_words.txt` and `keyboard.ini` were moved aside and put back (md5-checked, unchanged). Results:
  - `[osk] word list: 30000 words from …\test-v018\unz\data\osk\words_en.txt (28 ms)`: the packaged list loads;
  - "hel" + R3 picked "help"; up into the bar, right, Cross picked "hello";
  - both learned once each; the right stick moved the keyboard to the right edge (`x=1.0000`);
  - screenshot `analysis\screenshots\m2_pkg018words_B_bar.png`: the bar reads R3 help, [hello], hell, helping, helped, held.
- **Batch 2's hold, how sure:** failed the gate (1.57%, 1.05%), probably from the branch, cause unproven. Tonight alone it's 5 of 19 runs with Batch 2 against 0 of 7 without (about a one-in-five chance). The pooled history, 5 of 22 against 1 of 41, is what makes it convincing.
- **State after the night:** no save marker or kept copy; game lock free; no game running; the clone's `build\lbp3.exe` back to md5 `0c8d2ce4…` with no backups left.
- **The v0.1.8 package, with Batch 2 (the one to publish; not published):** `make_package.ps1 -Version v0.1.8-lbp3port` in `lbp3-wt\release` at `e7e4176` on `release/v0.1.8-batch2` (pushed), shadPS4 `b0242a25`.
  - Its build: 88 steps after switching branches, so the exe (md5 `8b29cc8b…`) is a rebuild of the regressed merge `de9dc0c`'s source (`1e4bd1e9…`): the same code, not the same file, as for v0.1.5. Offline suites on it: 18 of 18, 0 failed (launchertest 428, restest 2,672, framegentest 120).
  - `D:\Recomps\lbp3-dist\LBP3-Recompiled-v0.1.8-lbp3port.zip`: 49 entries, 8,095,385 bytes, sha256 `aa5caf99ba472b4db7537a69c1da3d2d858cab5eb1b7360579ae8c88daf4394d`. `SOURCE.md`: branch `release/v0.1.8-batch2`, commit `e7e4176`, shadPS4 `b0242a25`. Release notes with Batch 2's items and the judder known issue; CREDITS.md names sethlikepie and sgdc3.
  - **Clean-folder test: PASS** (`test-v018b2\report`, `test-v018b2_run.txt`): 1440p, 80 sites, first picture after 0.8 s; 3440x1440, 91 sites, after 1.1 s; 0 crash lines, 0 GPU warnings, alive in both.
  - **Word suggestions in the game on the packaged exe: PASS** (`test-v018b2_words.txt`, shots `m2_pkg018b2words_*`). The word list loaded from `…\test-v018b2\unz\data\osk\words_en.txt` (30 ms); R3 picked "help"; the bar pick gave "hello"; both were learned once; the keyboard moved (x=1.0). The user's own learned words and keyboard place were put back, md5-checked.
  - **To publish:** tag `v0.1.8-lbp3port` at `e7e4176` on the runtime fork and at `b0242a25` on the shadPS4 fork; fast-forward main to `release/v0.1.8-batch2`. Players' shader caches rebuild once (version 8).
- **Published** at the user's "go ahead": https://github.com/rexyness/LittleBigPlanet-3-Recompiled/releases/tag/v0.1.8-lbp3port, "LittleBigPlanet 3 Recompiled v0.1.8 (word suggestions on the on-screen keyboard; fixes from sethlikepie's port)".
  - Tags `v0.1.8-lbp3port`: runtime `e7e4176`, shadPS4 `b0242a25`, both lightweight like v0.1.4's.
  - Notes from `tools/package/RELEASE-NOTES.md` (unchanged since `e7e4176`). Assets: the zip (8,095,385 bytes) and its `.sha256`.
  - Checked: not a draft or prerelease; `releases/latest` names it; the downloaded asset's sha256 is `aa5caf99…`, the tested zip's.
  - **Main not yet fast-forwarded:** this background job doesn't push main. The user runs `git merge --ff-only fork/release/v0.1.8-batch2` and `git push fork main` in the clone; main `9a4d760` is an ancestor.

## v0.1.9-lbp3port (published 2026-11-20, ~23:55)

**Play online with friends on LBPPC** (the "Play with LBPPC players" switch, `docs/research/lbppc-coop.md`), plus the netcode chain landed with it (cleaner leaving, the session log). The user: "go ahead and do the release"; no regression runs beyond what main already had ("no need to do regression"): the tick gate and rig sessions on the merged code (`docs/research/lbppc-coop.md`).

- **Branch:** `release/v0.1.9` = main `dbe36a1` + `5dc9742` (the release notes and credits; the draft `RELEASE-NOTES-next.md` folded in and removed). shadPS4 `b0242a25`, the same as v0.1.8's, so players' shader caches carry over.
- **Compatibility:** with the switch off the wire format is v0.1.8's (HELLO capabilities 1|2, DATA3, BYE unchanged; capability 4 only with the switch on), so v0.1.3 to v0.1.9 play together (from the code; no mixed-version session was run). With the switch on, a v0.1.8 player's ACK is refused: the join fails instead of drifting.
- **Package:** `make_package.ps1 -Version v0.1.9-lbp3port` in `lbp3-wt\release` at `5dc9742`. `D:\Recomps\lbp3-dist\LBP3-Recompiled-v0.1.9-lbp3port.zip`: 49 files (the same set as v0.1.8's), 8,145,179 bytes, sha256 `313be89087c81f5d01c0d6349836666c70782e183f8862dcf70bcde12980ffe9`. `SOURCE.md`: `release/v0.1.9` @ `5dc9742`, shadPS4 `b0242a25`, "Package version: v0.1.9-lbp3port". The packaged launcher has the switch.
- **Checked:** the zip unpacked with Windows' `tar.exe` into `test-v019\unz` and started offline from there (muted, pads hidden, under the game lock): the game's first picture after 1.8 s, alive at 50 s, no crash lines. On the release build: p2ptest 308/308, launchertest 434/434.
- **Published:** https://github.com/rexyness/LittleBigPlanet-3-Recompiled/releases/tag/v0.1.9-lbp3port, "LittleBigPlanet 3 Recompiled v0.1.9 (play online with friends on LBPPC)". Tags `v0.1.9-lbp3port` (lightweight): runtime `5dc9742`, shadPS4 `b0242a25`. Assets: the zip and its `.sha256`. Notes from `tools/package/RELEASE-NOTES.md`. Not a draft or prerelease; `releases/latest` names it; the downloaded asset's sha256 is `313be890…`, the tested zip's.
