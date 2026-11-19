# DLC: the user's own add-on packs

Type: build
Status: done on branch `feat/addcont` (`092d4e5`, from `port` `6d212bc`, pushed to the fork); not merged into `port` yet. User launcher: `D:\Recomps\lbp3-wt\dlc\play_dlc.bat`.
Map: [LBP3 native port](../map.md). The human asked "can we do dlc unlock here?" (2026-11-12). We support DLC the human owns: they said "I do have the dlc but they're in pkg format" (`D:\Recomps\ROMs\PS4\DLC`, 256 packages). Unlocking DLC that isn't owned is out of scope.

## Question

How does LBP3 learn which add-ons are installed, and what does the port have to answer so the human's own packs unlock their items?

## Answer

**The packs** were extracted 2026-11-12 with the human's `pkg_extractor.exe` (256 of 256 OK) into `D:\Recomps\ROMs\PS4\CUSA00473-DLC\`, which is treated as read-only like the dump. 255 sit under `CUSA00473\<label>\` and 1 under `BCUS98148\PLITTLEBIG000005`, a PS3 LBP1 content ID. All of them are license-only: nothing but `sce_sys\` (param.sfo with `CATEGORY ac`, plus license.dat, license.info, entry_keys and so on). The whole folder is 16 MB.

**What the game does** (decompiled, eboot):
- `FUN_002463d0` runs at boot and at profile load (`FUN_0049c480`), and again on the system-service event `0x10000003` (ENTITLEMENT_UPDATE, handler `FUN_0006fb50`). It enumerates service labels 0..2, chosen from a platform table entry `DAT_012c8718` that defaults to index 0x40, or 1..3 for other entries.
- For each service label it counts with `sceAppContentGetAddcontInfoList(label, 0, 0, &n)`, then lists with 0x18-byte entries.
- For each entry it calls `sceAppContentGetEntitlementKey`. It accepts the pack only if that call returns 0 AND the entry's status is 0 (NO_EXTRA_DATA, i.e. license-only). It never reads the key's bytes.
- Accepted labels of 16 characters go through a 53-entry rename table (`DAT_01073800`, compared from character 6). They are then matched against the game's own DLC records (stride 0xa0), and `FUN_002461e0` sets the owned flags of each record's items.
- Not imported: `AddcontMount`, `GetAddcontInfo` and `sceNpEntitlementAccess*`. The DLC labels exist only in the eboot (strings such as `LBPDLCORIGCO0001`), not in the game data.

**The difference from shadPS4:** shadPS4 marks folder packs INSTALLED (4) and only marks RIF license-only entitlements NO_EXTRA_DATA. LBP3 would skip every pack as shadPS4 lists folder packs.

**The change** (`runtime/src/hle/app_content.{h,cpp}`, `main.cpp`):
- At start-up the runtime scans `<game dir>\CUSA00473-DLC\<title id>\<label>\sce_sys\param.sfo` (`LBP3_ADDCONT_DIR` picks another root).
- The label is `CONTENT_ID[20..36]`. Packs holding only `sce_sys` get status 0; packs with data get 4.
- All packs are listed under service label 0; the game treats every service label alike. Keys are 16 zero bytes, as in shadPS4.
- `[addcont]` lines log the scan, each list call, and key calls that miss.
- `LBP3_ADDCONT=off` lists none, the old behaviour.
- New `addconttest`, 55 checks: param.sfo parsing, the scan, and the calls the way LBP3 makes them.

## Checks (2026-11-12 evening, test save `analysis/savedata_story_20261111`, 165 Hz display)

- **The game's calls:** it lists service labels 0, 1, 2 twice per boot, at start-up and at profile load: 256 packs on label 0, 512 key calls, 0 misses.
- **Customize Character in the Pod popit**, the same save with the DLC on and off. Stills: `analysis\screenshots\dlc_on5_sheet.png` and `dlc_off5_sheet.png`.
  - **Costumes:** with the DLC on, the outfit pages are full: Oddworld's Abe, Star Trek, a Toy Story alien and many more. With it off there are 2 outfits.
  - **Materials:** 5 pages on, against about 24 swatches off.
  - **Head/Eyes:** full grids on, against a few items off.
- **Nothing else changes:**
  - the Needlepoint route on the DLC build loads and walks as before: walk work 19.85 ms, 0 frames over 40 ms, 0 GPU warnings;
  - all 9 offline test programs pass;
  - the human's saves were untouched (identical to `human_play_20261112_1959` before and after every run).

## Open

- **The PS3 pack** (`PLITTLEBIG000005`) is listed under label 0 with the rest. Whether the game's records or rename table know it isn't checked.
- **Whether the unlocks persist in the profile** when the game later runs without the packs isn't checked. The flags are set in memory at each boot. The human's live saves have not been run with DLC yet.
- **Merging into `port`:** after the human has seen their costumes (`play_dlc.bat`).

- 2026-11-12 night, **merged into `port`** at the human's request ("merge it into port"), after they saw their costumes: `port` `092d4e5` (fast-forward; the submodule is unchanged at `56120048`), pushed to the fork. DLC support is on by default whenever `<game dir>-DLC` exists; `LBP3_ADDCONT=off` turns it off. Built: 8 of 9 offline test programs pass, and `addconttest` gives 55/0. `imetest`'s pad check (`--pad circle`: is the closing tap still held, line 723) failed in some runs and passed in others while the human was playing with their DualSense. It reads controller 0, which was their live pad. The merge touched no input or IME code, so it's environmental: rerun it when the controller is idle.
- 2026-11-12 night: after the human closed the game, `imetest` passed 3 runs in a row (403/0) on the merged `port` `092d4e5`. The earlier failures came from their live controller.
