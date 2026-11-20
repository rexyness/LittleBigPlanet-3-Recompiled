# Release v0.1.0-lbp3port: ready to publish (NOT published)

## Ready (release lane, 2026-11-13 17:55), supersedes the b37790b section

- **Release commit:** bestofboth `01da20e4f564ab11d146ebece9a3bf3ac17effff`, pushed and public. shadPS4 is at `24e4e494`.
  - It is `7c51e60` (feat/shadnet3 merged by the coordinator) plus docs-only commits `83496cc`, `0d0944e` and `01da20e` (co-op: invite and join work, the session may drop after a few minutes; Dive In does not match strangers on play.lbppc.com).
  - Since `b37790b` it gained:
    - packaging: zip entries with forward slashes, build.bat by full path, the commit must be on the fork, RELEASE-NOTES.md shipped;
    - the setup's typed path, and What's new from the release notes;
    - Create account says "ready" only when game logins are allowed;
    - the updater: a loopback test hook (LBP3_UPDATE_TEST_API, 127.0.0.1 only), the .sha256 required, no install while a check runs;
    - `tools/fake_release_server.py`.
- **Zip:** `D:\Recomps\lbp3-dist\LBP3-Recompiled-v0.1.0-lbp3port.zip`, 7,753,437 bytes, 46 entries, built in the dedicated
  release worktree `D:\Recomps\lbp3-wt\release` (branch `rel/v0.1.0`, fast-forwarded into bestofboth; its own shadPS4 worktree and build folder).
  - sha256 `add5893e4ec5dc78718a1bceba348f75673395269bca8a602d97a5f0d3cea03b`, also in `.zip.sha256`.
- **Tests** (reports in `D:\Recomps\lbp3-dist\test-clean3\report`, `test-clean4\report` and `test-clean5\report`):
  - the packaged offline fresh profile reaches the Pod, and Quit Game works;
  - online as Kistopawa3: Home online, no 403, no parental notice;
  - Kistopawa3 was created through the packaged launcher (both servers), and its first sign-in was on a fresh profile;
  - the updater end to end: offer, bad hash, no .sha256, HTTP 500, cut-short download, good install with user\ kept, restart, cleanup;
  - launchertest 370/0, shadnettest 142/0, p2ptest 66/0, onlinetest 20/0.
- The release body is `release_notes_v0.1.0_body.md`, which is the same text as the zip's RELEASE-NOTES.md (tools/package in the repo).

### Publish (one line; PowerShell, cmd or Git Bash). A full release, not a pre-release (the updater reads /releases/latest)

```
gh release create v0.1.0-lbp3port "D:\Recomps\lbp3-dist\LBP3-Recompiled-v0.1.0-lbp3port.zip" "D:\Recomps\lbp3-dist\LBP3-Recompiled-v0.1.0-lbp3port.zip.sha256" --repo rexyness/LittleBigPlanet-3-Recompiled --target 01da20e4f564ab11d146ebece9a3bf3ac17effff --title "LittleBigPlanet 3 Recompiled v0.1.0 (first test release)" --notes-file "D:\Recomps\lbp3-pc\.scratch\lbp3-port\best\release_notes_v0.1.0_body.md"
```

### After publishing, before the link goes to friends: the real GitHub update path (about 5 minutes)

The end-to-end updater test ran against a fake server on 127.0.0.1, over plain HTTP and without redirects.
GitHub serves assets over HTTPS through a redirect, and the `.sha256` read has a fallback for exactly that,
which has never run against GitHub itself. The README promises that the co-op fix comes through the updater,
so check the real path once:

1. Check that `https://api.github.com/repos/rexyness/LittleBigPlanet-3-Recompiled/releases/latest` names `v0.1.0-lbp3port`.
2. Unzip the release zip into a new folder and change `SOURCE.md`'s `Package version: v0.1.0-lbp3port.` to
   `Package version: v0.0.9-test.`. Start `LBP3Launcher.exe` with no LBP3_* variables (no test hook), skip the setup.
3. Go to Tools > Check for updates. It must say "v0.1.0-lbp3port is out (you have v0.0.9-test)". Then use "Download and install":
   it must download, pass the .sha256 check, install, restart and show v0.1.0-lbp3port. On the next start it must say "Up to date".
4. If any step fails, delete the release and its tag (`gh release delete v0.1.0-lbp3port --cleanup-tag --repo
   rexyness/LittleBigPlanet-3-Recompiled`) before anyone downloads it, then fix the problem.

## History (the earlier plan)

## How to publish (the coordinator, after the launcher v3 merge and a final check)

1. Merge `feat/package` into `bestofboth` together with the launcher v3 merge. Build, then run the offline tests.
2. Build the package from that exact commit, in the `bestofboth` worktree with a clean tree:
   `powershell -NoProfile -ExecutionPolicy Bypass -File tools\make_package.ps1 -Version v0.1.0-lbp3port`
   - It writes `D:\Recomps\lbp3-dist\LBP3-Recompiled-v0.1.0-lbp3port.zip` and `.zip.sha256`.
   - It refuses to zip if anything from the game, saves or accounts slipped in.
   - It refuses a dirty tree, so `SOURCE.md` names a real commit.
3. Push `bestofboth` to the fork, so `SOURCE.md` points at a published commit.
4. Check the release zip:
   - `SOURCE.md`'s commit equals `<COMMIT>` (`git rev-parse HEAD`);
   - the `.sha256` matches;
   - `README-FRIENDS.md`'s "Create account" is native in the merged launcher (in this branch's launcher it still needs Python and the tool script, which aren't in the zip).
5. Publish as a normal release, NOT a pre-release, with the tag on that commit. Two reasons:
   - The updater (spec item 1) reads `GET /repos/{owner}/{repo}/releases/latest`, which returns the newest non-prerelease, non-draft release, so a pre-release would be invisible to it.
   - The title already says "test release".

```
gh release create v0.1.0-lbp3port ^
  "D:\Recomps\lbp3-dist\LBP3-Recompiled-v0.1.0-lbp3port.zip" ^
  "D:\Recomps\lbp3-dist\LBP3-Recompiled-v0.1.0-lbp3port.zip.sha256" ^
  --repo rexyness/LittleBigPlanet-3-Recompiled --target <COMMIT> ^
  --title "LittleBigPlanet 3 Recompiled v0.1.0 (first test release)" ^
  --notes-file "D:\Recomps\lbp3-pc\.scratch\lbp3-port\best\release_notes_v0.1.0_body.md"
```

`gh auth status` was checked read-only: signed in as the repo owner, with the `repo` scope. The repo is public,
a fork, default branch `main`, and has no releases yet.

State on 2026-11-13:
- `feat/package` is pushed to the fork at `e393228` (from `bestofboth` `481589c`).
- The shadPS4 submodule commit `24e4e494` is already public on rexyness/shadPS4 (checked with `gh api`).
- A test zip built from `feat/package` passed the clean-folder test (`best/package.md`). It is not the release
  zip: rebuild it from the merged commit (step 2).
- **For the human to decide:** `licenses\LICENSES.md` says the binaries as built are distributed under
  **GPLv3**, and the zip carries `GPL-3.0.txt`. The reason is that abseil and the Vulkan headers (both linked)
  are Apache-2.0, which is compatible with GPLv3 but not with GPLv2 only. Every GPL part is "or later", so this
  is allowed, but it goes beyond "GPL-2.0-or-later". To keep the notice at GPL-2.0-or-later instead, edit
  `tools/package/LICENSES.md`.

The release body (the text shown on GitHub) is `release_notes_v0.1.0_body.md`.
