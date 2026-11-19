# My Unlocks: LBP1/LBP2 content in LBP3

> **Moved to GitHub:** https://github.com/rexyness/LittleBigPlanet-3-Recompiled/issues/22 (#22) on 2026-11-19. This file is the archive as of the move; work on the issue.

Type: research, then a build (static analysis, then tested in game)
Status: moved to #22 (2026-11-19); was: built 2026-11-12 on `feat/my-unlocks` `0c7200c` (pushed to the fork, not in `port`). `LBP3_MY_UNLOCKS=lbp1,lbp2` runs the game's own import offline, and it was tested on test saves. The user launcher is `D:\Recomps\lbp3-wt\unlocks\play_unlocks.bat`. (First answer, the same day: My Unlocks can't grant per item, its evidence is a server record, and LBP1/LBP2 DLC already goes through ticket 18's add-on path.)
Map: [LBP3 native port](../map.md). Follows [18](18-dlc.md). The human said My Unlocks unlocks their LBP1 and LBP2 content, and that it is greyed out because they aren't connected to PSN. They want that content unlocked "the same way the DLCs are". The boundary from ticket 18 still holds: grant only what the user's own packs or files show they own.

## Question

What is My Unlocks, what greys it out, what does it check and unlock, and can the LBP1/LBP2 content be granted per item through the same path as the LBP3 packs?

## Answer

Labels: **[dec]** decompiled from the eboot, **[data]** game data or strings, **[log]** the user's session log, **[inf]** inferred.

### What the page is
- **Its text [data]:** `american.trans`, key `0xc5cf71dd` "My Unlocks", with:
  - description `0xadc76a81`: "Bring all the lovely goodies you've found from previous LittleBigPlanet games into LittleBigPlanet 3";
  - wait message `0x2c89c9ea`: "We're checking for any digital LittleBigPlanet games you own…";
  - entries `0x4143ea6` "Unlock LittleBigPlanet Content" and `0xc7deb75f` "Unlock LittleBigPlanet 2 Content";
  - six per-world entries ("Unlock Avalonia / Da Vinci's Hideout / Eve's Asylum / The Cosmos / The Factory of a Better Tomorrow / Victoria's Laboratory Content", described as "Unlocks all content from LittleBigPlanet 2 – <world>").
- **Its script [data]:** the settings menu script `gamedata/scripts/modaloverlaystatesettings.ff` (zlib, decompressed) has `DoUnlockSettings__Q5Gooeyi`. It calls the natives `HasLBP1__`, `HasLBP2__i` (locals `lbp1`, `lbp2Any`, `lbp2DaVinci`, `lbp2Victoria`, `lbp2Factory`, `lbp2Avalon`, `lbp2Cosmos`, `lbp2Eve`), `IsOnline__`, `IsPlayGoInstallComplete__`, `PlayGoCheckModalOverlay__`, and `ActivateImportScreen__i` with `ELBPImportProgressionFlags`.
- **The natives [dec]**, found through the {function, name} pairs in the relocation table:

  | Native | Address | Implementation |
  |---|---|---|
  | `HasLBP1__` | `0x822ba0` | `FUN_007d8d10` |
  | `HasLBP2__i` | `0x822bc0` | `FUN_007d8d90` |
  | `ActivateImportScreen__i` | `0x836fb0` | itself |
  | `IsOnline__` | `0x80cf80` | `FUN_007e43f0`, which returns the global `DAT_0130d8f0` |

### What it checks: the LBP server, not PSN entitlements or add-ons [dec]
- **The flags:** `HasLBP1` reads byte `+0x3a` of the local user's record in the online-user cache (`DAT_0119a588+8`, looked up by `FUN_004e23f0`). `HasLBP2` reads byte `+0x3b`.
- **Where they come from:** only `FUN_00396850`, the parser of the LBP server's user XML, writes them. Under `<clientsConnected>` it reads `<lbp1>` into `+0x3a` (field name at `0xe79fca`), `<lbp2>` into `+0x3b` (`0xe79fcf`), and `<lbp3ps3>`/`<lbp3ps4>` into `+0x3c`. The same parser reads `entitledSlots`, `lbp2EntitledSlots`, `lbp1UsedSlots` and so on.
- **So** "checking for any digital LittleBigPlanet games you own" is the LBP server reporting which LBP clients this PSN account has connected with. There are no `sceNpEntitlementAccess` imports, and the add-on list isn't involved.
- **`HasLBP2(world)`** with flags `2, 4, 8, 0x10, 0x20, 0x40` also needs a non-zero per-profile value: `FUN_004f9040(profile+0x1b0, key)` with keys `0xf42e56e2`, `0xf6070765`, `0xee17f3de`, `0x87f15c41`, `0xb2ad2759`, `0xc903e850`. Its meaning isn't decoded.

### What greys it out
- **[log]** The runtime reports the user as signed out: "[np] reporting sign-in state 1 of user 1" in `play_dlc_log.txt`.
- **[inf]** The page is gated by `IsOnline__` (`DAT_0130d8f0`, a connected-to-LBP-servers flag). Even when signed in, the server is needed, and it has been gone since 2024. With no server reply the user record stays empty, so both flags read false.
- The exact branch in the compiled script wasn't decoded, so this gating is an inference.

### What it unlocks [dec]
- **The import:** `ActivateImportScreen(flags)` takes, per flag bit (1 = LBP1, then the LBP2 groups), a fixed array of item GUIDs from the eboot (`DAT_00e11fb8`, `e11f94`, `e10a18`, `e1093c`, `e10904`, `e10970`, `e10948`; the first entries are e.g. `0x0f3234`, `0x0c6e25`, …). It queues them on the import screen (`FUN_008e1830(0x13a5710, …)`).
- **The result:** a whole group of items written into the profile, which is persistent. The import itself looks local; no server call was seen in it [inf].
- **Granularity:** all of LBP1 as one unit, and LBP2 as one unit per story world. These are LBP1/LBP2 base-game items, not DLC.

### Can it go through the add-on path? Partly, and that part is already done
- **The rename table [dec, data]:** `FUN_002463d0`, ticket 18's scan, maps accepted add-on labels through 53 pairs at `DAT_01073800` (strings at `0xe6c581`):
  - 48 LBP3 labels map to LBP1 PS3 DLC labels (`PLITTLEBIG0000xx`);
  - 5 map to LBP2 in-game-commerce labels (`IGCLBP2KAD…`).
  The game's DLC records are keyed by those PS3 names. When a label isn't in the table, the label itself is used, so **an add-on listed with a PS3 label matches its record directly.**
- **The user's PS3 pack** `BCUS98148\PLITTLEBIG000005` is LBP1 DLC: the rename target of `LBPDLCRARECO0002`, which the user doesn't own as an LBP3 pack. Ticket 18 lists it on service label 0, so by the decompiled flow it already unlocks that record in `feat/addcont` [dec; not verified in game]. It is not a licence for the LBP1 game.
- **LBP1/LBP2 DLC the user owns through LBP3 packs (33 of the 53 pairs):**
  - `2KADCK0001→IGCLBP2KADCK0001`
  - `COMPCO0001→000013`, `COMPCO0003→000022`, `COMPCO0004→000025`
  - `CPSFCK0001→000026`
  - `KMGSCK0001→000036`, `KMGSLK0001→000034`
  - `ORIGCK0001→000012`
  - `ORIGCO0001→000006`, `ORIGCO0002→000007`, `ORIGCO0007→000024`, `ORIGCO0008→000031`, `ORIGCO0009→000032`, `ORIGCO0010→000045`, `ORIGCO0011→000046`, `ORIGCO0012→000061`, `ORIGCO0013→000062`
  - `ORIGLK0001→000033`, `ORIGLK0002→000047`
  - `RARECO0001→000004`
  - `SONYCK0002→000021`, `SONYCK0003→000044`
  - `SONYCO0001→000001`, `SONYCO0002→000002`, `SONYCO0003→000003`, `SONYCO0005→000019`, `SONYCO0006→000020`, `SONYCO0007→000023`, `SONYCO0011→000055`, `SONYCO0012→000057`
  - `SONYMP0001→000043`, `SONYMP0002→000048`, `SONYMP0003→000056`

  The left side is `LBPDLC…`; the right side is `PLITTLEBIG…` unless written out. Plus `PLITTLEBIG000005` from the PS3 pack.
- **Not owned (20):** `2KADCO0001–0004`, `CPSFCO0001–0004`, `KMGSCO0002–0005`, `ORIGCO0003–0006`, `RARECO0002` (covered by the PS3 pack), `SONYCO0004`, `SONYCO0008` and `SONYST0001`.
- **Not covered by any add-on label:** the My Unlocks groups (LBP1/LBP2 base-game items). They are keyed to the server's `clientsConnected` flags, not to labels.

### Verdict
- **LBP1/LBP2 DLC:** per item and backed by the user's packs. It works through ticket 18's path already, with no My Unlocks needed. Confirm in game that `PLITTLEBIG000005`'s item appears.
- **My Unlocks itself:** it grants all-or-nothing per game (per world for LBP2), and its only evidence is the defunct LBP server's record that the account played LBP1/LBP2 online. The user's packs hold DLC licences, not LBP1/LBP2 game licences, so nothing they have backs it. Following the boundary, nothing that fakes sign-in or the server's flags is designed here.
- **If the user can show ownership of the LBP1/LBP2 games:** that means their PS3 LBP1/LBP2 game licence or disc dump (`PARAM.SFO` of `BCUS98148`/`BCUS98245` or a digital `NPUA…`), or their LBP1/LBP2 PS3 profile. A follow-up could then answer `HasLBP1`/`HasLBP2` from that evidence, per game, and allow the page while offline. Still needed before building that:
  - decoding the six per-world profile keys and the script's gate;
  - checking the import is fully local;
  - a profile-save backup first, because the import writes the items into the profile for good.
  Estimate: about 1 session of RE plus 0.5 to build and test.

## Comments
- **The work:** this lane's scratch files are in the job's temp folder: `farc_list`, decompressed scripts `ff_dec\`, `natives.txt` (3,913 native pairs), `rename.txt` and the probe outputs. No code commits; no game runs.
- **2026-11-12 night, built: the game's own import, run offline.** `feat/my-unlocks` `0c7200c`, from `port` `092d4e5`, pushed to the fork, not merged.
  - **Decision:** the human's goal is "lbp1 and lbp2 stuff on my char", and they said "we don't necessarily need to make my unlocks enabled".
    - The coordinator's reasoning: these items are LBP3's own content, shipped in the user's game data, and not paid DLC. The gate was a loyalty flag the LBP servers answered, and no player can satisfy it any more.
    - An opt-in, user-controlled offline answer restores a dead service's function, as LBP server-emulation projects do.
    - Paid LBP1/LBP2 DLC without packs stays locked: the rename-table entries the user doesn't own are untouched.
  - **What greys the page [measured]:** the Settings page greys "My Unlocks", like Online Play and Voice Chat, on `IsOnline__`. HasLBP1/HasLBP2 aren't even called while it's greyed.
    - The settings script calls `IsOnline__` several times a frame. Logged call order: `IsOnline`, `GetLocalPlayerHasOnlineUnlocked` ×2, `IsPlayGoInstallComplete`, `IsOnline` ×3, … `IsLBP3ClosedBeta`. Other scripts call it with the same `self`.
    - Answering it for that one section by call order was fragile, so it isn't done: the page stays greyed.
  - **The import is self-contained [decompiled]:**
    - `ActivateImportScreen__i` (eboot+`0x836fb0`) ignores `self`. It loads the import screen resource and queues the GUID lists per flag bit: 1 = `e11fb8` (LBP1); 2/4/8/0x10/0x20/0x40 = the LBP2 worlds.
    - `FUN_008e1830` resolves those lists, and a "Profile Import Thread" (`FUN_008e1b90`) loads each resource. The call sets `DAT_013a5708 = 1`.
    - The engine's front-end loop (`FUN_00517e50`) renders that screen while it's active. Its handler (`FUN_008bee30`) adds one loaded item per frame through the local profile's vtable `+0xe0`, then closes. Cross or Circle cancels.
  - **The runtime side** (`runtime/src/hle/my_unlocks.cpp`):
    - With `LBP3_MY_UNLOCKS=lbp1`, `lbp2` or both, it hooks `IsOnline__`.
    - The first call with a profile loaded (`*FUN_004cab10() != 0`), which is when the start menu opens, calls `ActivateImportScreen(flags)` once, on the game's main thread.
    - It logs `[unlocks]` lines; `LBP3_MY_UNLOCKS_TRACE=1` also logs the import-screen natives. Without the variable nothing is hooked.
  - **In game [measured]:** on test saves (m2_lib's `human_keep`/`use_baseline` on `savedata_story_20261111`), on the 165 Hz display.
    - **LBP1+LBP2 (`0x7f`):**
      - The game's own screen ("Importing… Importing collected objects from your LittleBigPlanet™ profile") ran about 2 minutes, then said "Objects from LittleBigPlanet have been successfully imported!" with OK.
      - The game saved the profile by itself: `littlefart202` at 328,380 B became `littlefart1468` at 413,456 B.
      - Customize Character before and after (`analysis\screenshots\unl3_before_outfits.png`, `unl3_after_outfits.png`): a new outfit on Costumes page 1, plus new materials, eyes and shoes.
      - Log: `analysis\unl3_import_stderr.txt`. The imported test profile is kept in `analysis\unl_saves\after_import`.
    - **Persistence:** relaunched without the switch (no `[unlocks]` lines, no hooks), the profile loads and the post-import items are there. In `unl3_relaunch_outfits.png` the Materials are identical to after the import.
    - **LBP2 only (`0x7e`), fresh test save:** the same screen ran about 50 s, with the same (generic) message, and the profile saved (371,440 B). Log: `analysis\unl4_lbp2_stderr.txt`.
    - **Run again on the already-imported profile:** the import runs again and adds nothing; the profile stays at about 371 KB and the game didn't re-save it. Harmless. Log: `analysis\unl5_rerun_stderr.txt`.
    - **Offline tests:** all 9 programs pass on the branch, `imetest` included (403/0 with no controller in use).
  - **For the user:** `D:\Recomps\lbp3-wt\unlocks\play_unlocks.bat`.
    - It uses the normal `play.bat` settings (1440p, frame generation, the audio fix, DLC) plus `UNLOCK=lbp1,lbp2`.
    - It refuses to start while a test marker is set or the game is running, and copies their saves to `analysis\savedata_backups\before_unlocks_<date>_<time>` first.
    - The user opens the start menu once, waits about 2 minutes without pressing Circle, then presses OK. Afterwards they play with the normal `play.bat`.
  - **State:** the human's saves are identical to `human_play_20261112_2054` (restored with `human_restore`). No marker, kept copy or game is left, and the clone is untouched on `port`.
  - **Not done:**
    - The My Unlocks page stays greyed, by design.
    - Nobody checked that each LBP2 world's list is complete or what it contains; the GUID lists weren't decoded item by item.
    - No merge into `port`; that's the human's call.
- **Unrelated, noted only:** the trans strings "Cross-Buy! … Cross-Buy costumes available to download … go to the PlayStation Store?" belong to a separate store check (`IsEntitlementBasedDLC__Q6SlotID`, `GetEntitledSlots__Q7StringAQ16Game.ELBPVersion`).

- 2026-11-12 night, **run on the human's own profile** with `play_unlocks.bat`, on the 240 Hz display. Their saves were backed up first to `analysis\savedata_backups\before_unlocks_20261112_2126`. The human checked the result: "it unlocked em". `feat/my-unlocks` (`0c7200c`) is not merged into `port`. Once imported, the items live in the profile, so plain `play.bat` keeps them.
- 2026-11-12 night, **merged into `port`** at the human's request ("go ahead and merge it for now, I'll think of a way to utilize it later on"): `port` `0c7200c`, plus a `play.bat` commit adding `MY_UNLOCKS=0` (1 sets `LBP3_MY_UNLOCKS=lbp1,lbp2`). The submodule is unchanged, and it's pushed. All 9 offline test programs pass on the merged clone.
- 2026-11-12 night, **a crash later in that session.** At about 21:43 an access violation hit eboot.bin+0x1bd1a5, a read of 0xffffffffffffffff on a background game thread. It came during a save and an audio-bank unload, one of the banks being the SpongeBob DLC's. The human says it happened when they pressed Options, i.e. the start menu, which is where the import hook sits. The saves were intact (snapshot `human_play_20261112_2143`). At the human's request ("don't mind it") the crash isn't being investigated. `play.bat`'s comment now says to close the game after the import and switch `MY_UNLOCKS` back off. The switch is off by default, so normal play doesn't install the hook. If it happens again without the switch, open a ticket: the log is kept as `analysis\play_logs\play_unlocks_20261112_2143.txt`.
- 2026-11-12 late night, from the verification run (`perf/verify_2cd464a.md`): no crash in about 45 Options presses over 4 sessions, with and without the switch. On two fresh story test saves the import showed "Failed to import objects", although an earlier test import on the same save, and the human's own, succeeded. Not yet explained; it matters only for using the switch on a new profile.
  - **Correction to that run's inference:** the failing `mount 'LBPxSAVEx12x350' mode 0x2` (read/write, no create) appears in EVERY session's log, normal play included (`play_logs\*`, `play_1440a`, `play_fg`, `play_dlc`). The game probes that slot, gets "not found", and then mounts `12x349`. It's routine, so it isn't the common thread between the failed imports and the crash.
- 2026-11-13, **why the import "failed" on the fresh test saves (o2, o3, o4): a back-button press, not a save problem** (decompiled + log evidence):
  - **What makes "Failed to import objects" (decompiled):**
    - The import screen's handler `FUN_008bee30` checks every connected controller each frame. If one holds the screen's cancel bit, it sets the screen's cancelled byte (`+1`) and closes.
    - The close `FUN_008bec30` is called only from there. When cancelled, it shows message `0xcc70ef5c`, "Failed to import objects". Otherwise it shows `0x7bfa76e7`, the "successfully imported" message, and marks the profile `0x9217a8d5`.
    - Nothing else fails the import. An already-imported profile, a missing save, or the `12x350` probe can't cause it. A missing item file only shows a "Missing File" notice from the import thread.
  - **The cancel bit is the back button.** The game's pad reader `FUN_00064f10` maps PS4 Circle 0x2000 to 0x20 and Cross 0x4000 to 0x40. The handler cancels on 0x20 when the region flag at `eboot+0x1060a20` is 1, and on 0x40 otherwise.
    - That flag is set in `FUN_000638d0` to 0 when the content region letter is J, K or C, and to 1 otherwise. So on this US dump, **Circle cancels**, and Cross is the OK for the message afterwards.
    - The earlier note "Cross or Circle cancels" was wrong; the user's "don't press Circle" was right.
  - **The logs fit.**
    - In o2, o3 and o4 the import thread lived about 4–5 status windows. About a second after "import started", a story save (`12x350` probe, then the `12x349` write) appears, which is what closing the start menu does. Then "Failed".
    - In `unl3`, `unl4` and `unl5`, where the lane only waited, it lived 17–37 windows, with no story save, and succeeded. `unl4` ran on the same fresh save as o3/o4 (`littlefart197`, `playfart_12_349_72`), so the save isn't the difference.
    - The verification lane's input log wasn't kept. That it pressed Circle to back out of the start menu is inferred from the story save and the code; it isn't directly recorded.
  - **The guard:** `fix/unlock-import` `34e3e8d` (from `port` `9625c5a`), pushed to the fork, not merged. While an import that `LBP3_MY_UNLOCKS` started is on screen (`eboot+0x13a5708` != 0), `scePadReadState` clears the cancel button (Circle here, or Cross if the region flag says so) from what the game sees. Every other button passes, and nothing changes once the screen closes or with the switch off. `LBP3_MY_UNLOCKS_GUARD=off` restores the game's way, and one `[unlocks] … held off` line is logged the first time it acts. `unlocktest` gives 21/0, and the other 10 offline programs pass.
  - **In-game check needed** (about 15 min, test saves): on the fresh story save, with `LBP3_MY_UNLOCKS=lbp1,lbp2`:
    1. Options, then Circle a few times during the import. Expect the "held off" line, the import to finish with "successfully imported", and the items to show in Customize Character.
    2. The same with `LBP3_MY_UNLOCKS_GUARD=off`. Expect "Failed to import objects", which confirms the cause.
    3. One normal session without the switch. Circle must back out of menus as usual.
- 2026-11-13 night, **the import guard merged into `port`** (`34e3e8d`, runtime only; fast-forward from `9625c5a`), pushed. Evidence before merging:
  - In game, guard on: Circle pressed three times during the import, the import ran its full 37 s, and the profile grew to the imported size.
  - Guard off (`LBP3_MY_UNLOCKS_GUARD=off`): "Failed to import objects" after about 1 s, which confirms the cause.
  - No switch: Circle closes the start menu as normal.
  - At runtime defaults with the fix: osk, `t13_coop` and backup all pass.
  - All 11 offline test programs pass on the merged clone.
  - The note in `play.bat` ("don't press Circle") stays as advice.
