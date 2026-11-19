# v0.1.3 (2026-11-14, late morning)

> Released at your go: https://github.com/rexyness/LittleBigPlanet-3-Recompiled/releases/tag/v0.1.3-lbp3port. Your own build is on it too.

- **Exact maths online** (from LBPPC): AMD and Intel players stay in step. **Both co-op players need v0.1.3**; an older version is refused when it connects.
- **Build new shaders in the background** (launcher, Graphics & sound > Shaders; off by default): no first-visit freezes, things can appear a moment late instead. From empty caches the worst freeze in the Needlepoint walk went from 1.75 s to 0.14 s.
- **Still to do:** the regression on the combined build (milestone 1's checks), which you put off for later. Details: perf/lbppc_ports.md, best/package.md.

---

# Loading screen and LBPPC's resolutions (2026-11-14 morning)

> Before bed you asked for a loading screen instead of the ~40 s black window while the shaders load, and for "the same resolutions that lbppc has". Both are done, tested and in the build you play: the launcher you always click (`lbp3-wt\release\build\LBP3Launcher.exe`) and `lbp3-wt\best`. Nothing else about how you start the game changed.

## The loading screen

- **What you'll see:** from the moment the window opens, the game's own PS4 background art and icon (read from your dump), "LittleBigPlanet 3", what it's doing ("Loading shaders 3,296 / 7,022 46%") and a progress bar. It fades into the game's first screen. On a slow start it adds a line saying the first start after a PC restart takes longer.
- **The slow start itself should be faster too.** The shader cache used to be read one file at a time. Now, when the first 128 reads come in slow, the rest is read on 16 threads at once. On stand-in "cold" files (freshly written copies, not a real restart), reading dropped from **75.6 s to 11.8 s**. Fast starts are unchanged (5.3 s either way), and there it doesn't start. One ordinary boot tonight did start it (files pushed out of memory by the tests) and read in 12.3 s. **Please send me the `[gpu] pipeline preload` lines from `analysis\play_best_log.txt` after your next start following a PC restart**, so I can see the real number.
- **Switches** (only if something looks off): `LBP3_BOOT_SCREEN=0` gives the old black window, `LBP3_PRELOAD_PREFETCH=0` the old one-at-a-time reads.
- Details: `perf/boot_screen.md`.

## The resolutions

- **Where:** the launcher's Graphics & sound page (and Home's quick settings) now have a Resolution dropdown with LBPPC's nine sizes: 1280x720, 1920x1080, 2560x1440, 3840x2160 (4K), 2560x1080, 3440x1440, 3840x1600 (21:9), 3840x1080, 4480x1260 (32:9). Your setting stays 2560x1440, and your shader cache still matches it (the 1440p patch is byte-for-byte the same as before).
- **Wide sizes** widen the view, as on a real ultrawide (the menus too). On your 16:9 monitors they show with black bars above and below.
- **4K** works, but heavy scenes drop below 30 fps (the Needlepoint walk: 120 frames over 40 ms; the Pod and Create mode hold 30). On your 1440p screens it's downsampled, so it's sharper, not bigger. 1440p is still the sweet spot.
- **Online with a wide size:** the game's own camera stays 16:9 for the simulation (as LBPPC does it, to keep two players' worlds in step) and only the picture is widened. The offline test shows the picture is wide. It doesn't show whether two players stay in step; that needs a two-player session with one wide player moving for a few minutes. Until then, a wide size online is untested, and it shouldn't go into a release for friends.
- **Wide sizes take wider photos** (stickers and level icons up to 1280x360). They save and show fine.
- `play_best.bat` takes the sizes too (`RES=3440x1440` and so on).
- Details: `perf/resolutions.md`.

## What was tested

- Every size in game, with frame generation on: the Pod, the Pod computer, Edit Now, the pause-menu fade, a photo, the route into Needlepoint Peaks and its heavy walk. All pass, with 0 GPU errors and 0 crashes.
- The combined build (loading screen plus resolutions): 1440p, 4K and 3440x1440 again, and the full regression at the defaults. The keyboard, photo, backup dialogs and co-op all pass; Introduction 0.00% / 0.13%, Thruster 3/3, Bunkum 16/16 with Needlepoint, the Create sky cyan, no driver errors. The loading screen came up and handed over in every boot (about 6.5 s).
- Your saves were set aside and put back for every test run. The files in place now are the ones your last session wrote (its log ends on `littlefart1786` and `playfart_12_349_134`). A fresh copy is in `analysis\savedata_backups\human_play_20261114_0452_full`.
- The new launcher's Play was checked with its `--print-launch` mode against your settings: the same exe, 1440p, cache and online settings as before. It hasn't been clicked for real.

## Where it is

- Fork `main` = `3b0b958`, shadPS4 `lbp3` = `54abc447` (pushed). Branches `feat/boot-loading` and `feat/resolutions` are on both forks too.
- Your previous game exe is `lbp3-wt\best\build\lbp3.exe.pre_bootscreen_20261114_0345`, and your previous launcher is `lbp3-wt\release\build\LBP3Launcher.exe.pre_res_20261114_0447`. Copy either back to undo.
- **Released as v0.1.2-lbp3port** (2026-11-14, at your OK): https://github.com/rexyness/LittleBigPlanet-3-Recompiled/releases/tag/v0.1.2-lbp3port. Friends on v0.1.1 get it from the launcher's Tools > Check for updates. Details in `best/package.md`.

---

# Best of both worlds (2026-11-13 morning)

> You asked for "the best of both worlds" before you went to sleep. It's a new branch: our port plus the best ideas from LBPPC, the other LBP3 PC port. It's ready to play. Your normal `play.bat` and `port` are unchanged, so you can switch between the two.

## How to launch it

Double-click **`D:\Recomps\lbp3-wt\best\play_best.bat`**. It uses your normal saves and game folder, and its own copy of the shader cache, so it never mixes with `play.bat`'s. Its log is `analysis\play_best_log.txt`, and every session is also kept in `analysis\play_logs\`.

## What's new compared to `play.bat`

- **An in-game menu:** press **Insert**, or **L3+R3** together on the pad. It shows the frame rate and latency, and lets you turn frame generation on or off, set the volume, show a small stats corner, or quit.
- **"Quit Game" in the game's own pause menu.** It replaces the greyed-out Good Grief! button; press it twice. This uses LBPPC's script edit and needs Python. The game files are never changed; the edit is made in memory.
- **All 22 languages:** set `LANGUAGE=` in the file (`de`, `fr`, `es`, `ja`…). The US edition normally hides most of them.
- **Better motion controls:** the gyro's slow drift is learned away (0.4° over a minute instead of 94° in the test), and the tilt settles towards gravity.
- **Number-pad tilt** (4/6/8/2) for pads without motion sensors.
- **The mouse as the touch pad,** for touch Create mode. It's off by default (`MOUSE_TOUCH=1` turns it on, and F7 captures the mouse).
- **Faster startup:** cached shaders are rebuilt on all CPU cores, and the graphics driver's own cache is saved to disk (both from LBPPC). On a normal start the cache load takes about 4 s.
- **A finer glow at 1440p:** the game's bloom is drawn at the higher resolution too.
- **Frame generation keeps the map's spinning "Loading…" text readable.**
- **A save import tool** for decrypted saves from a real PS4: `tools\ps4_save_import.py` in the branch folder.
- **A crash fix:** a rare line-drawing case that crashed the game is now drawn safely. The overnight level tour found it.

## The switches

Everything new has a line at the top of `play_best.bat`, with a short explanation:
- the `play.bat` settings: `FRAMEGEN`, `RES`, `MY_UNLOCKS`;
- display: `FG_TEXT`, `BLOOM_1440`, `STATUS_FLIPS`;
- startup: `PIPELINE_THREADS`, `VK_PIPELINE_CACHE`;
- menus: `MENU`, `STATS`, `QUIT_MENU`, `LANGUAGE`;
- controls: `MOTION_FILTER`, `TILT_KEYS`, `MOUSE_TOUCH`;
- online: three lines, commented out (below).

Set any of them back to the old value if something feels off.

## One thing to expect

**The first start after a PC restart can take 25–40 s,** while Windows reads the roughly 10,000 small shader-cache files from disk. Starts after that are back to about 4 s. `play.bat` has the same first-start cost; it isn't new to this branch, and it was measured here: 25 s cold against 0.8 s warm for 6,168 files. Reading those files in parallel would shorten it, and that's a follow-up.

## Online (off; your choice of server)

The branch can play on a community LittleBigPlanet server: your profile, levels and comments, like LBPPC does. It's **off**, and **no server is built in**: which server to use is your decision.
- **Setting it up:** `D:\Recomps\lbp3-pc\.scratch\lbp3-port\best\online.md` covers it. Choose a server, then remove `rem ` from the three `ONLINE` lines in `play_best.bat` and fill in the server and your online name.
- **The server options:**
  - **LBPPC's public server:** please ask them first, since it's their service.
  - **Your own Refresh server:** that means downloading and running their server, which I'd need your OK for.
- **Tested:** only against a fake local server. It signed in and reached the Pod with no hang, and switching it off leaves the game exactly as before. It has never been tried against a real server.

## Not done yet

- **Online friends and joining** (shadNet, the PSN stand-in): several sessions of work.
- **A lower-latency frame limiter** (LBPPC's idea): built and measured, but in our port it didn't change the latency at all, so it isn't in the branch.
- **The shader warm-up tour:** it can now jump straight into any story level, and one sweep loaded 52 of the 54 entries for about 20 s each. That added 712 pipelines to this branch's cache: the hubs, cutscenes and the first stretch of every level, including Ziggurat and Bunkum. Later parts of levels can still freeze the first time. It's a partial fill, not a full compile step yet.
- **Remapping keyboard/mouse to pad buttons:** not ported; low value with a DualSense.
- **Motion with your real DualSense:** only tested in the offline test so far. Tell me how tilting feels.

## What was tested

- **The full regression, at the game's defaults and at `play_best.bat`'s settings:** every check passed, with no GPU driver errors.

  | | Defaults | play_best (1440p, frame gen, Quit Game) |
  |---|---|---|
  | Introduction camera (frames with 0 or 2 updates) | 0.13% / 0.00% | 0.50% / 0.00% |
  | Thruster recipe | 3/3 | 3/3 |
  | Bunkum, then Needlepoint loads | 16/16 voice lines | 16/16 voice lines |
  | Create sky | blue | blue |
  | Photo, save backup, keyboard, co-op | all pass | all pass |
  | Heavy Needlepoint walk | 18.0 ms work, 0 frames over 40 ms | 21.3 ms work, 0 frames over 40 ms |

- **15 offline test programs pass.**
- **The launcher itself:** one real boot of `play_best.bat` on your saves, closed cleanly. Your saves only got the game's usual profile rewrite (backup `analysis\savedata_backups\human_play_20261113_0830`).
- **Each feature was also tested in game by the lane that built it.** Details for each are in `best\README.md`.

---

# Morning (2026-11-13): what happened while you were out

> You went out around 22:00 and said to keep going. Nothing was deleted, brightness stayed parked, and your saves were only ever touched by the game itself (latest backup `analysis\savedata_backups\human_play_20261113_0046`).

## Merged into `port` and pushed (`34e3e8d`, shadPS4 `553be3ba`)

- **The whole current build passed a full check** before anything new went in. That covered the Introduction camera, Thruster 3/3, Bunkum, the blue sky, photos, save backup, the keyboard, co-op and the heavy Needlepoint route, both at the game's defaults and at your `play.bat` settings, with no GPU errors.
  - **Photos at 1440p are right:** same framing and colours as at 1080p; I looked at the picture.
  - **Options didn't crash again** in about 45 presses over 4 sessions, with and without the unlock switch.
- **One shared shader cache for 1080p and 1440p.** The 1440p build used to keep a whole separate cache, so levels first played at 1080p froze again at 1440p. Now only the 17 shaders the 1440p build modifies are kept apart. Your cache was merged and installed:
  - the old one is kept as `user\shader_cache\CUSA00473_pre_shared_20261113_0043`;
  - the old 1440p folder is untouched;
  - the cost is about 0.65 s longer to reach the Pod;
  - `LBP3_RES_CACHE=separate` brings back the old behaviour.

- **The LBP1/LBP2 import can't be cancelled by accident any more.** The two "failed" imports on fresh test profiles weren't a bug: Circle was pressed during the import, and on this US version Circle is the import's cancel button. While an import started by `MY_UNLOCKS=1` is on screen, the port now hides Circle from the game, so the import always finishes. Without the switch, nothing changes. It was checked in game, and the keyboard, co-op and save-dialog tests still pass.

## Waiting for you to try (branches with launchers, not in `port`)

- **"Loading…" text in frame generation:** `D:\Recomps\lbp3-wtgtext\play_fgtext.bat`. The spinning text on the Prologue map's level node stays readable in the in-between frames, and nothing else looks different. It costs about 0.4 ms of GPU per generated frame. `FG_TEXT=0` in that file gives today's behaviour, to compare. Say "merge the text fix" if you like it.

## Being looked at: 1440p slowdown in the Diner

- **What your logs showed:** in your first 1440p session, one stretch of the Diner (after Marlon's dialogue) ran at up to 89 ms a frame for about 1.5 minutes, about 13–25 fps. At 1080p the same stretch was a steady 18–24 ms. The scripted tests only cover Needlepoint, so they missed it.
- **Most likely cause:** the GPU is over budget at 1440p there. Measuring it in game was stopped when you said to stop. It resumes from the plan in `perf/diner_1440.md` whenever you want. If you notice slowdowns at 1440p in that spot, `RES=1080` in `play.bat` avoids them until it's fixed.

## Found and held

- **27 more shaders need fixing for 1440p.** A scan of all 135,428 shaders in the game found them, mostly the outline highlight and x-ray silhouette effects. They're on a branch (`fix/res1440-shaders-44`) because none of them ever came up in a test or in your play, so the fix is unseen. If you ever see a stretched or offset outline or x-ray effect at 1440p, tell me where.
- **First-visit freezes happen once per level, ever.** Your shader cache already persists between sessions. 30 story levels are still unvisited, at about 0.3–2 s once each. Background compiling would only help those, so it's still your call.
- **The LBP1/LBP2 import failed on two fresh test profiles** ("Failed to import objects"), though it worked on yours. It only matters for a new profile; it's noted in ticket 19.

## Still yours

- Finish Manglewood, co-op over Parsec, and the rest of the story.
- Background shader compiling: yes or no.
- Cleaning up: about 30 test copies of the port under `D:\Recomps\lbp3-wt\` and several GB of captures in `analysis\`. I'll only delete with your OK.

# While you were away (2026-11-11, night)

> You went out around 21:10 and said to keep going. This page is the short version; the details are in tickets [13](issues/13-local-co-op.md), [14](issues/14-milestone-3-prologue-first-world.md) and [15](issues/15-milestone-5-performance.md). It gets a final update before I stop.

## Your saves

- Your saves are exactly as you left them (checked against the 21:04 snapshot `analysis\savedata_backups\human_play_20261111_2104` after every test block).
- **New safety net:** test scripts now refuse to treat test saves as yours (a marker, `analysis\savedata_live_is_test`, is set while test saves are in place), and `play.bat` won't start while a test has your saves set aside. This came from a near miss earlier: stopping a test run halfway left a test copy that a later run would have put back over your saves. It was caught and your saves restored before anything was lost.
- Every play session's log is now also kept in `analysis\play_logs\` (the old rotation had already dropped your 14:11 session).

## Co-op is in `port` (pushed)

- All of milestone 1 and 2's checks pass on it, and a new scripted check (`tools/t13_coop.sh`) passes: player 2 joins at boot, leaves when its pad goes and comes back, in the Pod and in story mode.
- **Player 2 gets their own LBP profile**, saved in `user\savedata\user2\` (the game does this for every player, like a PS4). Your saves are never touched by it. The test profiles I created were removed, so your friend starts fresh.
- Still waiting on you: playing with your friend over Parsec.

## Milestone 5 (performance): two fixes merged and pushed (`port` `2d92d65`)

- Your sessions showed the heaviest levels (Needlepoint Peaks, Stitchem Gate and Manor) running over the 30 fps budget, and 0.5–1.4 s freezes at level entry.
- **Why it was slow:** each frame ran the game's code (~6 ms), then recorded it for the GPU (~7 ms), then let the GPU draw it (~11 ms), strictly one after another.
- **Fix 1:** the GPU now starts on each finished part of the frame while the rest is recorded. On the Needlepoint route the frame's work fell from 27.4 to 22.5 ms while walking (and 24.4 → 19.3 standing), so the heaviest parts you played (33–34 ms) should now fit in the budget.
- **Fix 2:** a wait before each frame held the game for 20 ms eight frames in a row once per session (a hitch you may have felt early in a level). It's gone: frames over 40 ms on the walk went from 9 to 1.
- Every check from milestones 1 and 2, co-op included, passes on it. Turn either off with `LBP3_GPU_FLUSH=off` or `LBP3_SUBMIT_WAIT=all` if something looks wrong.
- **The freezes at level entry** are your graphics driver compiling shaders the first time it sees them; the second visit to a level has none. The only way to remove the first-visit ones is to compile in the background and skip the affected objects for a frame or two (what most emulators do). That's a visual trade-off, so it's **your call**; I haven't started it.

## A rendering bug found and fixed (`port` `d84eae8`, pushed)

- One of the game's drawing paths uses a GPU stage (geometry shaders) the port never set up, so 13–60 draws a frame were silently skipped (gap 15). They scale with the characters on screen, so they're most likely the fabric "fins" on Sackboy and costumes (mostly depth and motion-blur passes).
- Simply turning the stage on crashed the GPU driver (it recovered by itself; three short resets during testing). The cause: shadPS4 told the GPU each of these shaders could output 64 vertices, which goes over your RTX 3090's limit; they really output 4. That's fixed, and a guard now skips and logs any such shader that would go over the limit instead of letting the GPU fault.
- Now on by default: 0 skipped draws, no GPU faults, and every check passes. **Have a look at Sackboy in motion** next time you play; still screenshots don't show the difference. `LBP3_GS_STAGES=0` turns it off if anything looks wrong.

## Also measured

- **Your Introduction stalls** (1 s at Newton's entrance, 2.5 s after his save) didn't happen in 5 scripted walks through the same spots. The level-load pauses are the game's own loader, not our file reading.
- **The first frame of a new scene** (the Pod at boot, the Prologue map) took 150–300 ms on our side. At the Prologue map it was the game's lighting volumes being re-uploaded whole every time they grew by one layer (144 times in one frame). **Fixed and pushed (`port` `d799245`):** that frame now takes ~97 ms instead of ~190, and looks the same. `LBP3_EXPAND=old` brings back the old way.
- **Half-drawn frames, fixed and pushed (`port` `64d6252`):** the port marks "show this frame now" with a marker in the game's command memory and never removed it; when the game reused that memory, the old marker could fire mid-frame. That showed the Pod's first frame, and about nine frames in every 2.5 minutes of a level, before they were fully drawn (a brief flicker at most). Now the marker only counts where it was put, and the runtime's filler no longer leaves old markers behind: none left in testing.
- **GPU clears instead of uploads, fixed and pushed (`port` `bac46c2`):** in levels the game clears its render targets on the GPU every frame, and the port turned each clear into a full re-upload (26 images, ~105 MB a frame). Now a whole-image clear stays a clear: 26 → 3 re-uploads a frame, about 1–1.3 ms less work per frame. Screenshots at the spots that broke before (the Introduction fog, the Pod) match. `LBP3_RT_FILLS=old` brings back the old way.

## Where it stands (Thursday morning)

- **Faster memory protection, fixed and pushed (`port` `0570df8`):** the runtime asked Windows about a memory region before each of ~1,100 protection changes a frame; now it changes the protection directly and only asks when that fails (it never did, in ~27 M calls). The game's fluid jobs and main thread got faster with it: walk work ~21 → ~18 ms, the worst GPU wait 17 → 4 ms. The Thruster recipe still passes 3 of 3. `LBP3_PROTECT=old` brings back the old way.
- **`port` = `0570df8`** (shadPS4 submodule `d5af2ae7`), pushed to both your forks. It has everything above. Every check from milestones 1 and 2, co-op included, passed on each step, with no GPU errors.
- **Frame work on the heavy Needlepoint walk: 27.4 ms → about 18 ms**, frames over 40 ms 9 → 0. The heaviest parts you played (33–34 ms) should now fit in the 30 fps budget; worth feeling for yourself.
- `play.bat` starts this build; every session's log is now kept in `analysis\play_logs\`.

## Your calls

1. **Shader freezes on first visits:** compile in the background and skip new objects for a frame or two, like most emulators, or keep the short freeze. I haven't started it.
2. **Co-op with your friend over Parsec:** ready whenever you are (give them gamepad-only permission in Parsec).
3. **Look at Sackboy in motion** for the restored fabric fins, and tell me if anything looks off.

## Not done (for later)

- The GPU still waits ~2.5 ms on the game's fluid jobs in about half the heavy frames. The next safe-ish lever is fewer write-tracking faults on the fluid buffers (needs screenshot checks); letting the GPU skip the wait is the class of bug that froze the Thruster (ticket 10). Details in `perf/gfx_wait.md`.
- The Pod's first frame still copies ~260 MB of memory the game never wrote; skipping that is doable but riskier (missed writes would show as black textures).
- Tonight's worktrees in `D:\Recomps\lbp3-wt\` are left in place (all merged or pushed as branches); I didn't delete them unattended because some contain links into the clone's shadPS4 folders.

---

# Earlier: while you were away (2026-11-10)

> **Planning is finished** (evening of 2026-11-10): every ticket is resolved and the [port spec](spec.md) is final. **Milestone 1 is done the same evening** ([11](issues/11-milestone-1-foundation.md)): `port` is built and passes all five checks; pushed to both your forks with your OK. Next is milestone 2, Create mode. The rest of this page is the record of that day.

## Corrections first

1. **My scripted "Introduction" runs were the Legend of Bunkum cutscene.** After your play session the Prologue map's cursor starts on that node, so my route played it. I re-measured in the real Introduction (`00_craftworld`; the route now presses left on the map, `tools/pace_run_intro.ps1`). The ticket 09 numbers below are the new ones.
2. **Every screenshot I took was cropped.** The capture scripts weren't DPI-aware. At 125% Windows scaling they saved only the top-left 80% of the 1280×720 window.
   - **Gap 7 isn't a bug.** The Popit's Tools Bag page sits fully on screen at 72.7% of the width; a PS4 shows 72.8%.
   - **The "top black band" isn't a bug either.** It is the game's own symmetric border, and one PS4 video shows the same border.
   - The capture tools are fixed ([patch](patches/tools-capture-dpi.patch)).
3. **My Thruster soaks covered only 5 minutes.** After 5 minutes without a button press the game swaps the scene for its attract video, so my "8-minute" soaks were 5 minutes of Thruster. The soak script now presses a button once a minute and takes a screenshot each minute, and a 60-minute soak with every fix on passed with the Thruster in view throughout. It ran at 33.3 ms on the 165 Hz display, so it doesn't replace any of your sessions.
4. **A side finding of mine was wrong.** I had noted that `sceKernelPollSema` doesn't take the count. It does, so that gap candidate is withdrawn.

## Fixes ready

Each fix is a built branch in `D:\Recomps\lbp3-recompiled` plus a patch in [patches/](patches/). Nothing is merged or published.

| Problem | Branch | Result |
|---|---|---|
| Thruster freeze on save (deadlock), and the GPU reset in the same recipe (tickets 10, 07) | `fix/gfx-submit-wait` (clone and shadPS4) | Old wait: 5 of 5 deadlocked (2 of them on the final diagnostic build) and 2 of 2 reset. Fix: 14 of 14 OK (3 of them with all fixes merged) |
| Camera snaps (ticket 09, gap 4) | `fix/epoll-precise` | Real Introduction, level only: sim steps ≠ 1 fell from 12.1% and 9.8% to 0.31% and 0.12%; camera hitches from 14 and 17 to 2 and 2 |
| Create sky has red and blue swapped (gap 6) | `fix/clear-swizzle` (shadPS4) | The sky is PS4 cyan; the floor and walls are unchanged |
| Legend of Bunkum ends on a white screen (gap 8) | `fix/guest-fpu` | All 16 voice lines play, and Needlepoint Peaks loads by itself. 5 of 5 with the fix, 2 of 2 failures without |
| Upstream's `gamedir.py` tools fail on a merged dump (gap 2) | `fix/gamedir` | `boot_steps.py` and `farc.py` work on your dump |

All of it was measured on the 165 Hz display, because the 240 Hz one is off.

## What I need from you

1. ~~**Turn the 240 Hz display back on.**~~ Done.
2. **Three play sessions.** I launch each one with `tools/hitl_start.sh <tag> <preset>` (it checks that the diagnostic build is running) and watch with `tools/watch_freeze.sh`; you play.
   - ~~**Ticket 07**~~ **done 2026-11-10:** with the old code, a GPU reset about 11 s into the Thruster scene, the moment you fired water; with the fix, about 3.5 minutes of Thruster at fast pacing and no reset (you think you fired water then too; you certainly did in the ticket 10 session, also clean). 07 closes with ticket 10's fix.
   - ~~**Ticket 10**~~ **done 2026-11-10:** with every fix on at 240 Hz you added 3 Thrusters and water, fired them, and the level saved; no deadlock and no reset in about 23 minutes (10 Comments).
   - ~~**Ticket 09**~~ **done 2026-11-10:** two walks at 240 Hz. The camera is smooth between transitions; the stutter you felt at transitions is load stalls of 1–2.5 s (Newton's entrance, tutorial videos), now under gap 10 for the performance milestone.
3. ~~**Listen to the intro once**~~ **done 2026-11-10:** "intro plays perfectly".
4. ~~240 Hz reruns~~ **done 2026-11-10:** the Thruster save recipe 6 of 6 OK with the fix (3 at normal pacing, 3 at your fast pacing); the camera A/B 0.30/0.44% steps ≠ 1 with the fix against 10.53/12.55% without. Next: finish the milestone ladder (04) and the spec.

## State of things

- **Your saves are safe.** Your newest level (3 Thrusters and water, from the ticket 10 session) is your current save, backed up in `analysis\savedata_user_after_t10_hitl1`, and the newest snapshot (after the ticket 09 walks) is `analysis\savedata_user_before_reruns`. My test scripts start from the older one-Thruster copy (`analysis\savedata_thruster_on`), keep your current saves aside, and put them back when they exit (`tools/saves.sh`). Only the small rolling settings file changes on each boot.
- **The clone** is on `diag/gpu-checkpoints`, built. The clean fix branches are listed above.
- **The 7-second pause at boot** is the game's own: 3 s on "Connecting to LittleBigPlanet™3 online…", then a 2 s pause after each of the Sony and Sumo logos. A PS4 with this dump does the same. The test launcher's `--skip-intro` blanks the logos, so the pauses ran together into what looked like one black stall. Level loads stall single frames (0.2–0.35 s when a level starts, rarely up to 1 s), not seconds. Recorded for the performance milestone.
- **The intro movie** (logos, then the 90-second live-action intro) plays to the end at the right speed with steady frames, its sound port runs clean, and it hands over to the Pod (which shows the expected offline network message). Someone still needs to listen to it once.
- **One more gap 8 fix:** the PS4 floating-point state is now set before the first modules start, not only before the game's entry point. The Legend of Bunkum still plays through with it.
- **Gap 9** (Sackboy launched out of Needlepoint Peaks) didn't happen in three spawns or a 70 s walk with jumps. Upstream saw it during slow stretches with the old pacing, so the Prologue playthrough (milestone 3) is the real check.
- **Saves:** a level the port saved (the Thruster recipe's Save Changes) loads back intact after quitting and relaunching. Story progress and edited levels are still unchecked.
- **Ticket 07's fallback works:** `LBP3_SKIP_SHADERS=9f99bb0401d9cc34` removes just the Thruster's flame, so if resets continue we can test without that pass.
- **Milestone 1 dry run:** on a throwaway branch (`try/all-fixes`, not `port`) all six fixes merge cleanly and pass the four scripted checks together: the Introduction at 33.3 ms with 0.25% steps ≠ 1, the Thruster save recipe 3 of 3, the Legend of Bunkum played through, and the cyan sky. A 60-minute Thruster soak with every fix on (the `all` preset on the diagnostic build) also passed: no frame over 40 ms after the level loaded. What remains for milestone 1 is redoing those merges on `port` and your 10-minute Create session.
- **Published 2026-11-10, at your request:** public forks `rexyness/LittleBigPlanet-3-Recompiled` (all 9 work branches) and `rexyness/shadPS4` (the 4 branches whose commits the game branches pin), and this planning repo as private `rexyness/lbp3-pc`. No pull requests upstream yet (your call, "not yet"). The game branches' `.gitmodules` still points at PurpleGitty's shadPS4; milestone 1 repoints it on `port`.
