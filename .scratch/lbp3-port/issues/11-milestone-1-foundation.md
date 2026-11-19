# Milestone 1, Foundation: build `port` and pass its five checks

Type: task
Status: resolved
Blocked by: none
Map: [LBP3 native port](../map.md) (done; this is the first build ticket from the [spec](../spec.md))

## Question

Build the integration branch `port` as the [spec](../spec.md#milestone-ladder) describes, and pass milestone 1's five checks on it.

## Done when

`port` builds, checks 1–4 (scripted) pass on it, and check 5 (the human's play session) passes. Pushing `port` to the fork waits for the human's OK.

## Comments

- 2026-11-10, **`port` built** (in the clone; nothing pushed):
  - **Submodule** `port` = `25b48b3a`: `4640726e` → fast-forward to `fix/gfx-submit-wait` (`40d6adc5`) → merge `fix/clear-swizzle` (`876508ce`). Same tree as the dry run's `e01ea74b`.
  - **Superproject** `port`, from `local/frame-pacing`: fast-forward to `fix/gfx-submit-wait` (`e85156b`), merges of `fix/epoll-precise`, `fix/guest-fpu` (`4d2eefe`), `fix/gamedir` and `fix/clear-swizzle` (`946a518`; the submodule pointer conflict resolved to the submodule's `port`), then `44d25af` (the DPI-aware screenshot tools). At that point the tree equals the dry run `try/all-fixes` except for the submodule pointer, whose tree is the same.
  - **Step 4:** `c85366d` points `.gitmodules` at `https://github.com/rexyness/shadPS4.git`, branch `port` (the clone's own `origin` remotes are unchanged; `git submodule sync` was not run). `00b47e5` makes the cutscene auto-skip opt-in (`LBP3_CUTSCENE_AUTOSKIP=on`). `18984f6` logs audio underruns every 1000th time instead of every 50th (gap 11).
  - **Build** at 20:06: clean. The binary has none of the diagnostic build's strings (no GPU checkpoints, no `LBP3_EPOLL_PRECISE` switch): every fix is unconditional.
- 2026-11-10, **checks 1–4 pass on `port` `18984f6`** (`tools/m1_checks.sh m1port`, 240 Hz display, vblank pacing; the human's saves came back identical to the snapshot):

  | Check | Result |
  |---|---|
  | 1. The real Introduction | 0.69% and 0.44% steps ≠ 1 over the level (runs 2 and 3); 30-frame averages 33.3–35.3 ms, 2–3 frames over 40 ms after the level opened (the known level-entry frames). Run 1 never reached the level: the offline network dialog came up late on the first boot of the new build, so the route's presses closed it and stayed in the Pod (a script timing flake, `pace_m1port_intro1_home.png`) |
  | 2. The Thruster save recipe | 3 of 3 OK, 20 save mounts each |
  | 3. The Legend of Bunkum | 16 of 16 voice lines, Needlepoint Peaks loads; the cutscene watchdog logged nothing (the auto-skip stayed off) |
  | 4. The Create sky | mean RGB (121, 177, 182), cyan (the diagnostic build without its switch gives (182, 177, 121), yellow) |

- 2026-11-10, **check 5 run by the agent by remote input, at the human's request** ("bro you do that"), on `port` `18984f6` at 240 Hz, starting from a copy of the human's newest save (`analysis\savedata_water_level`: their level with 3 Thrusters and a water power-up; the human's own saves were kept aside and put back after each part, matching the snapshot):
  - **How the water fires** (the human's hint): drop out of hover mode with d-pad down and walk onto the glowing pad under the nozzle. It is a power-up dispenser that gives Sackboy the **Creatinator**, which fires water with R1. In the newer save Home opens on Play, so the level is reached with Home → down (Create, "Recently Edited") → square (Edit Now).
  - **5A, present pacing** (`LBP3_PACING=present`, frames as fast as 4 ms per 30-frame average): 10 minutes with the Thrusters firing and water fired for 2 s every 20 s, then Save Changes. The save completed (save mounts 36 → 65, frames kept going: 32,580 → 33,390 over the 21 s after it) and Play mode started. No rejected samplers, no stuck waits, 7 frames over 40 ms in play (`analysis\m1c5a_stderr.txt`, `screenshots\m1c5a_contact.png`).
  - **5B, vblank pacing, booted without `--skip-intro`:** the logos and the intro movie played with subtitles, the movie's sound port (4) had no underruns, and the game handed over to the Pod. Then Play → Adventure → Prologue → Introduction, walked right with jumps by script. About 60 s in, Newton's scene began (his control panel, "Just a simple press of this button and our adventure will begin!"). It went through the frame of lamps where upstream's game once froze for good, then the abduction (space, the beam, the white flash), and arrival in Bunkum, into the Legend of Bunkum with Newton among his lamps. No rejected samplers, no stuck waits; from Newton on, 31 frames over 40 ms at the scene changes, the worst 75 ms (`analysis\m1c5b_stderr.txt`, `screenshots\m1c5b_w_contact.png`).
  - **5C, vblank pacing, the same water level** (`m1c5c`): about 3 minutes of Thrusters and water by script, which was then stopped so the human could play. While it ran, **the human placed six missiles**, all with their engines lit, and played with them ("missiles seems to be working fine too"), then saved: the profile save `bigfart9` (7,808 bytes, up from 6,644). To about frame 12,090: no rejected samplers, no stuck waits, no device loss, 13 frames over 40 ms. The worst, 785 ms, came right after the save, as the game left Create for Play and closed its sound banks (the gap 10 kind of stall, not a freeze). That session ran on a copy of the human's save that was identical to it, so the human kept the result as their save; the previous one is in `analysis\savedata_backups\before_missiles_2058`.
  - Not covered: missiles at present pacing (5A had the Thrusters, water and the save at present pacing; the missiles were at vblank pacing).

## Answer

**Milestone 1 is done** (2026-11-10). `port` (`18984f6`, submodule `port` `25b48b3a`) builds from the six fixes, the screenshot tools, the submodule URL, the auto-skip default and the log rate, and passes all five checks on the 240 Hz display: the Introduction under 1% steps ≠ 1 (0.69%, 0.44%), the Thruster save recipe 3 of 3, the Legend of Bunkum played through with the auto-skip off, the cyan Create sky, and check 5: Thrusters, water and a save at both pacings, the intro movie, Newton's abduction cutscene and active missiles, with no reset, deadlock or stuck wait anywhere. Pushed at the human's OK ("yeah push it", 2026-11-10): `rexyness/shadPS4` `port` = `25b48b3a`, then `rexyness/LittleBigPlanet-3-Recompiled` `port` = `18984f6`, which pins that commit and points `.gitmodules` at it, so a recursive clone of `port` can fetch the submodule. The new commits are under the human's GitHub no-reply address, and their changes contain no personal details. Next on the ladder: milestone 2, Create mode works like a PS4 (gaps 12, 13, 14, 17).

