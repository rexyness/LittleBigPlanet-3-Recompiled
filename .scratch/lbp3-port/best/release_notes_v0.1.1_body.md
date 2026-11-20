**v0.1.1: an online co-op fix, and the sign-in that play.lbppc.com will soon require. Everyone should update.**
If you have v0.1.0, open the launcher and go to **Tools > Check for updates**. It installs v0.1.1 and keeps
your saves, settings and accounts. New here? Read on. **No game files are included:** you need your own copy
of the game.

### What's in it (new in v0.1.1)
- **Online co-op:** a lost game message no longer ends the session. **Both players need v0.1.1** for this: a
  v0.1.1 player and a v0.1.0 player still connect, but without the protection, so their session can drop as before.
- **Players on the same network** connect over that network first.
- **Sign-in codes (LBPPC 0.1.3):** the game's sign-in to play.lbppc.com now carries a one-time code from shadNet.
  The server will soon require it, and older versions won't be able to sign in once it does.
- **Fresh Windows installs:** the game and the launcher bring Let's Encrypt's root certificates (ISRG Root X1
  and X2), so online works even where Windows hasn't fetched them yet.
- **Clearer account messages:** when sign-in is paused for up to 15 minutes after too many wrong passwords, when
  the server doesn't accept a name or password (passwords need 8 to 64 characters), and when your network has
  made too many new accounts today.

### What you need
- Windows 10 (1903 or later) or Windows 11, 64-bit.
- A graphics card with Vulkan 1.3 and a current driver. Tested on an NVIDIA RTX 3090; other cards are untested.
- **Your own LittleBigPlanet 3, US edition (CUSA00473), updated to 01.28,** extracted to a folder named `CUSA00473`
  (the base game with the 01.28 patch merged in).
- Optional: your own DLC `.pkg` files and a PS4 pkg extractor.

### Getting started
1. Download `LBP3-Recompiled-v0.1.1-lbp3port.zip` and unzip it somewhere you can write to (not Program Files).
2. Run `LBP3Launcher.exe`. The setup asks for the folder that contains `CUSA00473` (pick it, or type or paste its
   path) and checks it.
3. Press **PLAY**.

The first visit to each area can pause briefly while your graphics driver builds shaders. It happens once.

### Also in this port
- 1440p rendering, frame generation (60 on screen), and the PS4's 7.1 audio mixed properly for stereo.
- Local co-op with up to four controllers; DualSense and DualShock 4 touchpad and motion.
- The game's text entry, photos and system dialogs drawn in game; Create mode works.
- Your DLC, from your own packs, and the old "My Unlocks" LBP1/LBP2 items.
- **Online on the community servers at play.lbppc.com** (made by the LBPPC project; Sony's servers are gone).
  Create an account from the launcher's Online page. Community levels, your profile and your friends list work,
  and you can invite friends and join their games.
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
- Dive In doesn't match you with strangers on play.lbppc.com; play with friends through invites or Join.
- First-visit shader pauses; the very first start after a reboot can take 20-40 seconds.

### Credits and source
Built on PurpleGitty's LittleBigPlanet 3 Recompiled and the shadPS4 graphics core, with several pieces from
LBPPC by emurphy04 (credited in the code), including the shadNet sign-in code request from LBPPC 0.1.3. Their
team also runs play.lbppc.com and allowed this port to use it. Full credits are in `CREDITS.md`.

This is free software under the GNU GPL. The source is this repository at the tagged commit, with its shadPS4
submodule at github.com/rexyness/shadPS4 (the exact commits are in `SOURCE.md` in the zip).

LittleBigPlanet is a trademark of Sony Interactive Entertainment. This project isn't affiliated with or endorsed by Sony,
Sumo Digital or Media Molecule.
