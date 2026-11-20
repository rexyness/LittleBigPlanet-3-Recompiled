# LBP3 PC

A native PC port of LittleBigPlanet 3 (PS4): the game's own code running on Windows on top of a replacement for the PS4's system software.

## The port

**Native port**:
The PS4 game's original x86-64 executable running directly on the PC's processor, with a runtime standing in for the PS4 system libraries. Nothing in the game is translated or rebuilt.
_Avoid_: recomp, recompilation, decompilation, emulator

**Runtime**:
The port's replacement for the PS4 system libraries (`libkernel`, `libSce*`) that the game calls into.
_Avoid_: HLE layer, shim, emulator

**Video core**:
The part of the port that turns the game's PS4 GPU work into Vulkan, taken from shadPS4.
_Avoid_: renderer, GPU emulator

**Frame clock**:
The time the runtime gives the game's simulation threads: held at the last frame's start for a short while after it, so that every frame reads times exactly one frame apart. Other threads get real time.
_Avoid_: virtual clock, fake time

**Base**:
LBP3 Recompiled, the existing native port of this same game that this port is forked from.
_Avoid_: template

**Upstream**:
The base's own public repository, which the fork keeps tracking.

**Port family**:
The other community native ports (bloodborne_pc, gow3_pc, gravityrush_pc, iss_pc, issn_pc). They are a source of fixes and ideas, not the base.
_Avoid_: lineage, upstream

## The game

**Game dump**:
The user's own unpacked copy of the game (US CUSA00473, version 01.28) that the port runs. It is input, never part of the port.
_Avoid_: ROM, ISO, game files (when it means this specific copy)

**Eboot**:
The game's main executable, `eboot.bin`.

**Game-shipped module**:
A loadable module that comes in the game dump rather than with the PS4 system software, such as `libc.prx`, `libSceFios2.prx`, `spu.prx` and the FMOD plugins.
_Avoid_: system module, PRX (alone)

**Imported library**:
A PS4 system library the eboot or a game-shipped module links against and expects the runtime to provide.

## The on-screen keyboard

**On-screen keyboard**:
The PS4-style text-entry panel the port draws over the game window whenever the game asks the player for text, played with the pad, the mouse or the PC keyboard.
_Avoid_: OSK (in prose), IME, keyboard (alone, where the PC keyboard could be meant)

**Prediction bar**:
The row of the on-screen keyboard between the text field and the keys, where suggestions appear.

**Suggestion**:
A word offered in the prediction bar to finish the word the player is typing. The player's text changes only when they pick one; nothing is corrected or completed on its own.
_Avoid_: autocomplete, autocorrect, prediction, candidate

**Word list**:
The common words, each ranked by how common it is, that suggestions come from before the player has taught the keyboard anything.
_Avoid_: dictionary

**Learned word**:
A word the player has used, kept between game runs with the number of times they used it. A word is used when it appears in text the player confirms and was not already in the text the keyboard opened with; cancelled text teaches nothing.
_Avoid_: user dictionary, history

## Online co-op

**Online co-op**:
Two to four players in one game session, each running the port on their own PC, connected over the internet.
_Avoid_: multiplayer (unqualified), netplay

**Host**:
The player whose session the others join.

**Guest**:
A player who has joined a host's session.
_Avoid_: client, joiner

**Online server**:
The community service the port signs in to for accounts, friends, invitations, each player's address and the relay. It is LBPPC's, not this port's.
_Avoid_: PSN, the backend

**Game netcode**:
The game's own network code inside the eboot: the messages it exchanges with the other players and the rules it applies to them.
_Avoid_: netcode (unqualified)

**Peer link**:
The port's connection between two players' PCs: the part of online co-op that this port owns.
_Avoid_: P2P transport, tunnel, bridge

**Game datagram**:
One message the game netcode sends to another player, carried by the peer link unchanged.
_Avoid_: packet (for the game's own message)

**Direct path**:
A peer link route straight between the two players' PCs.

**Relay path**:
A peer link route through the online server's relay, taken when no direct path exists.
_Avoid_: proxy, TURN

**Stall**:
A stretch in which a player's game still shows frames but its simulation doesn't step.
_Avoid_: lag, freeze

**Freeze**:
A stretch in which a player's game runs no frames at all, and so sends nothing.
_Avoid_: stall, hitch

**Flip**:
One frame shown by a player's game, 30 a second. Stalls are counted in flips with no simulation step.

**Burst**:
Several game datagrams in a row lost on the way to one player.
_Avoid_: packet loss (for this case), dropout

**Join block**:
The stall every player's game puts itself into when a guest joins, from the request for the game state until that game has loaded it.
_Avoid_: join freeze, join lag

**Session log**:
The lines the port writes into the game log while online so that a session can be measured afterwards: the peer link's (`[p2p-win]`, `[p2p-path]`) and the game's (`[ses]`, `[ses-win]`, `[ses-conn]`).
_Avoid_: telemetry, stats

**Kick**:
The game netcode ending another player's connection because it accepted nothing from them within its own time limit.
_Avoid_: drop, disconnect (for this case)

**Divergence**:
Two players' simulations no longer matching, which the game detects and reports as "The game diverged".
_Avoid_: desync

## Stickers

**Sticker atlas**:
The game's render target that every placed sticker is drawn into before it shows on a surface: a 16x16 grid of sticker tiles, 255 of them usable. 4096x4096 on the PS4.
_Avoid_: decal atlas, tile texture (the game's own names: RenderDecals, TileTex)

**Sticker tile**:
One square of the sticker atlas, holding the stickers of one patch of one object's surface. 256 px on the PS4, 512 px with sharper stickers.

**Sticker density**:
How many sticker tiles the game gives each world unit of a stickered surface, as a share of its maximum (one tile per 250 units). The game lowers it for everything when the visible stickers need more tiles than the atlas has, and raises it again when they fit.
_Avoid_: sticker resolution, decal quality

**Sharper stickers**:
The port's setting that makes sticker tiles 512 px, so every sticker has twice the PS4's detail per axis at any sticker density. On by default; `LBP3_STICKERS=ps4` or the launcher's checkbox keeps the PS4's atlas.

## Planning

**Port spec**:
The document this effort produces: the decisions needed to start building the port.

**Netcode spec**:
The document the netcode effort produces: the decisions needed to start rebuilding the peer link.

**Netcode bar**:
The measured targets online co-op has to meet for the netcode spec to count as delivered. Each is a keep or a gain.

**Keep**:
A target of the netcode bar that today's online co-op already meets and a rework must not make worse.

**Gain**:
A target of the netcode bar that today's online co-op fails.

**Playable line**:
The netcode bar's test of whether a stretch of play is worth playing, read from a game's stalls.

**Offline finish line**:
The point at which the port counts as working for this effort: story mode playable start to finish, plus Create mode and the Pod, with audio, saves and a controller, plus local co-op for up to four players on one PC, all offline.
_Avoid_: playable (unqualified), done

**Gap**:
A difference between how the base behaves on the game dump and what the offline finish line asks for: something broken, missing or never tried.
_Avoid_: bug (for something never tried), TODO

**Library verdict**:
What the port does with one imported library: *inherit* it from the base, *implement* it, or *stub* it.
