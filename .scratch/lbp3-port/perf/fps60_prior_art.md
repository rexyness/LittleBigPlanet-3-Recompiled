# LittleBigPlanet at 60 fps: prior art and what it says about the timestep

Web research only. No local builds and no game were used. Accessed 2026-11-12.
Target: LittleBigPlanet 3 PS4, version 01.28. Region IDs: CUSA00473 (US), CUSA00063 (EU), CUSA00762 (GB), CUSA00693 (Asia), CUSA00810 (US LATAM).

## How to read the evidence

Each quote carries a tier, and each finding a confidence label.

| Tier | Meaning |
|---|---|
| **A** | Exact text, pulled raw with `gh api` or curl. GitHub sources are pinned to a commit. |
| **B** | Extracted by WebFetch, so a model sits in between. Wording is very likely exact but not guaranteed. |
| **C** | Search-engine summary or snippet only. The page itself could not be fetched. |

Confidence is **high**, **medium** or **low**.

## Answer in brief

1. **No 60 fps patch for LBP3 on PS4 was found, in any region, in the places checked.**
   - Checked and absent: GoldHEN's patch repository and its successor, illusion's PS-Game-Patch. Also GoldHEN's cheat repository, the CheatRunner patches and illusion's other site and patch repositories.
   - Not covered: the Orbis Unjailed list could not be read, and scene forums and Discord servers were not searched.
   - The only LBP3 PS4 patch found anywhere is shadPS4's "Disable DOF" for 01.28, one byte at 0x1462928.
2. **RPCS3 has no 60 fps patch for LBP 1, 2, 3, Karting or Hub.**
   - Its patch file holds only MLAA, motion blur, score-bubble, Create Mode and aspect-ratio patches for these games.
   - The RPCS3 wiki's VBlank list marks LBP 1, 2 and 3 as "VBlank affects gamespeed and framerate" (tier C).
3. **The real 60 fps attempts found are all on sibling titles, and none is a simple patch:**
   - **LBP1 on PS3** (qwrwed, Oct 2026): unlocking the flip makes the game run about 2.1x fast. There is no single timestep to halve, and many per-tick values in code and data had to be retuned. The work is partial, and the author also documents frame generation as the practical route.
   - **LBP PSP**: a 2015 PPSSPP cheat swaps a 1/30 float for 1/60 and removes one wait. It is marked BETA and has known breakage. Separately, a static-recompiler project (Nakagawa-Recomp, Nov 2026) reached 60 Hz logic at stock speed, but only by patching 68 code sites. Its package is private.
4. **The one LBP3 PS4 native-PC project (LBPPC, CUSA00473 1.28)** ships "60 FPS" through DLSS or FSR frame generation over a 30 Hz game. It calls native 60 "the hardest one on the list".
5. **On PS4, Sony set a 30 fps / 1080p target in July 2014**, after a producer had said 60. No Digital Foundry analysis turned up. No PS4 Pro patch or PS5 boost to 60 was found (tier C or inference).

**Timestep verdict, from PS3 and PSP evidence:**
- The engine is **fixed-step and frame-locked**: one simulation tick per presented frame at 30 Hz, with no delta-time compensation and no render interpolation.
- Game speed scales with frame rate: about 2x when uncapped, slow motion when the frame rate drops.
- The per-tick assumption lives in four places, not one constant:
  - code constants;
  - plain-text data files (character speeds and accelerations, frame counts);
  - animation clips, which advance one clip frame per tick whatever their `fps` field says;
  - **saved level data**: an integer world `frame` counter, plus emitter `frequency`, `phase` and `lifetime` as integers (presumably frame counts). Emitter `lastUpdateFrame` is set from the world `frame`.
- **The PS4 build itself has not been verified.** Every mechanistic finding comes from PS3 LBP1, PSP LBP or the shared file format.

---

## Checklist: the places the task named

| # | Place | Result |
|---|---|---|
| 1 | GoldHEN/GoldHEN_Patch_Repository | **Checked, absent** (F1) |
| 2a | illusion0001 repositories (now `illusionyy`) | **Checked, absent** (F2) |
| 2b | Orbis Unjailed fps-patches-mods page | **Could not read**: live 404, archive unreachable (F6) |
| 3a | RPCS3 patch file (`patch.yml`, served by rpcs3.net) | **Checked**: LBP entries exist, none for frame rate (F7) |
| 3b | RPCS3 wiki game pages, VBlank list, forums | **Could not read** (HTTP 403, Cloudflare). VBlank-list rows known only from search summaries (F8, F10) |
| 3c | RPCS3 GitHub issues and GitHub wiki | **Checked** (F9). The GitHub wiki has nothing on LBP |
| 4 | Vita3K and VitaGrafix, for LBP PS Vita | **Checked**: no frame-rate notes or patches (F16) |
| 5 | Community knowledge of the 30 Hz logic rate | **Found**, mostly tier B and C (F17), plus file-format evidence (F13) |
| 6 | Digital Foundry and other LBP3 PS4 frame-rate analyses, PS4 Pro and PS5 | **No Digital Foundry piece found.** The official 30 fps target is confirmed (F18-F20) |

Extra prior art turned up on the way: qwrwed's LBP1 60 fps work (F12), Nakagawa-Recomp's LBP PSP 60 Hz package (F15), LBPPC (F21) and GoldHEN's `fliprate_remover` plugin (F5).

---

## PS4 patch repositories

### F1. GoldHEN patch repository: nothing for LittleBigPlanet. Confidence: high

- **Source:** https://github.com/GoldHEN/GoldHEN_Patch_Repository at commit `667ad05942b3` (2024-09-06).
- **What was checked:**
  - Listed the full tree: 283 entries, not truncated.
  - Downloaded the tarball and grepped every file under `patches/xml`, `patches_old/json` and `patches_old/yml` for `littlebig`, `little big` and all five LBP3 CUSA IDs. No match.
  - GitHub search of the repository's commits, issues and pull requests for "LittleBigPlanet" and "LBP": nothing.
- **The repository says it is outdated** (tier A, README):
  > **This repository is now outdated and has been moved to [illusion0001/PS4-PS5-Game-Patch](https://github.com/illusion0001/PS4-PS5-Game-Patch)**

### F2. illusion's repositories (illusion0001, now `illusionyy`): nothing for LittleBigPlanet. Confidence: high

- **The account was renamed.** `github.com/illusion0001` now returns 404. `repos/illusion0001/PS4-PS5-Game-Patch` redirects to **https://github.com/illusionyy/PS-Game-Patch**, checked at commit `65914503123d` (2026-10-23).
- **PS-Game-Patch:** the tarball holds 169 XML patch files, and a grep for LBP names and the five CUSA IDs found nothing. GitHub search of its code, commits and issues for "LittleBigPlanet" and "LBP": nothing.
- **Other illusionyy repositories** (`illusionyy.github.io`, `legacy-website`, `libhijacker-game-patch`): tarballs grepped, no match.

### F3. shadPS4 `ps4_cheats`: the only LBP3 PS4 patch found, and it is not about frame rate. Confidence: high

- **Source:** https://github.com/shadps4-emu/ps4_cheats/blob/main/PATCHES/LittleBigPlanet3.xml, added in commit `346984fc` (2026-09-25, "Little Big Planet 3 - Disable DOF - 01.28 (#153)"). Repository HEAD `cf33d5bae8bd`.
- **The patch** (tier A):
  ```xml
  <ID>CUSA00693</ID> <ID>CUSA00762</ID> <ID>CUSA00473</ID> <ID>CUSA00063</ID> <ID>CUSA00810</ID>
  <Metadata Title="Little Big Planet 3" Name="Disable DOF" Author="DanielSvoboda" PatchVer="1.0"
            AppVer="01.28" AppElf="eboot.bin" isEnabled="false">
      <Line Type="bytes" Address="0x1462928" Value="00"/>
  ```
- **What it changes:** one byte in the 01.28 eboot (a flag, presumably) that turns off depth of field. It touches neither frame rate nor timestep. This is the only file in that repository that matches LBP.

### F4. Other PS4 patch and cheat collections: nothing for LBP3. Confidence: high

- **GoldHEN/GoldHEN_Cheat_Repository** at `acba322df339`: no file for any of the five CUSA IDs among its 5,473 tree entries.
- **notmaj0r/CheatRunner_Patches** at `b4a99ec5da55`: no LBP file.
- **GitHub-wide code search** for each LBP3 CUSA ID: hits only in title databases, save tools, server projects and shadPS4 compatibility lists. No frame-rate patch anywhere.

### F5. GoldHEN `fliprate_remover` plugin: a generic tool, never reported on LBP3. Confidence: high for the tool's content

- **Source:** https://github.com/GoldHEN/GoldHEN_Plugins_Repository/blob/main/plugin_src/fliprate_remover/source/main.c, at `998fe03fe027`.
- **Quote** (tier A):
  > // Fliprate Remover: Universally removes framerate limit for games using
  > // system library `sceVideoOutSetFlipRate`
  > // may not be used with games that have fixed timestep.
  - The hook simply returns 0 from `sceVideoOutSetFlipRate`.
- **Relevance:** it is the obvious first experiment on a PS4, and it only helps if LBP3 caps its frame rate through `sceVideoOutSetFlipRate`. No report of anyone running it with LBP3 was found. If LBP3 behaves like the PS3 game, the expected result is the same 60 fps at about 2x game speed.

### F6. Orbis Unjailed "fps-patches-mods": could not read. Confidence for "no LBP entry": medium (inferred)

- **Source:** https://florinsdistortedvision.github.io/orbisunjailed/fps-patches-mods. Every variant returns **HTTP 404 today**.
- **The archive could not be reached either.** The Wayback API reports a snapshot (`web.archive.org/web/20240825163114/...`), but both web.archive.org and archive.ph are unreachable from this environment.
- **The source repository is gone.** The `florinsdistortedvision` GitHub account has no orbisunjailed repository left.
- **Search evidence** (tier C): the page told readers to load "GoldHEN 2.3 or newer" for its "60FPS & Resolution patches". A domain-restricted search for LittleBigPlanet on the site returned only unrelated jailbreak guides.
- **Inference:** the page was most likely a front end to the GoldHEN and illusion patch sets, and neither ever had an LBP entry (F1, F2). This could not be confirmed.

---

## PS3 (RPCS3)

### F7. RPCS3 patch file: LBP entries exist, but none for frame rate. Confidence: high

- **Source:** the patch API RPCS3 itself downloads from, https://rpcs3.net/compatibility?patch&api=v1&v=1.2. Format version 1.2, sha256 `1d138b56165fd6f4a5c2faf9cb28ef40d542caa9c293eef0d18e75c9fcecd72b`, 73,149 lines. This is the same content as the wiki's Help:Game Patches pages.

**What is in it for each game** (tier A):

| Game | Version (PPU hash) | Patches |
|---|---|---|
| **LittleBigPlanet 1**, all regions | 01.30 (`PPU-545c1abb…`) | "Create Mode"; "Disable Score Bubble FX"; "Aspect Ratio"; "Disable Blur". The blur patch's note reads "Disables blur effects (Motion blur, DOF and such)" |
| **LittleBigPlanet 2** | 01.33 (`PPU-a74423ca…`) | "Disable MLAA"; "Aspect Ratio"; "Create Mode Everywhere"; "Disable Score Bubble FX"; "Disable Motion Blur" |
| LittleBigPlanet 2 | 01.21 | "Create Mode Everywhere" |
| LittleBigPlanet 2 Move Pack Beta | — | "Local Custom Server" |
| **LittleBigPlanet 3 (PS3)**: BCES01663, BCES02068, BCUS81138, BCUS98362, BCAS20322, NPEA00515, NPUA81116, NPHA80277 | 01.26 (`PPU-4ff9503c…`) | "Disable MLAA" (illusiony); "Create Mode" (Zhaxxy); "Disable Score Bubble FX" (ennuo); "Disable Motion Blur" (Hasster, illusiony) |
| LittleBigPlanet Hub Internal Beta | — | "Aspect Ratio" |
| SPU section | — | "Disable SPU MLAA - LittleBigPlanet 2, LittleBigPlanet 3, LittleBigPlanet Hub" |

- **Nothing for LittleBigPlanet Karting.**
- **No LBP entry is named "Unlock FPS", "60 FPS" or anything like it.**

### F8. RPCS3 wiki "Vblank compatible games list": VBlank changes LBP's game speed. Confidence: medium (tier C only)

- **Source:** https://wiki.rpcs3.net/index.php?title=Vblank_compatible_games_list. Fetching it returns HTTP 403 (Cloudflare). The rows below come from search-engine summaries.
- **Columns, as summarized:** game, target FPS, VBlank works, clocks scale, FPS patch required, notes.
- **Rows, as summarized** (tier C):
  > LittleBigPlanet | 30 | No | Default | N/A | VBlank affects gamespeed and framerate, clocks scale didn't affect anything.
  > LittleBigPlanet 2 | 30 | No | Default | N/A | VBlank affects gamespeed and framerate, clocks scale didn't affect anything.
  > LittleBigPlanet 3 | 30 | No | Default | N/A | VBlank affects gamespeed and framerate, clocks scale didn't affect anything.
  > LittleBigPlanet Karting | 30 | Yes | Default | N/A |
- **What this means:** raising RPCS3's VBlank rate raises LBP 1, 2 and 3's frame rate and their game speed together. Changing the emulated clock (time scale) does nothing, so the game does not measure elapsed time. It counts VBlanks or flips.
- **Karting is the exception,** listed as working. Its engine lineage is disputed, though: reports call it ModNation Racers' engine, partly LBP2's, or a new engine. So its row says little about LBP3.

### F9. RPCS3 issue #8840: "physics are tied to frame-rate". Confidence: high that it was said; medium as engine fact

- **Source:** https://github.com/RPCS3/rpcs3/issues/8840, "[FIXED] LittleBigPlanet low fps when playing levels" (2020-09-05).
- **Comment by Asinin3,** 2020-09-05. GitHub marks the commenter `author_association: CONTRIBUTOR` on RPCS3 (tier A):
  > Not a bug, game's physics are tied to frame-rate so when you have low fps the game is in slow-motion. Similarly, if you go over 30fps then the game runs too fast. You won't go over 30 unless you uncap frame-rate though.
- **The reporter's own description** (tier A):
  > When I go into a level, it goes into slow motion, with fps below 24. The game runs well but when moving it's just slow.

### F10. RPCS3 forum, LittleBigPlanet [BCES00141] thread. Confidence: low (tier C)

- **Source:** https://forums.rpcs3.net/archive/index.php/thread-174607-2.html. HTTP 403.
- **Search summary:** a user reports that raising VBlank "just seems to speed up the game when past the 60 vblank setting". Another asks "Is it not possible to run this game faster than 30fps?"

### F11. RPCS3 wiki pages for LittleBigPlanet 1, 2, 3 and Karting: could not read

- HTTP 403 (Cloudflare challenge). An attempt through the MediaWiki API was refused by the session's permission policy, and no workaround was tried.
- These pages' notes on frame rate and game speed remain unverified.
- **A partial mirror exists:** the wiki's recommended per-game configs (from `api.rpcs3.net/config`), mirrored at https://github.com/PSH5JB/PSH5/tree/main/payloads/emulators/RPS5S3/files/game_configs_wiki.
  - Read for BCES00141, BCUS98245 and BCUS98362: they contain only `Write Color Buffers`, `Read Color Buffers` and `MSAA`. They have no VBlank or frame-limit line.
  - The mirror keeps only GPU-agnostic settings, so this proves little.

### F12. qwrwed/lbp-rpcs3-60fps: LBP1 at 60 fps in RPCS3, the most detailed engine evidence. Confidence: high that it is reported; medium as engine fact (single author, about two weeks old, not replicated)

- **Source:** https://github.com/qwrwed/lbp-rpcs3-60fps, at commit `720d6952bfc0` (created 2026-10-27). File: `LittleBigPlanet 1 60fps - findings to share.md`. Tested on BCES00611 1.30, PPU hash `PPU-545c1abbf1c562d60fca7435401f020beab76b53`.
- **The repository also holds:** a data-mod tool (`mod/lbpmod.py`, `mod/anim_resample.py`, `mod/edits_60fps.json`) and measurement scripts (`tools/`).
- **It references "Papy's BCES00141 1.30 notes".** Those could not be found online.

**Key lines** (tier A, verbatim):

- **The 30 fps cap and the uncap:**
  > The game's VBlank handler (0x37a498) signals a flip every 2 VBlanks. Patching 0x37a4c4 and 0x37a4e4 to `li r0,1` (0x38000001) and 0x37a4bc to a branch (0x48000014) lets it run at 60.
  > At 60 the whole game runs about 2.1x fast (measured: a 0.3 s stick hold moves Sackboy 93 px at 30fps, 193 px at 60fps).
- **No single timestep:**
  > ## Why there is no single timestep to halve
  > - No SPU program (5,437 functions in RPCS3's SPU cache) loads 1/30, 1/60, 30 or 60 as a constant, and the EBOOT holds no double 1/30 or 1/60.
  > - Halving the PPU's 1/30 float constants does not change game speed (Papy's tests).
  > - Movement is per tick. Sackboy's walk speed grows by about 3 units per tick (next ≈ 0.995 × speed + 3.003) and caps at 30 per tick.
- **Per-tick values in data files:**
  > Sackboy's tuning is a plain-text character settings file, `gamedata/creatures/yellowhead.cha` ... (`WalkSpeedMaxDesired 30`, `WalkSpeedMaxAcceleration 3`, `AirSpeedMaxDesired 22.5`, `AirSpeedMaxAcceleration 2.4`, jetpack values) and frame counts (`FramesTillDrown 900`, `FramesFromFireDeathTillRespawn 35` and others).
  > World gravity is the `PEffector` gravity vector, default (0, -2.7, 0), stored with each level.
- **Animation advances per tick:**
  > Sackboy's animations advance one frame per tick. The walk clips are 13 frames: 0.43 s per cycle at 30 ticks/s, 0.22 s at 60.
- **Level mechanics run on the world tick counter:**
  > Level mechanics run per tick too. In Get a Grip the King at the start bobs every 1.98 s in the 30fps original and every 1.00 s with the 60fps setup
  > Joint time is the world frame counter: the function at 0x1bf24 returns `(float)*(world+0x1d0)`, which points to `world+0x1b8`, advancing once per tick. ... 52 places read the counter.
  > With it at 60fps the King bobs every 2.01 s (30fps original: 1.98 s), but in 30 Hz steps: his image changes on alternate frames only.
- **Speed still follows the frame rate after retuning:**
  > The game's speed follows its frame rate once these per-tick values are halved, so the setup only plays at normal speed when RPCS3 holds 60 FPS.
- **Unresolved:**
  > Hypothesis for the remaining gap: the slide after the stick is released decays per tick inside the physics engine, so it is cut short at 60fps. The EBOOT holds no settings table for engine damping or friction
- **The fallback the author documents:**
  > Frame generation route: ... lsfg-vk 2.0.0 with multiplier 2 doubles the output of the original 30fps game (patches off, data unedited). RPCS3 holds 30 FPS, so game speed is the original's.

**What the author had to change:**
- the flip divider;
- gravity, through the material `gravityMultiplier` set to 0.25;
- walk speed, through `.cha` edits;
- the jump modifier and jump decay constant (four PPU patch lines);
- the animation-clock reader;
- every Sackboy animation clip, frame-doubled (213 files);
- the world-counter increment, gated to alternate ticks.

The result was still not equivalent:
- Walk distance per hold was about 62 px, against 85 px at 30 fps.
- Prize-bubble icons pulsed about 1.78x fast. With all four collectible clips frame-doubled they ran about 1.3x *slow* instead (overshoot).
- Grass and glow kept their fast motion.
- The `.anim` `fps` field turned out to be ignored. Setting it to 15 "changes nothing". The tick dependence is in the clip's frame count, which is why the clips had to be resampled.

This is a partial result on a different game (LBP1, PS3), but the same Media Molecule engine family as LBP3.

### F13. Craftworld Toolkit: the level and world format stores integer tick counters. Confidence: high for field types; medium that they are 30 Hz ticks

- **Source:** https://github.com/ennuo/toolkit, at `d6a2ef80b5a0`. Files: `lib/cwlib/src/main/java/cwlib/structs/things/parts/PWorld.java` and `.../PEmitter.java`.
- **PWorld** (tier A):
  ```java
  public int frame;
  @GsonRevision(min = 0x2e2)
  public int simFrame;
  @GsonRevision(min = 0x377)
  public int frameLevelStarted;
  ...
  public float maxVel = 100.0f, maxAVel = 1.0f;
  ```
  - `Revisions.java`: `WORLD_SIM_FRAME = 0x2e2; // added simFrame to world`.
- **PEmitter** (tier A):
  ```java
  public int frequency, phase, lifetime;
  public float speedScaleStartFrame, speedScaleDeltaFrames;
  public float lastUpdateFrame;
  ```
- **The emitter's clock is tied to the world frame counter** (tier A, `PWorld.java` lines 275–276, run for revisions above 0x2c3):
  ```java
  if (emitter.lastUpdateFrame == 0)
      emitter.lastUpdateFrame = frame;
  ```
  - For revisions 0x230–0x2c3 the serializer derives `lastUpdateFrame = speedScaleStartFrame + speedScaleDeltaFrames`.
  - The serializer has no seconds-to-frames conversion (no `30` multiplier). `frequency`, `phase` and `lifetime` are integers, presumably frame counts, given that the emitter's clock is the world frame counter.
- **What this shows:** saved levels, including user-generated content, carry integer frame counters, and emitter timing runs on the world frame counter.
  - qwrwed (F12) found that joint `animationTime` is also "in frames (default 60)".
  - A separate `simFrame` exists from revision 0x2e2, the LBP2 era. Its existence does **not** show a decoupled simulation rate. It may count frames while not paused, for example.
- **Consequence for any native 60 Hz plan:** level data authored for 30 ticks per second would need converting or gating, not just engine constants.

---

## PSP and Vita siblings

### F14. LBP PSP: a 60 fps cheat on PPSSPP. Confidence: high that it exists; medium for its behaviour

- **Lineage caveat:** LBP PSP was made mainly by SCE Cambridge Studio. Push Square calls it "a direct port", but its codebase relationship to the PS3 engine is not established. Treat it as an analogous engine, not the same code.
- **Source:** https://forums.ppsspp.org/showthread.php?pid=105911, post #457 by Kabuto_Kun, 2015-04-18. Listed at https://kabutokun.github.io/ as "LittleBigPlanet - BETA?".
- **The code** (tier B):
  > `_S UCES-01264 _G LittleBigPlanet [EUR] _C0 60 FPS`
  > `_L 0x202209BC 0x00000000 _L 0x203ACED8 0x3C888889 _C0 30 FPS [Default]`
  > `_L 0x202209BC 0x0E2E9A69 _L 0x203ACED8 0x3D088889`
  - The same pattern exists for "LittleBigPlanet Demo [USA]" (0x2020EA40 / 0x20364000).
- **Decoded here:**
  - 0x3D088889 = 0.0333333 (1/30); 0x3C888889 = 0.0166667 (1/60).
  - 0x0E2E9A69 is `jal 0x08BA69A4`, NOPed in the 60 fps state, presumably the second VBlank wait.
  - So: drop one wait and halve one dt float.
- **Reported problems:** https://forums.ppsspp.org/showthread.php?pid=112149, post #547 by Name456, 2015-11-02 (tier B):
  > causes loading to not work (have to change it back to 30fps)
  > causes the narrator's speech to be cut, sped up, and basically make him say random things.
  - Kabuto_Kun then noted on the original post that "it has some problems".

### F15. LBP PSP: Nakagawa-Recomp's experimental 60 Hz logic package. Confidence: high that it is reported; medium as engine fact (single author, package private, about a week old)

- **Source:** https://github.com/Jstar269/Nakagawa-Recomp/issues/1931, "LittleBigPlanet (UCUS98744): native 60 Hz game logic package (experimental)", opened 2026-11-03, still open. Repository HEAD `a395826e6bad`.
- **How it works:** a static recompiler for PSP games. The LBP package itself is private; only the generic tooling is public, in `docs/ENHANCEMENT_CONTRACT.md` §9.
- **The model behind it** (tier A, contract §9.1):
  > Many PSP titles run a 30 Hz game-logic loop by waiting two VBLANK intervals per update and tune frame-counted durations, steps, decays and per-update probabilities for $\Delta t = 1/30\text{ s}$.
- **Measured result, 2026-11-04** (tier A):
  > | Logic rate over 500 s, through the first story level | median 30, max 30 updates/s | median 60, max 60 updates/s |
  > | Timed level element (physics clock over game time) | 1.000000 | 1.000000 over 378 s |
  > | Jump, same input (7 jumps) | apex 0.302, airtime 1.000 s | apex 0.317, airtime 1.017 s |
- **Intro movies ran at double speed until gated:**
  > An earlier build of the private 60 Hz package played the intro movie sequence at twice its speed ... The movie loop steps the movie once per logic update, so it stepped twice as often at 60 Hz.
- **Status on 2026-11-11** (tier A):
  > the private package v0.5.0-pre9 (68 sites and 3 data sites; the census added 21)
  > | Game clock per update (timed element) | 1/30 s | 1/60 s (0.016669 over 1,398 updates) |
  > Particles: the emitter gadget's frame index and its seconds-to-frames conversions (8 sites, `count_x2`, all or none). The other particle systems take the frame time.
  > Animation: per-update blends in the jetpack state (7 sites, `decay_k_to_1-sqrt(1-k)`). ... Clips, crossfades, blinks and expressions take the frame time.
  > UI: stick pan and zoom, the popit cursor, a speech-length cap (`count_x2`). Menus and fades run on the clock.
  > Audio takes the real frame time, so it does not depend on the logic rate.
- **What it shows:**
  - PSP LBP is also a fixed 1/30 s logic step.
  - Some subsystems already read real frame time: audio, some animation, menus and fades. The physics and gameplay core, and the level gadgets, count ticks.
  - Doubling the rate exactly took a census of per-tick constants: counts, decays, retentions, accelerations and launch velocities.
  - The jump-arc error from halving the step is reported as closed in pre9. That claim rests on **one jump**, in a run where host load held the logic at about 30 updates a second.

### F16. LBP PS Vita (VitaGrafix, Vita3K): no frame-rate work. Confidence: high

- **VitaGrafix patch list:** https://github.com/Electry/VitaGrafixPatchlist/blob/main/patchlist.txt, at `0091bf1eaa4d`.
  - The LittleBigPlanet entry (PCSF00021, PCSA00017, PCSC00013 and PCSD00006, version 1.22, eboot 0x0714AF6B) has only an `@IB` section, the internal resolution, with eight `t2_mov` lines.
  - It has **no `@FPS` section**, while other games in the list do.
- **Vita3K compatibility issues:** #495 (PCSA00017), #188 (PCSF00021) and others, at https://github.com/Vita3K/compatibility/issues/495. The only timing-related notes are about audio:
  > audio is now working but it sounds robotic and slow

  > On latest Vita3K build, audio is faster but still sounds robotic.
  - There is no note on frame rate.

---

## Community knowledge of the 30 Hz logic rate

### F17. LBP creators' "30 frames per second" logic. Confidence: medium (tier B and C, old forum posts)

- **QuietlyWrong, "QW's LBP2 Logic Tutorial #4 - Timers and Counters"** (PlayStation EU forum). URL: https://community.eu.playstation.com/t5/LittleBigPlanet/QW-s-LBP2-Logic-Tutorial-4-Timers-and-Counters/m-p/13164980. The fetch failed; tier C only.
  - Per the search summary, the tutorial says LBP2's simulation runs at 30 frames per second, so a 0.1 s timer step spans three frames.
  - It also says "some timer settings are one frame short", that is, timer durations are quantized to 30 Hz frames.
- **LBPCentral archive, "Sticker Panel Animation"**, https://lbpcentral.lbp-hub.com/index.php?t=63528-Sticker-Panel-Animation- (tier B):
  > [Bremnen, 2011-09-26] "the selector will loop at 30 frames per second" ... "a single frame and its output are both displayed for 1/30 of a second each"
  > [killerbrainbow, 2011-11-08] "a NOT gate wired to itself only produces a 15Hz signal"
  > [zupaton, 2011-11-09] "Yeah, that would produce a 30Hz signal, but it's interpreted as a constant on signal by the game."
  - A NOT gate fed back into itself toggles once per logic tick, giving a two-tick period, or 15 Hz. That fits logic being evaluated once per 30 Hz tick.
- **LittleBigWiki "Timer"**, https://littlebigplanet.miraheze.org/wiki/Timer (tier B): no mention of frames. It gives only the 0 to 86,400 s range.
- **GameFAQs LBP3 board,** "Does this game run @ 60fps?", https://gamefaqs.gamespot.com/boards/805576-littlebigplanet-3/70537212. HTTP 403; tier C.
  - The summary has a poster claiming the developers had to keep frame rates equal across systems "for frame-perfect logic".
  - Another poster says the 30 cap exists so that "the logic in a lot of the story & community levels works properly".
  - This is user speculation and consistent with F13, not a developer statement. Confidence: low.

---

## LBP3 on PS4: official statements and hardware

### F18. Sony's statement: 30 fps / 1080p "to ensure we're able to build the game for both the PS3 and PS4". Confidence: high

- **GamingBolt, 22 July 2014,** https://gamingbolt.com/littlebigplanet-3-targeting-1080p-and-60fps-on-ps4-will-be-faithful-to-the-series-dna (tier B):
  > Daimion Pinnock confirmed that they are targeting 1080p and 60fps on the PS4.
  - Then the update from Sony:
  > The game will target 30fps/1080p to ensure we're able to build the game for both the PS3 and PS4 systems.
- **PlayStation LifeStyle, 23 July 2014,** https://www.playstationlifestyle.net/2014/07/23/sumo-digital-aiming-to-run-littlebigplanet-3-at-full-1080p30fps-on-both-platforms/ (tier B): the same Sony quote.
- **Reading:** Sony tied the 30 fps choice to shipping one game on both PS3 and PS4. That fits one shared simulation tuned for 30 ticks per second, though Sony did not say so.

### F19. LBP3 PS4 frame rate as shipped. Confidence: medium

- **Reviews:** Gaming Age and Press Start (search snippets, tier C) report 1080p at 30 fps on PS4. Press Start: "The game is presented in 1080p, and runs at a frame rate of 30 frames per second."
- **No Digital Foundry analysis of LBP3 was found.** Searches run:
  - "Digital Foundry LittleBigPlanet 3 PS4 frame rate analysis 30fps";
  - "eurogamer digital foundry "LittleBigPlanet 3" PS4 PS3 performance analysis face-off";
  - "\"Digital Foundry\" \"LittleBigPlanet 3\" frame rate".
  - A domain-restricted search on eurogamer.net and digitalfoundry.net was refused by the search tool.
- **User reports** on GameFAQs and PSNProfiles (tier C) describe drops to about 10 fps around Popit menus with many saved objects, and "things slow down". Patch 1.03 fixed Popit lag caused by large numbers of saved screenshots.

### F20. PS4 Pro and PS5: no change to the 30 fps cap found. Confidence: medium for the Pro; low-to-medium for the PS5

- **PS4 Pro:** the PSN title metadata for CUSA00473 shows `"neoEnable":0` (tier A, from the `FlexBy420/sce-tmdb-scraper` and `Xeeynamo/psndb` JSON).
  - "Neo" was the PS4 Pro's codename, so this most likely means no PS4 Pro mode. That meaning is inferred; the field is not documented anywhere found.
- **backwards-compatible.com,** https://www.backwards-compatible.com/games/littlebigplanet-3/. Bot check, could not read; summary only (tier C):
  - PS5: 1080p, 30 fps;
  - PS4 Pro: 1080p, 25–30 fps;
  - PS4: 1080p, 20–30 fps.
- **LBP Fandom wiki "PlayStation 5" page** (tier C summary): on PS5 the game is "still locked at 30 FPS" with steadier frame pacing and faster loading.
- **No PS5 Game Boost to 60 was found.** That is expected: Game Boost raises the frame rate only for games whose own cap allows it.

### F21. LBPPC: native PC port of LBP3 PS4 (CUSA00473 1.28) uses frame generation for "60". Confidence: high for what the project says; its "30 steps a second" is a project claim, not a published measurement

- **Source:** https://github.com/emurphy04/LBPPC, at `74aba5f602db` (created 2026-10-29), and https://lbppc.com/.
  - It is an open-source native port: "runs the PS4 game's own code directly on your PC's processor".
  - It descends from a Bloodborne PC port and uses shadPS4's graphics and online code.
  - **How it relates to this project is unknown.** It counts as independent PS4 evidence only if it is a separate project.
- **README** (tier A):
  > **Status:** 0.1.0, the first public beta. It supports LittleBigPlanet 3 US (CUSA00473), version 1.28.
  > - **60 FPS** with frame generation: DLSS on GeForce RTX 40 and 50 series cards, FSR 3.1 on any other. The game still steps 30 times a second, as on PS4, so physics and timing feel the same.
  > - **Native 60 and 120 FPS:** the game itself running faster, rather than frame generation filling in between 30.
- **lbppc.com** (tier B):
  > "LBP3's physics are built around 30 steps a second, so this is the hardest one on the list."
- **The repository has no published timestep analysis.** `docs/research.md` covers shadPS4 status and servers only. The one issue (#2) is a build error.

### F22. shadPS4: LBP3 only boots or reaches the menus, so no emulator-side frame-rate experiments exist. Confidence: high

- **Source:** https://github.com/shadps4-compatibility/shadps4-game-compatibility, issues #264 (CUSA00063, "status-menus"), #285, #642 and #193 (CUSA00473, "status-boots", "issues: hang"), #86 (CUSA00762) and #2202 (CUSA00693).

---

## What all this says about the timestep

| Model | Verdict | Evidence |
|---|---|---|
| **Variable dt** (measure real time each frame, scale by it) | **Ruled out** for the PS3 engine and for PSP LBP | Game speed follows frame rate both ways: about 2.1x at 60 (F12), slow motion below 30 (F9), "VBlank affects gamespeed" with clocks scale having no effect (F8). Halving the 1/30 floats does not change speed (F12) |
| **Fixed step with render interpolation** (sim at 30, render interpolated at 60) | **Ruled out** as stock behaviour | Nothing interpolates. With the world counter gated to 30 Hz, the image changes only on alternate frames (F12). LBPPC fills frames with frame generation, not engine interpolation (F21) |
| **Fixed step, frame-locked** (one 1/30 s tick per presented frame; flip every 2 VBlanks) | **Supported** for PS3 LBP1 (direct), PS3 LBP2 and LBP3 (VBlank list, tier C), and PSP LBP (direct) | Flip every 2 VBlanks (F12); 30 Hz logic loop of two VBlank waits per update (F15); integer frame counters in the world and level format (F13); creator lore of 30 fps logic (F17) |

**Where the per-tick assumption lives:**
1. **Code constants:** the jump boost decay of 1.48 per tick, the jump modifier, animation-clock readers and per-update blend factors (F12, F15).
2. **Game data files:** the `.cha` character settings (speeds and accelerations per tick, `Frames…` counts) and material `gravityMultiplier` (F12).
   - In `.anim` clips the tick dependence is the **frame count**. Clips advance one clip frame per tick, and the clip's `fps` field is ignored (F12).
3. **Level data, user-generated content included:** `PWorld.frame` and `simFrame`; emitter `frequency`, `phase` and `lifetime` as integers, presumably frames; emitter `lastUpdateFrame` seeded from the world `frame`; joint `animationTime` in frames; logic timers quantized to 30 Hz (F12, F13, F17).
4. **Exceptions:** some subsystems already use real time. In PSP LBP that is audio, some animation, and menus and fades (F15). Whether the same split holds in LBP3 PS4 is unknown.

**Why the experts went to frame generation:** both qwrwed (LBP1, PS3) and LBPPC (LBP3, PS4) chose frame generation over a 30 Hz simulation as the practical 60 fps. Retuning to a native 60 Hz tick reached only partial fidelity on PS3 (F12). On PSP it needed a 68-site census of a much smaller game (F15).

**Not verified for CUSA00473 01.28:** the PS4 build's actual mechanism.
- It may call `sceVideoOutSetFlipRate`, wait on VBlank or flip events, or use another limiter.
- Whether it keeps the PS3's per-tick design.

Everything mechanistic above is PS3, PSP or file-format evidence. The PS4-specific evidence is only:
- Sony's "30fps/1080p to ... build the game for both the PS3 and PS4" (F18);
- LBPPC's claim that the game "steps 30 times a second, as on PS4" (F21). This is independent only if LBPPC is not this project;
- the cross-generation shared level format (F13).

All three point the same way, but none is a measurement.

**Cheap local checks this suggests** (inference, not done here):
- Find where the PS4 eboot sets its frame cap: `sceVideoOutSetFlipRate` imports, VBlank event waits, or a frame-time limiter.
- Run once with the cap removed, as GoldHEN's `fliprate_remover` would (F5), to confirm the expected 2x speed.
- Find the PS4 counterparts of the world frame counter (`world+0x1b8` in LBP1) and of the `PWorld.frame` serialization.

---

## Leads seen only as titles (tier C, not opened)

- "LittleBigPlanet 3 - 60*FPS - AMD Fluid Motion Frames on RPCS3 Emulator (New driver)", https://www.youtube.com/watch?v=qll4K4UK0CI. It shows the PS3 LBP3 at 60 through driver frame generation, which is another frame-generation route.
- "Why You Should Not Run LittleBigPlanet At Unlocked Framerate? #RPCS3 #shorts #lbp2 #littlebigplanet", https://www.youtube.com/shorts/oI82hoV6zEk. Presumably shows the speed-up from F8 and F9.
- "LittleBigPlanet PS Vita Frame-Rate Tests", https://www.youtube.com/watch?v=kY4-GFGYZLw. Its content is unknown; relevant to item 4.

## Could not read or verify

- The Orbis Unjailed fps page (404; web.archive.org and archive.ph unreachable from here).
- The RPCS3 wiki pages for LittleBigPlanet, LittleBigPlanet 2, LittleBigPlanet 3 and LittleBigPlanet Karting, the VBlank list and the RPCS3 forums (HTTP 403, Cloudflare).
- GameFAQs threads (403).
- The QW tutorial (fetch failed).
- backwards-compatible.com (bot check).
- TCRF's LBP3 PS4 page: the fetch returned an injected page aimed at AI agents instead of the article. Its instructions were ignored and the page was not used.
- "Papy's BCES00141 1.30 notes", cited by qwrwed: not found online.
- Any Digital Foundry analysis of LBP3 (none found; see F19).
