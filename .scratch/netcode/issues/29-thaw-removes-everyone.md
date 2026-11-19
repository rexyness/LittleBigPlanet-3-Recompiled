# Does a game that thaws after more than 10 s remove every player before it reads what arrived?

Type: research
Status: resolved
Claimed by: research agent (work-through session 2, 2026-11-16)
Findings: branch `wayfinder/netcode-third-player`, file `docs/research/netcode-thaw-removes-everyone.md` (`d5bdd80`)
Blocked by: none
Map: [Online co-op netcode](../map.md)

## Question

[How long can a joining guest that hangs keep everyone from stepping?](27-hung-join-blocks-all.md) inferred from the call order in `eboot+0x3604a0` that a game coming back from a freeze runs the 10 s keepalive test on every connection before it reads its sockets. If so, a freeze of more than 10 s on one PC ends the session from that PC's side whatever the other PCs did, and no change to the link on the other side can save it.

Establish:

- the order of the keepalive test and the socket read in that update, checked in assembly;
- what the test measures against: the time of the last keepalive taken off the socket, or of the last one received by the port;
- whether the port could spare the session: by the bridge stamping what arrived during the freeze, or by a hook that lets the read run first. Mind "keep our tick" in the map's Notes.

Done when each point is marked known, observed, inferred or not found. A run that suspends one instance for 12 s would confirm it; that needs the test accounts' sign-in ([Can the three test accounts sign in and find each other?](24-test-accounts-sign-in.md)). Ghidra is allowed; see the map's Notes.

## Answer

Resolved 2026-11-16 by the research agent, from the code and existing logs; no game was started. Addresses, log lines and the basis of each mark are in the findings file.

**No: the game reads its socket before the test** (known, checked in assembly). The function that takes a keepalive reads the socket itself first, and one read empties it for every connection. The order is the same for all four callers of the network update. The inference in [How long can a joining guest that hangs keep everyone from stepping?](27-hung-join-blocks-all.md) read the call order one level too high.

**What the test measures** (known): the game's own clock at the moment it takes a keepalive out of the ordered stream, one control message per pass. Not the datagram's arrival time, and nothing the port stamps. That clock runs through a freeze. A thawing game passes its own test on the first pass (inferred): the bridge keeps the first 256 datagrams per socket, so the oldest keepalives are there.

**The session is lost anyway, from the other side** (each step from the code, never run). The PCs that didn't freeze remove the frozen one 9 to 10 s in, by the same test, and their links send a BYE. The thawed game hears DEAD at the top of its frame and removes them. So sparing the thawing side can't matter.

**What the port could do** (inferred): hold the kick back on the PCs that did not freeze. Either raise the 10.0 at `eboot+0x1061c4c` on every PC, or have each port write a fresh time into a connection's `+0x160` for a player whose link answers while their game is silent. Neither touches "keep our tick"; both act only with online on. The port's clock is the wrong lever: the simulation paces from it.

**Logs:** no freeze of 5 s or more in any log (33,315 frame lines in 66 logs; the longest is 3.5 s), so nothing was observed.

**The ticket's plain 12 s suspend can't confirm any of this**, since the other PC's kick ends the session either way. The run that can: raise the limit on the PC that doesn't freeze and suspend only the main thread of the other for 12 s. Suspending the whole process stops the link's thread too.

**Corrections to earlier findings:**
- [How long can a joining guest that hangs keep everyone from stepping?](27-hung-join-blocks-all.md): a thawing game reads first; it loses everyone because the others removed it and said so. Raising the limit helps only on the PCs that didn't freeze.
- [What does the game netcode tolerate, and which of its limits can be reached?](03-game-netcode-limits.md): a connection's `+0x160` is the time the keepalive was taken out of the ordered stream, not the time it was read off the socket.

**What follows from it:**
- One decision with two directions: for a player whose link answers while their game is silent, does the port end the connection sooner (DEAD) or hold the others' kick back, and for how long? It goes to [Which of the game netcode's own values does the port change?](14-game-netcode-values.md), with the target in [What is the netcode bar?](07-netcode-bar.md).
- Whether a session is worth keeping after a freeze of more than 10 s (the game comes back 300 or more steps behind) needs the run above: it goes into [How does today's online co-op hold up at set delays and loss?](06-coop-under-set-conditions.md). Whether the bridge should keep more than 256 remote datagrams follows from that.
