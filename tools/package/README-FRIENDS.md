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

The first time you visit each part of the game it can pause for a second or two while your graphics driver
builds shaders. That happens once; they're kept in `user\shader_cache` for next time. The very first start
after a reboot can also take 20-40 seconds while Windows reads that cache.

## Playing online (optional)

Online play uses the community servers at **play.lbppc.com** (made by the LBPPC project; Sony's servers are
gone). You need your own account there: on the launcher's **Online** page, type an online name and press
**Create account**, then switch **Online** on. Your password stays in `user\config\online_account.txt` on your
PC. Community levels, your profile and your friends list work. Dive In doesn't match you with strangers on this
server; play with friends through invites or Join. Add friends by name on the launcher's **Friends** page.

Online co-op is **experimental**: inviting and joining a friend works (from the game's own menus), but the
session may drop after a few minutes. A fix is in progress and will arrive through the launcher's
updater. Only your shadNet friends can join your game.

If the game says *"A problem occurred when attempting to connect to the LittleBigPlanet servers (403)"*, the
sign-in failed, usually because of a wrong password in that file.

## Controls

A DualSense or DualShock 4 is best (touchpad and motion work); any controller SDL supports works. Up to four
controllers for local co-op. **Insert** on the keyboard, or **L3+R3** on the pad, opens the port's own menu
(performance stats, frame generation, volume, quit game).

## Where things are

- `user\` - your settings, saves (`user\savedata\`) and shader cache. Back up `user\savedata\` to keep your
  progress. When updating by hand, keep `user\` (unzip the new version over this folder).
- **Updates:** the launcher's **Tools** page checks for a new version and installs it (it checks the download
  first, and never touches `user\` or `analysis\`).
- `analysis\` - logs (`play_launcher_log.txt`) and save backups. **Make a report zip** in the launcher
  bundles the logs for whoever helps you.

## Known issues

- Brightness in some scenes is higher than on a PS4.
- Online co-op is experimental: inviting and joining a friend works, but the session may drop after a
  few minutes. A fix is in progress and will arrive through the launcher's updater.
- The first-visit shader pauses described above.

See CREDITS.md for who made what, and SOURCE.md for the source code.
