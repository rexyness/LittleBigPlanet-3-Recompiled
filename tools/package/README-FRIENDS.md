# LittleBigPlanet 3 Recompiled: getting started

This runs **your own copy** of LittleBigPlanet 3 (PS4) natively on Windows. Nothing from the game comes in
this download: you bring the game, and your DLC if you have any.

## What you need

- **Windows 10 (version 1903 or later) or Windows 11, 64-bit.**
- **A graphics card with Vulkan 1.3** and a current driver. Tested on an NVIDIA RTX 3090; other cards
  should work but haven't been tested.
- **Your own LittleBigPlanet 3, US edition (CUSA00473), updated to version 01.28**, extracted to a folder: the
  base game with the 01.28 patch merged in. It's the folder with `eboot.bin` and `sce_sys\`. Name the folder
  `CUSA00473`, inside a parent folder of your choice (for example `D:\Games\PS4\CUSA00473`).
- **Optional, your DLC:** your own DLC `.pkg` files, extracted into a `CUSA00473-DLC` folder next to
  `CUSA00473`. The launcher's DLC page can do the extracting with `pkg_extractor.exe`, a separate third-party
  tool that isn't included. With another extractor, extract the packs into that folder yourself.
- About 2 GB of free space next to the launcher: the game's own cache (`user\temp0`, about 600 MB from the first
  start), saves and the shader cache (more as you play).

## First start

1. Unzip this folder anywhere you can write to (not `Program Files`), for example `D:\Games\LBP3-Recompiled`.
2. Run **LBP3Launcher.exe**. The programs aren't code-signed, so Windows SmartScreen may say "Windows protected
   your PC": choose **More info**, then **Run anyway**. The setup opens on the first start:
   - **Game files:** pick the folder that CONTAINS `CUSA00473` (the parent folder), or type or paste its path.
     The launcher checks it.
   - **DLC:** optional; see above.
   - **Online:** optional; see below.
3. Press **PLAY**.

A new profile starts with the game's brightness screen (press Cross), then the Pod. With online off, the game
says *"There is a problem with your network connection"* once; press **Close**. That's expected offline.

While the game starts, a loading screen shows your game's own art and how far the shader loading has got.
The first time you visit each part of the game it can pause for a second or two while your graphics driver
builds shaders. That happens once; they're kept in `user\shader_cache` for next time. The first start after a
reboot takes longer while that cache is read back from disk. To avoid those pauses, switch on **Build new
shaders in the background** (launcher, **Graphics & sound** > Shaders): no freeze, but something new can appear
a moment late the first time it's drawn.

## Picture

The launcher's **Graphics & sound** page sets the **Resolution** the game draws at: 1280x720, 1920x1080 (the
PS4's own), 2560x1440 (the default), 3840x2160 (4K), or an ultrawide size (2560x1080, 3440x1440, 3840x1600,
3840x1080, 4480x1260). Ultrawide widens the view, as on a real ultrawide monitor; on a 16:9 screen it shows with
black bars. 4K needs a strong graphics card: on an RTX 3090, heavy scenes drop below 30 frames a second.
**60 frames a second on screen** (frame generation) is on by default.

## Playing online (optional)

Online play uses the community servers at **play.lbppc.com** (made by the LBPPC project; Sony's servers are
gone). You need your own account there: on the launcher's **Online** page, type an online name and press
**Create account**, then switch **Online** on. Your password stays in `user\config\online_account.txt` on your
PC. Community levels, your profile and your friends list work. Dive In doesn't match you with strangers on this
server; play with friends through invites or Join. Add friends by name on the launcher's **Friends** page.

Online co-op is new: invite a friend or join their game from the game's own menus. Only your shadNet friends
can join your game. **Both of you need the same version, v0.1.3 or later** (Tools > Check for updates). Since
v0.1.3 the game does its maths exactly online, so a PC with an AMD processor and one with an Intel processor
stay in step; a player on an older version can't join (their game would drift apart from yours). Since v0.1.1
a lost game message no longer ends the session. Friends on the same network connect over it directly.

If the game says *"A problem occurred when attempting to connect to the LittleBigPlanet servers (403)"*, the
sign-in failed. Usually the password in that file is wrong; the launcher's **Online** page can test it. The
other cause is an out-of-date version: play.lbppc.com is switching to the sign-in codes that came with v0.1.1 and
will then refuse older versions, so update (Tools > Check for updates). After too many wrong passwords the server
pauses sign-in for up to 15 minutes.

## Controls

A DualSense or DualShock 4 is best (touchpad and motion work); any controller SDL supports works. Up to four
controllers for local co-op. **Insert** on the keyboard, or **L3+R3** on the pad, opens the port's own menu
(performance stats, frame generation, volume, quit game, and **Import LBP1/LBP2 items**: the old "My Unlocks",
whose servers are gone. Choose it, then open the start menu with Options; the game's own import runs for about
2 minutes, then press Cross on "successfully imported").

## Where things are

- `user\` - your settings, saves (`user\savedata\`) and shader cache. Back up `user\savedata\` to keep your
  progress. When updating by hand, keep `user\` (unzip the new version over this folder).
- **Updates:** the launcher checks for a new version each time it opens (since v0.1.4; Tools > Updates turns
  that off) and says so under the title when one is out. Its **Tools** page installs it (it checks the download
  first, and never touches `user\` or `analysis\`). Nothing is installed until you press the button.
- `analysis\` - logs (`play_launcher_log.txt`) and save backups. **Make a report zip** in the launcher
  bundles the logs for whoever helps you.

## Known issues

- Brightness in some scenes is higher than on a PS4.
- Online co-op is still new:
  - it hasn't been tested yet between two PCs on different networks;
  - leaving and rejoining, and playing through real levels together, are only lightly tested;
  - a burst of lost game messages can cause a short hitch;
  - the joining player's game may be slightly less smooth than the host's.
- Dive In doesn't match you with strangers on play.lbppc.com; play with friends through invites or Join.
- Online co-op with an ultrawide player hasn't been tested with two players yet (the game's camera stays 16:9
  for the simulation and only the picture is widened, which should keep both games in step). If a session drops
  with "The game diverged", switch to a 16:9 size.
- Ultrawide sizes take wider photos (stickers and level icons).
- The first-visit shader pauses described above.

See CREDITS.md for who made what, and SOURCE.md for the source code.
