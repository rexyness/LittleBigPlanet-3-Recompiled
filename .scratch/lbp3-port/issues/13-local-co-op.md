# Local co-op: up to four players with their own controllers on one PC

> **Moved to GitHub:** https://github.com/rexyness/LittleBigPlanet-3-Recompiled/issues/16 (#16) on 2026-11-19. This file is the archive as of the move; work on the issue.

Type: task
Status: moved to #16 (2026-11-19); was: in progress (started 2026-11-11 evening, while the human plays milestone 3)
Blocked by: none
Map: [LBP3 native port](../map.md) (done; the human added local co-op to the offline finish line on 2026-11-11, and this build ticket closes it as gap 20 of the [spec](../spec.md))

## Question

Make a second, third and fourth controller join the game as players 2–4, the way a second PS4 user does on a console, and keep them working through the Pod, Create mode and the story.

## Done when

- With two controllers, a second player joins in the Pod and in a story level (the Introduction or the Prologue). Both players move independently, and rumble and the light bar reach the right controller.
- Unplugging the second controller drops player 2 without a hang or a soft lock, and plugging it back in rejoins.
- Three and four players join the same way: with real controllers if the human has them, otherwise with scripted virtual pads.
- A co-op session leaves player 1's saves and story progress as they were (a save snapshot compare, as `tools/saves.sh` does).
- Milestone 1's checks (`tools/m1_checks.sh`) and milestone 2's scripted checks still pass on the merged `port`.
- Human session: part of the Introduction or the Prologue played with two controllers.

## Comments

- 2026-11-11, **where the port stands** (read from `port` `d534bbe`; nothing run yet):
  - **The input side is ready.** The runtime keeps up to 4 controllers (`runtime/src/input/gamepad.h:8`, `kMaxPads = 4`) and gives each newly plugged one the next free slot (`gamepad_sdl.cpp`, `RefreshSlots`). `scePadOpen` maps user N to slot N−1 (`hle/libScePad.cpp:42`), and rumble and the light bar already go to the right slot.
  - **The game never sees a second user.** `sceUserServiceGetLoginUserIdList` lists user 1 only, and `sceUserServiceGetEvent` sends a single login event for user 1, then reports no events (`hle/system_services.cpp:38-51`). On a PS4 a second player signs in on their own controller, and the game finds out through that event queue. Here the game never opens pad 2, so a second controller does nothing today.
  - **What the eboot imports** from the libraries involved (`analysis\eboot_imports.json`, resolved with the clone's `tools/ps4_nid.py`):
    - `libSceUserService`: Initialize, Terminate, GetInitialUser, GetLoginUserIdList, GetEvent, GetUserName.
    - `libScePad`: Init, Open, Close, ReadState, GetControllerInformation, SetLightBar, SetVibration.
    - `libSceNpManager`: per-user calls such as GetState, GetOnlineId, GetNpId, GetAccountId and RegisterStateCallback. The offline runtime reports every user signed out, but its state callback only ever fires for user 1 (`hle/np_offline.cpp:91-95`).
    - `libSceSaveData`: Mount takes a user id, but the runtime ignores it. Every user's saves would land in `user\savedata\CUSA00473` (`hle/savedata.cpp:43-46`).
  - **Scripted input drives player 1 only.** Remote input returns early for any slot but 0 (`gamepad_sdl.cpp:219`), and the keyboard always belongs to player 1.
- 2026-11-11, **plan** (the agent's recommendations, per the map's standing preference):
  1. **Joining:** each controller is its own user, so there's no account picker. A controller filling slot N (from 1) logs in user N+1, and `sceUserServiceGetEvent` queues a login event. Unplugging it queues a logout. `GetLoginUserIdList` lists the users logged in. Player 1 stays logged in with or without a pad, because the keyboard is theirs.
  2. **Names:** `GetUserName` gives player 1 the Windows account name as today, and players 2–4 "Player 2" to "Player 4".
  3. **Online services:** users 2–4 are signed out like player 1. Fire the state callback for each user that logs in, if the game registers one per user.
  4. **Saves:** first find out whether the game mounts save data for users 2–4 at all. If it does, give each user its own folder (`user\savedata\user<N>\CUSA00473\…`, N ≥ 2) and leave player 1's path unchanged, so existing saves keep working.
  5. **Test hooks:** a player prefix in remote input (`p2 cross`), and `LBP3_VIRTUAL_PADS=<n>` to plug in n−1 virtual controllers, so scripted checks can log in players 2–4 without real controllers.
  6. **Then run it and fix whatever the game calls next** for the new user. The trace starts with the `[np]` and `[savedata]` log lines and every stub's first-call log.
  - **Not in this ticket:** sharing the keyboard between players. Keyboard work past the base is out of scope (map), so a keyboard player can't play alongside a controller player who isn't player 1.
  - **Where it runs:** branch `fix/local-coop` from `port`, built in a worktree under `D:\Recomps\lbp3-wt\` so the clone's `port` build stays as the human has it. It's merged into `port` after the checks. On the ladder it closes with milestone 3, whose play session can be the co-op one. It doesn't wait for milestone 2's human session.
- 2026-11-11 evening, **started** (the human: "go ahead with the co-op while i play then"). Branch `fix/local-coop` from `port` `5d159c4`, worktree `D:\Recomps\lbp3-wt\coop`. Two lanes: the runtime (the plan above: login/logout per controller, names, NP, per-user saves, `LBP3_VIRTUAL_PADS`, a `p2` prefix in remote input, `cooptest`) and a read-only reverse-engineering pass on how the eboot handles a second user (notes in [coop/re.md](../coop/re.md)). Nothing runs in the game until the human's session ends.
- 2026-11-11 evening, **reverse engineering** ([coop/re.md](../coop/re.md)): the pad thread (`eboot+0x65940`, ~12 ms) drains `sceUserServiceGetEvent`; a login alone makes the game open the new user's pad (`scePadOpen(user, 0, 0)`, and type 2, which may fail); a logout closes them. Adding a player is then the game's own "new controller" screen (`ModalOverlayStateNewController`). The main loop rebuilds up to 4 local users from `GetLoginUserIdList` with `GetUserName(user, buf, 17)` (16 characters). Saves and the text keyboard stay with the initial user. Open: what brings up the "new controller" screen, and whether players 2–4 need an identity of their own (the session check at `eboot+0x36c150`).
- 2026-11-11 evening, **runtime built** (`ebe14c1` on `fix/local-coop`, pushed to the fork as a branch; not in the game yet). The human will play with a friend over **Parsec**, whose guest controller shows up on this PC as a virtual Xbox 360 (or DualShock 4) pad.
  - Users per controller: a pad in slot N (1–3) logs in user N+1, removing it logs that user out; a guest reconnect is a logout then a login, back in the same slot. User 1 is always logged in (the keyboard is theirs). Names: user 1's Windows name cut to 16 bytes, users 2–4 "Player 2".."Player 4".
  - Seating: the session's first pad is player 1's (identified by vendor, product and serial; a DualSense reports its MAC over USB and Bluetooth); several pads appearing at once put a PlayStation pad first; while player 1's pad is away slot 0 waits for it; others take the lowest free slot from 1. `LBP3_PLAYER1_PAD=<name part>` pins player 1's pad (e.g. `DualSense`), for when the guest's pad could appear first.
  - Pads: opens and reads for users 2–4; a logged-out user's handle reads disconnected. Every player's pad is neutral while a system dialog is up; the release wait after it uses player 1's buttons only (dialogs read slot 0).
  - NP: each user gets one state notice; guests' `GetNpId`/`GetOnlineId` return signed out, or distinct "OfflinePlayer2".. ids with `LBP3_COOP_NPID=distinct`, to flip in game if player 2 never appears or a level load hangs on the session check.
  - Saves for users 2–4 (if any) go to `user\savedata\user<N>\`; every mount logs its user. Voice audio ports open for every user.
  - Test hooks: `LBP3_VIRTUAL_PADS=<players>`, remote `pads <n>` while running, a `p1`..`p4` prefix on remote input.
  - Offline: `cooptest` 81 checks (SDL virtual joysticks as a DualSense and Parsec-style Xbox 360 pads, seating orders, reconnects), `dialogtest` 368, `osktest` 263, `dlgtest` 154, `jpegtest` 157 pass; `imetest` 403 with its known real-pad flakes while the human holds a pad.
  - Next: with the game closed, scripted runs with a virtual player 2 (does the "new controller" screen come up, can `p2` presses join, does a logout drop player 2 cleanly), milestone 1's checks, then the human and their friend over Parsec.
- 2026-11-11 ~20:15, **in the game with virtual pads** (clone `m3/coop-verify` `144430a` = `port` + `fix/local-coop`; 165 Hz display; the human's saves set aside and restored identical to their 20:09 snapshot `analysis/savedata_backups/human_play_20261111_2009`):
  - `LBP3_VIRTUAL_PADS=2`: `[user] user 2 logged in (slot 1, virtual pad)` at boot, and **a second Sackboy appears in the Pod by itself**: the login alone adds the player (the boot's "problem with your network connection" message is the usual offline one, also there with one player). `p2 lstick` / `p2 cross` walk and jump player 2 independently of player 1 (`m2_coop_3_p2moved.png`).
  - Remote `pads 1` (player 2's pad gone): one logout; player 2's Sackboy slumps where it stands, LBP's disconnected-controller pose. `pads 2`: logged in again in slot 1, the Sackboy stands up, and the game shows its own notice "One or more players are not signed into "PSN". Network features will be unavailable." (OK), as a PS4 does for a local player without an account. Player 2 never needed an identity of their own (`LBP3_COOP_NPID` not needed).
  - **Story mode:** Pod → Play → the Introduction with two players: both spawn, player 2 runs ahead on its own pad, the camera keeps both (`m2_coop_8_level.png`, `m2_coop_9_level_p2.png`). No `Rejecting invalid S#`, stuck wait or device loss.
  - `LBP3_VIRTUAL_PADS=4`: users 2–4 logged in (slots 1–3), four Sackboys in the Pod, players 2–4 each moved by their own prefix (`m2_coop_10_four.png`).
  - Next: milestone 1 and 2 regressions on this build, merge into `port`, then the human and their friend over Parsec (gamepad-only guest permission; Parsec's display = the 165 Hz monitor).
- 2026-11-11 20:27–21:03, **the human's session on the co-op build** (clone checked out on `m3/coop-verify` `144430a`, launched with `LBP3_DISPLAY=165hz`, no pin): `[input] player 1's controller: DualSense Wireless Controller (054c:0ce6:...)`, slot 0. The friend left before connecting over Parsec, so no second controller ever appeared; 35 minutes at a steady 33.3 ms, closed normally. Saves snapshot `analysis/savedata_backups/human_play_20261111_2104`. **Parsec co-op is still untested.**
  - The milestone 1/2 regression on this build was stopped at the human's request partway through (in `m1_checks.sh`), so `port` is **not** moved yet (still `5d159c4`); the clone stays on `m3/coop-verify`, whose build is what `play.bat` runs.
  - **Hazard found:** stopping the chain left orphaned scripts that swapped saves again, and one kept the *test baseline* as `analysis/savedata_human_kept`; a later run would have "returned" it over the human's saves. The human's saves were restored from the 20:09 snapshot and verified; the bad kept copy was deleted. To do: `saves_keep` (saves.sh) and `pace_run_intro.ps1` must refuse to keep saves that are identical to a test baseline (`savedata_thruster_on`, `savedata_water_level`); and when stopping a chain, kill its child scripts too.
  - Next: that guard; rerun the regression (`m1_checks.sh`, backup both ways, `t_m2v_osk.sh`, `t17_story.sh`); merge `fix/local-coop` into `port` and push; then the Parsec session with the friend.
- 2026-11-11 21:12–22:01, **regression and the scripted co-op check on the co-op build, then merged**:
  - Milestone 1: Introduction 0.00% and 0.00% steps ≠ 1 (with `sim_steps_level.py` now never counting the level's first 5 s; one run had read 1.43% because 21 zero-step flips at the story save fell just inside the old window), Thruster recipe 3 of 3, Bunkum 16 of 16 with Needlepoint loading, sky (121, 177, 182). Milestone 2: boot (the eight libraries bound, no stub lines, 0 GPU warnings), level name, Text Chat, photo, backup declining and accepting on the overlay, the keyboard check, story progress and the saved edit all pass.
  - **`tools/t13_coop.sh`: PASS** (`coop_t13b`). One player vs two players through the Pod into the Introduction; player 2 logs in at boot, out when its pad goes and in again, frames keep flipping, no GPU warnings, player 1's save files the same in both.
  - **Correction to [coop/re.md](../coop/re.md):** the game *does* keep saves for player 2. It mounts and creates `LBPxSAVE` and `LBPxSAVExLITTLE` for user 2 (15 mounts, set param, save icon): every PS4 user has their own LBP profile. The runtime's per-user path puts them in `user\savedata\user2\`, so a guest's profile (costumes, progress) persists between sessions and never touches player 1's saves. The first run of the script failed on its own expectation of no user-2 saves; the folders `user2`–`user4` left by the day's test runs were deleted (the human has never had a real second player), and `tools/saves.sh` now keeps and restores the guests' folders around test runs and starts tests without them.
  - Also merged: `play.bat` refuses to start while a test run has the saves set aside (`analysis\savedata_live_is_test`), and archives every session's log in `analysis\play_logs\` (`ad3cc9c`).
  - **`port` = `b5c42c4`** (`fix/local-coop` merged), pushed to the fork; the clone is back on `port`. Ticket 13 stays open until the human plays with a second person (Parsec).
