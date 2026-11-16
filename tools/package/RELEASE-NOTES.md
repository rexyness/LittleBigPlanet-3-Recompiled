**v0.1.6: fixes from LBPPC 0.1.4 for Intel and Radeon PCs, and no shader freezes online.**
If you have v0.1.4 or v0.1.5, the launcher shows "v0.1.6 is out" when it opens: press **Update**. From v0.1.1 to
v0.1.3, go to **Tools > Check for updates**. It installs v0.1.6 and keeps your saves, settings and accounts. New here?
Read on. **No game files are included:** you need your own copy of the game.

### What's in it (new in v0.1.6)
- **The intro cutscene no longer gets stuck on Intel processors.** On an Intel CPU the story's first cutscene stopped
  for good after Newton's line (the game kept running, but the next scripted step never came). A few of the game's
  maths instructions give slightly different answers on Intel than on the PS4's AMD processor; on PCs with a CPU
  that isn't AMD's, the exact versions already used online are now used offline too, and the intro plays through.
  AMD PCs are unchanged offline. From LBPPC 0.1.4.
- **Radeon cards and other programs' overlays.** Overwolf's, Epic Games' and Rockstar's Vulkan overlays, which
  Windows loads into every game, are kept out of this one: on a Radeon PC they made the game close straight away
  ("Game exited with 0x00000017"). Steam's shader pre-caching, which can't record this game and led to crashes in
  AMD's graphics driver when the game was started from Steam, is kept out too. Steam's overlay, OBS, RivaTuner and
  the graphics card's own layers still work. The game window is also handed to the graphics driver the way Vulkan
  requires (some drivers refused it before). From LBPPC 0.1.4.
- **New shaders are built in the background when playing online, by default.** The launcher's "Build new shaders
  in the background" (Graphics & sound > Shaders) is now **Never**, **When playing online** (the new default) or
  **Always**. A first-time shader build froze a friend's game for 1 to 3.5 seconds at a time, and meanwhile the host
  saw them stand still, shown away, with the death countdown. If you had it on (Always), it stays on.
- Nothing in online play changed: v0.1.3 to v0.1.6 players can play together.

### New in v0.1.5
- **Import LBP1/LBP2 items from the in-game menu.** Press **L3+R3** (or **Insert**), choose **Import LBP1/LBP2
  items**, then open the start menu with **Options**. The game's own import runs (about 2 minutes); press Cross on
  "successfully imported". It adds the LittleBigPlanet and LittleBigPlanet 2 items that come with LBP3 to your
  profile, which LBP3's My Unlocks page used to do before Sony's servers closed. It works in the Pod or in a level,
  Circle can't cancel it by accident, and running it again on a profile that has the items adds nothing.
- The launcher's "Import the LBP1/LBP2 items once" switch is gone; if you left it on, it no longer imports at every
  start.
- Nothing in online play changed: v0.1.3, v0.1.4 and v0.1.5 players can play together.

### New in v0.1.4
- **Photos, saved costumes and your profile picture are no longer streaked.** Since v0.1.0, at the default 2560x1440
  (and other sizes above 1080p), Popit photos, the pictures of costumes you save, level icons and your profile
  picture came out as smeared horizontal streaks. The game captures them into the same small screen buffers its
  glow uses, and those had been made bigger above 1080p, in a size the graphics card stores with padded rows, so
  every row slid sideways. They're back at the PS4's sizes; photos also show the whole photo frame again instead of
  its middle. **Pictures you already took or saved stay streaked** (they're stored that way): delete and retake
  those photos, and save those costumes again.
- **The launcher checks for updates when it opens** and says so under the title when a new version is out (Update
  takes you to Tools, where you install it with one button). It's on for everyone, including after this update;
  Tools > Updates turns it off.
- The launcher's "Finer glow above 1080p" switch is gone (it was what made the buffers bigger; the glow looks the
  same without it).
- Nothing in online play changed: v0.1.3 and v0.1.4 players can play together.

### New in v0.1.3
- **Exact maths online, so AMD and Intel players stay in step.** Online co-op runs both players' worlds side by
  side and compares them. A few of the game's maths instructions give slightly different answers on AMD and Intel
  processors (no problem on a PS4, where every console is the same), so the worlds could drift apart and the
  session drop ("The game diverged"). Online, all 3,373 of them now compute the exact result, the same on every
  PC. This is LBPPC's fix, ported. **Both players need v0.1.3:** a player on an older version can't join (their
  game would drift apart from yours).
- **Build new shaders in the background** (launcher, Graphics & sound > Shaders; off by default). No freeze the
  first time something new is drawn; instead it can appear a moment late. In a test from empty caches, the
  worst freeze walking through a level went from 1.75 s to 0.14 s. Also from LBPPC.
- The project's licence is now the GNU GPL version 3 or later (it was version 2 or later).

### New in v0.1.2
- A loading screen instead of a black window while the game starts, and faster first starts after a PC restart.
- Nine resolutions on the launcher's Graphics & sound page: 1280x720, 1920x1080, 2560x1440 (the default),
  3840x2160 (4K), and ultrawide 2560x1080, 3440x1440, 3840x1600, 3840x1080 and 4480x1260. Ultrawide widens the
  view; on a 16:9 screen it shows with black bars. 4K needs a strong graphics card.
- Your in-game name is your online account's name.

### What you need
- Windows 10 (1903 or later) or Windows 11, 64-bit.
- A graphics card with Vulkan 1.3 and a current driver. Tested on an NVIDIA RTX 3090; other cards are untested.
- **Your own LittleBigPlanet 3, US edition (CUSA00473), updated to 01.28,** extracted to a folder named `CUSA00473`
  (the base game with the 01.28 patch merged in).
- Optional: your own DLC `.pkg` files and a PS4 pkg extractor.

### Getting started
1. Download `LBP3-Recompiled-v0.1.6-lbp3port.zip` and unzip it somewhere you can write to (not Program Files).
2. Run `LBP3Launcher.exe`. The setup asks for the folder that contains `CUSA00473` (pick it, or type or paste its
   path) and checks it.
3. Press **PLAY**.

The first visit to each area can pause briefly while your graphics driver builds shaders. It happens once.

### Also in this port
- Frame generation (60 on screen), and the PS4's 7.1 audio mixed properly for stereo.
- Local co-op with up to four controllers; DualSense and DualShock 4 touchpad and motion.
- The game's text entry, photos and system dialogs drawn in game; Create mode works.
- Your DLC, from your own packs, and the old "My Unlocks" LBP1/LBP2 items (in-game menu, L3+R3).
- An in-game menu on L3+R3 or Insert: frame rate and latency, frame generation on/off, volume, the item import, Quit.
- **Online on the community servers at play.lbppc.com** (made by the LBPPC project; Sony's servers are gone).
  Create an account from the launcher's Online page. Community levels, your profile and your friends list work,
  and you can invite friends and join their games. Since v0.1.1 a lost game message no longer ends a co-op
  session when both players have v0.1.1 or later.
- A launcher with:
  - a setup wizard and a game file check;
  - a DLC installer and save backups;
  - a controller test and play stats;
  - updates. It checks each download against its `.sha256` and never touches your `user\` folder.

### Known issues
- Some scenes are brighter than on a PS4.
- Online co-op is still new:
  - it hasn't been tested yet between two PCs on different networks;
  - leaving and rejoining, and playing through real levels together, are only lightly tested;
  - a burst of lost game messages can cause a short hitch;
  - the joining player's game may be slightly less smooth than the host's;
  - only your shadNet friends can join you.
- **Online co-op with an ultrawide player hasn't been tested with two players yet.** The game's camera stays 16:9
  for the simulation and only the picture is widened, which should keep both games in step. If a session drops
  with "The game diverged", switch to a 16:9 size.
- Ultrawide sizes take wider photos (stickers and level icons).
- Dive In doesn't match you with strangers on play.lbppc.com; play with friends through invites or Join.
- First-visit shader pauses when playing offline (unless "Build new shaders in the background" is set to Always;
  then new things can appear a moment late instead, as they do online by default).
- Intel 12th-generation and later processors: LBPPC found crashes there (after the first cutscene, or when a level
  loads) whose fix isn't in this port yet. If that happens to you, please send a report (Tools > Make a report zip).
- Exact maths online costs about half a millisecond a frame (of the 33 ms the game has); it has been proven exact
  in tests on one PC, but a real AMD-against-Intel co-op session hasn't been played yet.

### Credits and source
Built on PurpleGitty's LittleBigPlanet 3 Recompiled and the shadPS4 graphics core, with several pieces from
LBPPC by emurphy04 (credited in the code), including the shadNet sign-in code request from LBPPC 0.1.3, the
game addresses behind the ultrawide view, the exact maths for online play, the background shader builds, and from
LBPPC 0.1.4 the Intel intro fix and the Radeon start-up fixes. Their team also runs play.lbppc.com and allowed this port to use it.
Full credits are in `CREDITS.md`.

This is free software under the GNU GPL. The source is this repository at the tagged commit, with its shadPS4
submodule at github.com/rexyness/shadPS4 (the exact commits are in `SOURCE.md` in the zip).

LittleBigPlanet is a trademark of Sony Interactive Entertainment. This project isn't affiliated with or endorsed by Sony,
Sumo Digital or Media Molecule.
