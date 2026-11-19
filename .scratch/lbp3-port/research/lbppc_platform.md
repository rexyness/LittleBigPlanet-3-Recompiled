# LBPPC: platform features compared with our port (lane C)

Source: https://github.com/emurphy04/LBPPC, commit `aad6a91bb7035e242899b223a16b49d91fb194ea` (2026-11-12, "0.1.2 hotfix"), read from a read-only shallow clone in `D:\Recomps\lbp3-wt\_ref\LBPPC`; nothing was built or run. Our side is `port` `34e3e8d`. Lane A covers the licence, shader compilation and architecture; lane B covers graphics.

**Licence (only as it affects reuse; lane A has the full answer):**
- The project as a whole is GPL-3.0-or-later (README line 159).
- Files carrying `SPDX-License-Identifier: GPL-2.0-or-later` can be reused in our GPL-2.0 fork, keeping their notices. These are all of `gpu/bbnet/` (shadPS4 v0.20.0's network code plus LBPPC's changes, per `gpu/bbnet/SHADNET-PORT.md`), `gpu/bbnet/bbnet_glue.cpp`, `gpu/shim/bbport_overlay.cpp`, `scripts/quit_menu.py` and `src/runtime_input_config.c`.
- Files without an SPDX line are GPL-3.0-or-later: `src/probe.c`, `runtime_pad.c`, `runtime_motion.c`, `runtime_mouse_touch.c`, `runtime_content.c`, `runtime_audio.c`, `scripts/save_import.py` and the launcher. For those, take ideas and facts (addresses, formats) only, unless our fork can move to GPL-3.

## 1. Online: the big one

LBPPC plays online on community servers. Its public server is `play.lbppc.com`, with about 10 million archived community levels (`docs/online.md`, README).

| Piece | What it is | Where in LBPPC |
|---|---|---|
| [Refresh](https://github.com/LittleBigRefresh/Refresh) | The LBP game server: profiles, levels, comments, matchmaking (TCP 10061). Run with `UseTicketVerification false` and `EnforcePatchwork false`. | `server/linux/refresh/` |
| [shadNet](https://github.com/shadps4-emu/shadNet) | The PSN stand-in: sign-in, friends, Matching2 signaling (TCP 31313, UDP 31314, TCP 31315). LBPPC patches it with a relay and "same game" for the friends list. | `server/linux/shadnet/patches/lbppc-shadnet.patch` |
| Client network stack | shadPS4 v0.20.0's own `net`, `network` (libSceHttp), `np` (manager, Matching2, signaling, auth) and `shadnet` client, vendored and glued onto their runtime | `gpu/bbnet/vendor/shadps4/**`, `gpu/bbnet/bbnet_glue.cpp`, `gpu/bbnet/include/` |

What LBPPC changes so LBP3 talks to a custom server (`docs/online.md` "What LBPPC changes"):
1. **Server address:** the game's server address points at the configured Refresh server (`LBPPC_SERVER`).
2. **Login ticket:** the PS4 PSN login (an authorization code) becomes a PS3-style NP ticket for the player, which custom servers read (`http.cpp` `BuildLocalLoginTicket`, about line 435).
3. **Signing key:** the key LBP3 signs its requests and checks replies with (HMAC-SHA1, X-Digest-A/B) becomes `CustomServerDigest`. The original key is built from three immediates at eboot `0x1edb9`, `0x1eda8` and `0x1edd6`; the reply check uses the string at `0xe4aff1` (`src/probe.c` `custom_server_install`, about lines 1004–1022). It's four byte edits, verified against the original bytes.
4. **Endpoints the PS4 game needs:** `/farc_hashes` and `/t_conf`, which Refresh disables, are answered with empty signed replies, because the PS4 game treats a failed `farc_hashes` as a failed connection and plays offline (`http.cpp` about line 1876).
5. **Co-op room updates:** Refresh answers a guest's room updates with 401, and these are turned into a signed success; otherwise the guest is dropped back to its Pod after a level (`http.cpp` about line 1934).
6. **Friends:** `/npdata` is answered with the friends the game sent, so the Pod's friends list shows LBPPC friends (`http.cpp` about line 1901).
7. **Join protection:** only shadNet friends, or players you joined first, can connect. This closes the "force join" exploit that ended the old community servers.
8. **Lockstep across CPUs:** online co-op runs both worlds in lockstep and compares them. `rcpps`, `rsqrtps` and `dpps` differ between Intel and AMD, so every such site in the eboot and `spu.prx` jumps to exact math (`src/exact_math_sites.inc`, `probe.c` about line 1409). The game also ends the session on the first difference, which LBPPC tolerates a few times after a level change (`probe.c` about lines 408–490).

**Status in LBPPC:**
- Released (0.1.0–0.1.2): solo online, browsing and playing archived levels, accounts and friends.
- Online co-op is released, but `docs/feature-status.md` still lists NET-02 joining as failing ("failed to join"), and the 0.1.0 notes call co-op "new". Publishing levels (NET-03) and leaderboards (NET-05) are not started.

**Our port:**
- The runtime reports PSN as signed out and the network as disconnected.
- HTTP and DNS calls fail after `LBP3_NET_FAIL_MS`.
- Upstream's patch at eboot `0x360b5b` skips the connect timeout.
- There is no online at all.

**Adopting it:**
- **What the user gets:** community levels (about 10 M), online play with friends, and possibly publishing.
- **Effort:** large, about 3–5 sessions.
  - Bring in shadPS4's network libraries. Our submodule is shadPS4, but our HLE runtime is PurpleGitty's, so the libraries need glue the way `bbnet_glue.cpp` does it (GPL-2.0-or-later, reusable).
  - Register them in our linker in place of our failing stubs.
  - Add the digest patch, the ticket, the endpoint answers and join protection.
  - Ideally make the lockstep exact-math change too, for co-op with a friend on a different CPU maker.
- **Open questions for the user:**
  - Whether to use LBPPC's public server: it's their service, and they may not want third-party clients, so ask first. The alternative is hosting their own Refresh + shadNet, which LBPPC's `server/linux/` documents.
  - Online also means accounts and exposure to other players. That's the user's call.

## 2. DLC and unlocks

- **The same discovery as ours:** LBP3 grants a pack only when its status is 0 ("no extra data"), and never reads the key (`src/runtime_content.c`).
- **They cap the list at 64 add-ons** (`MAX_ADDONS 64`). With the user's 256 packs, LBPPC would list only 64; ours lists all 256 (ticket 18).
- **Pack checks:**
  - They accept a folder only if its `sce_sys/param.sfo` is CATEGORY `ac` with a CONTENT_ID ending in the folder's label.
  - Ours parses the param.sfo too. Our `BCUS98148` PS3 pack lives in a second title folder, which they don't scan.
- **Their setup wizard** copies DLC from a folder into `user/addcont/CUSA00473/<label>`.
- **My Unlocks:** they have nothing for it (no `HasLBP1`/`ActivateImportScreen`). Ours (ticket 19) is ahead.
- **Nothing to adopt.**

## 3. Input

| Feature | LBPPC | Ours | Worth adopting? |
|---|---|---|---|
| Motion (gyro/accel) | SDL3 sensors. Raw accel in G, gyro with its offset learned while still, rates under 0.008 rad/s reported as 0, orientation from a Mahony filter. Tested: Sackboy's head follows the **acceleration** field only (`docs/input-research.md`, `src/runtime_motion.c`). | Sensors enabled; accel passed through, identity orientation (`input/gamepad_sdl.cpp` 424–463); no gyro-offset learning | **Yes, small.** Gyro offset learning and idle zeroing, plus a proper orientation. The head-tilt finding says accel matters most, so ours is probably mostly fine already; verify a still pad doesn't make Sackboy's head drift. About 0.5 session; reimplement (GPL-3 file). |
| Tilt without sensors | `tilt_*` bindings (numpad by default) drive the motion fields | none | Only if the user ever plays with a pad without sensors; they use a DualSense. Low. |
| Keyboard/mouse remap | shadPS4-compatible `input.ini` (keys, mouse buttons, controller buttons, mouse→stick); a launcher Controls page edits it | `user\config\input.ini`: keyboard actions only | Optional. The parser `src/runtime_input_config.c` is GPL-2.0-or-later and reusable. Low priority for a DualSense player. |
| Mouse as touchpad (touch Create Mode) | `mouse_to_joystick = touchpad` drives LBP3's own touch Create Mode; Popit navigation tested | touchpad points from DS4/DualSense only | Nice for Create with mouse. Medium effort; reimplement. |
| Active controller | 0.1.1: with several pads plugged in, player 1 follows the pad in use | each pad is its own player (local co-op, ticket 13) | **Ours is ahead.** LBPPC is "one pad (user 1) only" (PC-05 Not Started). |
| Rumble / light bar | `SDL_RumbleGamepad` | `scePadSetVibration` and `scePadSetLightBar` implemented (`hle/libScePad.cpp`) | Parity. |
| Haptics, adaptive triggers, pad speaker | none (the pad-speaker audio port is just a port type, `runtime_audio.c` port range 4) | none | Nobody has them. LBP3 calls only the 7 libScePad functions (`docs/input-research.md`). |

## 4. Audio

- **The same downmix finding:** SDL folds 7.1 to stereo with the fronts at about 0.21, roughly −13 dB. They fold it themselves, with the fronts at unity, `k=0.707`, LFE at 0.5, and a hard clamp (`src/runtime_audio.c`, about lines 70–98). They also handle 1, 3, 4, 5, 6 and 7-channel devices.
- **Ours** (ticket 16) adds a peak limiter instead of a hard clamp and leaves multichannel devices to SDL.
- **Optional idea:** their folds for 2.1/4.0/5.0 devices; the user is on stereo.
- **Voice chat:** none in either.

## 5. Saves

- **Import from a PS4:** `scripts/save_import.py` imports a **decrypted** PS4 save into LBPPC's folders. Encrypted `sdimg_` backups aren't supported. It finds `bigfart*` → `LBPxSAVE`, `littlefart*` → `LBPxSAVExLITTLE` and `playfart_<n>_<m>_*` → `LBPxSAVEx<n>x<m>`, and backs up the current save first.
- **Ours** uses the same folder layout (`user\savedata\CUSA00473\<dir>\00000001\…`). A decrypted PS4 export (Apollo or Save Wizard style) can be copied in by hand. A small import script with a backup would make that one click.
- **For the user:** useful if they want their PS4 story progress, though they're already far into the PC playthrough. Low effort; reimplement.
- **Cloud saves:** none in either.

## 6. System dialogs, IME and on-screen keyboard

- Theirs are not described in the docs. They answer the game custom data dialog's invite mode for online (`feature-status.md` NET-02).
- Ours are ImGui (keyboard, message, save-data, common and custom-data dialogs; ticket 12).
- **Nothing to adopt** apart from what online would need: invitation dialogs.

## 7. Debugging and UX

| Feature | LBPPC | Worth adopting? |
|---|---|---|
| **In-game menu** (Insert, or L3+R3) | ImGui overlay: an FPS counter and other overlays, frame-generation toggles, without leaving the game (`gpu/shim/bbport_overlay.cpp`, GPL-2.0-or-later) | **Yes.** We already have an ImGui overlay host (`ui/overlay.*`). An L3+R3 menu showing FPS and frame times, with frame generation, resolution (needs a restart) and the `[perf]` numbers, would replace editing `play.bat` for most switches. About 1 session. |
| **Quit Game in the pause menu** | `scripts/quit_menu.py` edits the pause menu's script (`modaloverlaystatemainmenu.ff`, cwlib RScript format) so the Good Grief! button becomes "Quit Game" (press twice). The game files are untouched: a virtual file overlay in the runtime's file layer serves the edited script and table (`out/file_edits.bin`) | **Yes, nice.** It shows that script edits work through a file overlay, which could be useful beyond Quit. GPL-2.0-or-later, reusable. Medium effort: we need a file-overlay hook in our fs layer. |
| **All 22 languages** | The US edition only allows English and French. A patch at eboot `0x2a0706` (a jump to `0x2a07ab`) accepts the system language, and every edition ships all languages' `.trans` text (`probe.c` `language_install`, about line 1023) | Cheap (one patch plus a language setting), but the user plays in English. Low. |
| **Launcher** (Python/Tk) | Settings (resolution, frame generation, upscaling, controls, online), setup wizard (merges the 1.28 update into the game folder, adds DLC), game-file check against known-good hashes (`scripts/data/lbp3-1.28-us.json.gz`), self-updater, Discord rich presence, CPU compatibility check | Not needed for one user who is comfortable with `play.bat`. The file check is a nice idea if a dump ever gets damaged. Low. |
| **"Make a report"** | Zips the last logs and settings; `scripts/log_privacy.py` strips names, IPs, user folder and passwords as the log is written | Useful only if we share logs publicly. Low. |
| **Log lines** | Every 5 s: frame stats, pacing percentiles, latency from game start to present, pad reads/s, VRAM | We have `[perf]`/`[video]`/`[framegen]`. Their **latency line** and **pacing percentiles** are a good idea for the in-game menu. |
| **Level jump/warp** | **None found** (searched `src`, `scripts`, `tools`, `docs`) | No help for the shader warm-up tour; we'd have to find the map's play-level call ourselves. |
| **Settings file** | `lbppc.ini` plus `input.ini`, written by the launcher | We use env switches in `play.bat`. An ini read at boot would let the in-game menu save settings. |

## 8. Game bugs they fixed that we haven't

- **None of ours overlap:** Thruster freeze, camera snaps, Bunkum white screen, sky colour, the Options/import crash (`0x1bd1a5`) and the import cancel don't appear in their docs or code. Their runtime is a different design (C, its own loader), so our bugs may simply not occur there.
- **Their `spu.prx` NaN guards** (draw-list sort keys, collision bounds, skinning weights at `spu.prx` `0x8f30`/`0x95b0`/`0x47960`, eboot sites `0x16c9475`/`0x16ca145`/`0x17081fc`) only matter with `LBPPC_SIM_DIVIDE`, their native 60/120 experiment, where frames run without a world step. **Not needed** for us at one step per frame.
- **Their animation-clock fix** (`0x3e2970`, the constant `0xe7d3cc`) is for the same experiment. Lane A/B may want it for native 60 research.

## Ranked: what to adopt

1. **Online on community servers** (Refresh + shadNet, LBPPC's approach). The biggest gain for the user: about 10 M community levels and online co-op with friends. Large effort (3–5 sessions); the network code and glue are reusable (GPL-2.0-or-later). The user decides about their account and server (LBPPC's public server or self-hosted), and LBPPC should be asked before pointing a different client at their server.
2. **In-game menu** (L3+R3 / Insert) on our ImGui overlay: FPS and latency, frame generation on/off, and the settings now in `play.bat`. About 1 session. Their overlay is reusable as a reference (GPL-2.0-or-later).
3. **Quit Game in the pause menu** through a virtual file overlay and a script edit. About 1 session; `quit_menu.py` is reusable. The overlay mechanism also opens the door to other script tweaks.
4. **Motion polish:** gyro offset learning and idle zeroing, plus an orientation, and checking that Sackboy's head doesn't drift with the pad lying still. About 0.5 session; reimplement.
5. **PS4 save import:** a small script with a backup, if the user wants their PS4 progress. Low effort.
6. **Mouse as touchpad** for touch Create Mode, and the language patch. Low priority for this user.
