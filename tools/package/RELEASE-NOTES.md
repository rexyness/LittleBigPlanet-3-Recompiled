**v0.1.2: a loading screen, faster first starts, and more resolutions, from 720p to 4K and ultrawide.**
If you have v0.1.1, open the launcher and go to **Tools > Check for updates**. It installs v0.1.2 and keeps
your saves, settings and accounts. New here? Read on. **No game files are included:** you need your own copy
of the game.

### What's in it (new in v0.1.2)
- **A loading screen instead of a black window** while the game starts. It shows your game's own background art
  and icon, and how far the shader loading has got ("Loading shaders 3,296 / 7,022"). It fades into the game.
- **Faster first start after a PC restart.** When the shader cache is slow to read, the rest of it is now read on
  many threads at once. In a test with "cold" files, reading went from 75.6 s to 11.8 s. Normal starts are unchanged.
- **Nine resolutions** on the launcher's Graphics & sound page:
  - 16:9: 1280x720, 1920x1080 (the PS4's own), 2560x1440 (the default) and 3840x2160 (4K);
  - ultrawide: 2560x1080, 3440x1440, 3840x1600, 3840x1080 and 4480x1260.

  Ultrawide widens the view, menus included, as on a real ultrawide monitor; on a 16:9 screen it shows with
  black bars. 4K needs a strong graphics card: on an RTX 3090, heavy scenes drop below 30 frames a second.
  2560x1440 settings from earlier versions carry over, with your shader cache.
- **Your in-game name is your online account's name** (it was your Windows user name).

### What you need
- Windows 10 (1903 or later) or Windows 11, 64-bit.
- A graphics card with Vulkan 1.3 and a current driver. Tested on an NVIDIA RTX 3090; other cards are untested.
- **Your own LittleBigPlanet 3, US edition (CUSA00473), updated to 01.28,** extracted to a folder named `CUSA00473`
  (the base game with the 01.28 patch merged in).
- Optional: your own DLC `.pkg` files and a PS4 pkg extractor.

### Getting started
1. Download `LBP3-Recompiled-v0.1.2-lbp3port.zip` and unzip it somewhere you can write to (not Program Files).
2. Run `LBP3Launcher.exe`. The setup asks for the folder that contains `CUSA00473` (pick it, or type or paste its
   path) and checks it.
3. Press **PLAY**.

The first visit to each area can pause briefly while your graphics driver builds shaders. It happens once.

### Also in this port
- Frame generation (60 on screen), and the PS4's 7.1 audio mixed properly for stereo.
- Local co-op with up to four controllers; DualSense and DualShock 4 touchpad and motion.
- The game's text entry, photos and system dialogs drawn in game; Create mode works.
- Your DLC, from your own packs, and the old "My Unlocks" LBP1/LBP2 items.
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
- First-visit shader pauses.

### Credits and source
Built on PurpleGitty's LittleBigPlanet 3 Recompiled and the shadPS4 graphics core, with several pieces from
LBPPC by emurphy04 (credited in the code), including the shadNet sign-in code request from LBPPC 0.1.3 and the
game addresses behind the ultrawide view. Their team also runs play.lbppc.com and allowed this port to use it.
Full credits are in `CREDITS.md`.

This is free software under the GNU GPL. The source is this repository at the tagged commit, with its shadPS4
submodule at github.com/rexyness/shadPS4 (the exact commits are in `SOURCE.md` in the zip).

LittleBigPlanet is a trademark of Sony Interactive Entertainment. This project isn't affiliated with or endorsed by Sony,
Sumo Digital or Media Molecule.
