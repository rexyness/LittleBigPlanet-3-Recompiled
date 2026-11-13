**A first test release.** LittleBigPlanet 3 (PS4) running natively on Windows, with a launcher. **No game files
are included:** you need your own copy of the game.

### What you need
- Windows 10 (1903 or later) or Windows 11, 64-bit.
- A graphics card with Vulkan 1.3 and a current driver. Tested on an NVIDIA RTX 3090; other cards are untested.
- **Your own LittleBigPlanet 3, US edition (CUSA00473), updated to 01.28,** extracted to a folder named `CUSA00473`
  (the base game with the 01.28 patch merged in).
- Optional: your own DLC `.pkg` files and a PS4 pkg extractor.

### Getting started
1. Download `LBP3-Recompiled-v0.1.0-lbp3port.zip` and unzip it somewhere you can write to (not Program Files).
2. Run `LBP3Launcher.exe`. The setup asks for the folder that contains `CUSA00473` and checks it.
3. Press **PLAY**.

The first visit to each area can pause briefly while your graphics driver builds shaders; it happens once.

### What's in it
- 1440p rendering, frame generation (60 on screen), and the PS4's 7.1 audio mixed properly for stereo.
- Local co-op with up to four controllers; DualSense and DualShock 4 touchpad and motion.
- The game's text entry, photos and system dialogs drawn in game; Create mode works.
- Your DLC, from your own packs, and the old "My Unlocks" LBP1/LBP2 items.
- **Online on the community servers at play.lbppc.com** (made by the LBPPC project; Sony's servers are gone):
  create an account from the launcher's Online page. Community levels, Dive In, your profile and your
  friends list work. Joining and inviting friends is new and **experimental** (not yet tested between two PCs).
- A launcher with a setup wizard, a game file check, a DLC installer, save backups, a controller test, play
  stats and updates.

### Known issues
- Some scenes are brighter than on a PS4.
- Online: joining and inviting friends is experimental; expect problems. Only your shadNet friends can join you.
- First-visit shader pauses; the very first start after a reboot can take 20-40 seconds.

### Credits and source
Built on PurpleGitty's LittleBigPlanet 3 Recompiled and the shadPS4 graphics core, with several pieces from
LBPPC by emurphy04 (credited in the code), whose team also runs play.lbppc.com and allowed this port to use it.
Full credits are in `CREDITS.md`. This is free software under the GNU GPL. The source is this repository at
the tagged commit, with its shadPS4 submodule at github.com/rexyness/shadPS4 (the exact commits are in
`SOURCE.md` in the zip).

LittleBigPlanet is a trademark of Sony Interactive Entertainment. This project isn't affiliated with or endorsed by Sony,
Sumo Digital or Media Molecule.
