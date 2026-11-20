# Which NAT gives the link a mapping per destination: the friend's, the host's or both?

> **Moved to GitHub:** https://github.com/rexyness/LittleBigPlanet-3-Recompiled/issues/5 (#5) on 2026-11-19. This file is the archive as of the move; work on the issue.

Type: task
Status: moved to #5 (2026-11-19); was: open
Blocked by: 19, 20
Map: [Online co-op netcode](../map.md)

## Question

[Why did no connection with the friend go direct?](02-why-no-direct-path.md) could only rank the causes, because the link asks one outside party (the online server) where it sees us, and logs nothing it sends.

If [Does an inbound Windows Firewall rule on both PCs open a direct path to the friend?](20-firewall-rule-test.md) ends with a direct path, this ticket is answered by it: close it with that result. If the path stays relayed, make the next session decide.

**The build** is the one from [Which log lines does a session need to measure itself?](19-session-log-lines.md), which carries the two things needed here:

- **A second outside party.** From the link's own socket, ask a second server for the socket's mapped address and print it beside the online server's, at start and at each "connected":
  `[p2p] nat: the server sees us at A:p1; <second> sees us at A:p2 (one mapping | a mapping per destination)`.
  The online server has one UDP port, so it can't be the second. Any server that answers a STUN Binding Request would do. That shows the player's public address to that service, so decide which one and whether it is on by default, and say so in the release notes.
- **What the link tried and what arrived:** probes sent and direct packets heard per address, and the first direct packet's source beside the port the server reported for that player.

**Then a session with the friend** (the human's part; hand them a checklist):

- both on that build; join once and play a few minutes;
- both send their reports right after the session;
- the friend reads his router's WAN address and says what kind of line he is on.

Done when the cause from the ranking is named with the new lines as evidence, or the lines show something the ranking didn't have.

**From** [Which log lines does a session need to measure itself?](19-session-log-lines.md)**:** the second item above is built (the `[p2p-path]` lines, runtime branch `netcode/session-log-game`). The first is not: the socket's mapped address as a second outside party sees it was left out of that ticket, because which party to ask is decided here. Build it on that branch once it is decided.
