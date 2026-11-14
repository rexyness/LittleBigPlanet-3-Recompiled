# Credits

- **LittleBigPlanet 3** is made by Sumo Digital and Media Molecule and published by Sony Interactive
  Entertainment. It belongs to them; this project contains none of the game.
- **LittleBigPlanet 3 Recompiled** by PurpleGitty: the original native PC runtime this port is built on
  (GPL-2.0-or-later).
- **This port's work** (the fork at github.com/rexyness/LittleBigPlanet-3-Recompiled): pacing and camera fixes,
  Create mode (on-screen keyboard, dialogs, photos), local co-op, performance work, frame generation, 1440p,
  the audio fix, DLC and LBP1/LBP2 unlocks, the launcher, online support (GPL-3.0-or-later since v0.1.3).
- **shadPS4** (shadps4-emu and contributors): the PS4 graphics core and shader recompiler (GPL-2.0-or-later).
- **LBPPC** by emurphy04 (github.com/emurphy04/LBPPC): the other LittleBigPlanet 3 PC port. Several pieces
  here come from it, with credit in the code: the parallel shader preload and saved driver cache, the
  online request signing and server answers, the shadNet client adaptations and the game server's sign-in
  codes (LBPPC 0.1.3), the pause-menu Quit Game script,
  the bloom and language patch addresses, the resolution list and the ultrawide view's patch addresses
  (camera aspect, 2D canvas and layout centres, the online camera split), and the community-download check. The LBPPC team also runs the
  play.lbppc.com servers and allowed this port to use them.
- **Refresh** (LittleBigRefresh) and **shadNet** (shadps4-emu): the community game server and PSN stand-in
  that online play connects to.
- Libraries: SDL3, Dear ImGui, abseil, protobuf, LibreSSL, glslang, Vulkan headers, Vulkan Memory Allocator,
  fmt, sirit, Zydis, xbyak, xxHash, miniz, robin-map, magic_enum, half, Boost headers, Tracy, stb, nlohmann/json
  (licences in `licenses\`).
