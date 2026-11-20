<!-- DRAFT for the release after v0.1.8 (written 2026-11-20, from main 4048c21). When cutting the release:
     fold this into RELEASE-NOTES.md (its update paragraph, the "What's in it" list, the credits), check the
     items under "Before release" below, then delete this file. Not packaged. -->

**v0.1.9: play online with friends on LBPPC.**

### What's in it (new in v0.1.9)
- **Play with friends on LBPPC's own client** (experimental, off by default). The launcher's **Online** page has
  a new switch, **Play with LBPPC players**; it applies from the next start. With it on, friends who play
  LittleBigPlanet 3 on LBPPC can join your game and you theirs, from the game's own menus, as with friends on this
  port. While it's on, this game computes its world the way LBPPC's does, so friends on this port need the switch
  on too to play with you, and off to play with each other without it (a join between the two settings fails
  instead of drifting apart). Games with LBPPC players use LBPPC's own connection: through the server when your
  connections can't reach each other directly, and without this port's repair of lost messages. Friends only, as
  always. With the switch on, the end of The Legend of Bunkum may need the pause menu's skip. Tested with LBPPC
  0.1.5.
- **Leaving an online game is cleaner.** When you close the game, the other players are told at once instead of
  noticing 15 seconds later, and when the game ends a connection its last messages still get through for a few
  seconds.
- **Your log records how each online session went** (every 5 seconds: who is connected, how the game's messages
  flow, any stalls), so a "it lagged" report can be read. It's only written to your log; nothing is sent anywhere.

### Known issues (new)
- With **Play with LBPPC players** on: three-player games, the European edition of LBPPC and long sessions haven't
  been tried yet.

### Credits (add)
The LBPPC co-op mode speaks LBPPC's connection protocol, re-implemented from LBPPC's source (emurphy04) and
shadPS4's network code (both GPL-2.0-or-later); its notes are in `docs/research/lbppc-coop.md`.

### Before release
- Can a v0.1.8 player and a v0.1.9 player (switch off) still play together? Not tried. If not, the update
  paragraph must say "both of you need v0.1.9", as for v0.1.3.
- Check the switch against the LBPPC release current at the time (`docs/research/lbppc-coop.md`; tools in
  `.scratch/netcode/tools/lbppc`).
