# What stops the host's simulation when a guest starts to join?

Type: research
Status: resolved
Claimed by: research agent (work-through session 2, 2026-11-16)
Findings: branch `wayfinder/netcode-third-player`, file `docs/research/netcode-join-start-stall.md` (`15ce21b`)
Blocked by: none
Map: [Online co-op netcode](../map.md)

## Question

[Does a guest that is still loading make the host wait on every frame?](21-loading-guest-wait.md) found that on the host every join begins with one block of flips with no simulation step: 0.8 to 2.4 s in the two-instance runs on this PC, and about 5 s on 15 November through the relay path. It isn't the wait for a guest's input, which comes after it and is short.

Establish:

- what holds the host's steps during that block. A lead that wasn't checked: the window from the guest's request for the game state to the host game's answer, during which the byte at `[eboot+0x130d5e8]+0x46` is 0;
- what its length depends on: the size of the game state, the RTT, the send budget, or the host's own work;
- whether, with three or more players, that window also holds back a guest who has just loaded (it would sit at state 10);
- whether the port can shorten it.

Done when each point is marked known, observed, inferred or not found, with addresses and log lines. The findings files of that ticket and of [What does the host do with a guest's late input?](17-late-guest-input.md) have the tables and the method. Ghidra is allowed; see the map's Notes.

## Answer

Resolved 2026-11-16 by the research agent, from the code and the logs of earlier runs; no game was started. Addresses, log lines and the basis of each mark are in the findings file.

**What holds the steps: the game's own exchange of the game state, on every PC in the session** (known from code; inferred that it is what held the logged blocks).
- The gate is the byte at `[eboot+0x130d5e8]+0x45`, not `+0x46` as the question had it. While it is 0 a game takes no step and sends no INPUTS.
- The host forwards the joining guest's request for the game state to every player already in. Each game saves a snapshot and clears the byte. The host's game sends the state's hash; each game rebuilds its world, reports "loaded" and waits inside that handler for the host's "unblock". The host sends it only when every PC has reported, the joining guest included. A second, smaller round for data labels follows, and then the byte is set again.
- **The wait has no time limit.** Only the removal of the slot ends it (the 10 s kick, or a join timeout of 30 to 60 s).
- The frames shown meanwhile come from the wait loop's own presenter, 35 ms apart, which is what the logs show.

**What its length depends on: the RTT to the joining guest** (observed).
- On a direct path (RTT 0 to 5 ms) six runs gave 24 to 28 flips with no step. Through the relay at about 354 ms, two runs gave 71 and 72.
- That is about 0.8 s plus roughly 4.6 round trips (inferred). At the 455 ms of 15 November it comes to about 3 s; the earlier "about 5 s" included catch-up.
- The host's own work is small: 65 ms from request to hash.
- **Not found:** the part of the game state's size and of the send budget (every run joined in the Pod), and what the 0.8 s on a direct path is made of. The host's datagrams to a joining guest leave 100 to 133 ms apart around the block; who sets that rhythm wasn't found.

**Three or more players** (known from code; no run has three).
- Every PC already in the game stops for the whole block, and the slowest PC decides its length.
- The `+0x46` window is the host game's turnaround, about two frames, inside the block. A guest held at state 10 adds no standstill of its own.

**Can the port shorten it: only through the path** (inferred). Each millisecond of RTT costs about five in the block, so a direct path cuts it from about 3 s to about 0.8 s, and the link shouldn't hold datagrams back in this phase. No timer of the port is involved, so "keep our tick" is untouched. Skipping the wait would be rewriting the simulation's netcode, which is out of scope.

**Corrections to the findings of [Does a guest that is still loading make the host wait on every frame?](21-loading-guest-wait.md):**
- the host's main loop wasn't running during the block; the frames came from the wait loop's presenter;
- the two long runs were long because they went through the relay, not because of the state 5 seen on the way;
- the hash handler replaces the world on every PC at every join, which is also what resets the lag sample count.

**What follows from it:**
- The block is a cost of the relay path, about five times the RTT at every join. It is one more thing the path ladder buys: [How do two players get a direct path, and what carries them when they can't?](08-path-ladder.md). The netcode bar can hold a target for it: [What is the netcode bar?](07-netcode-bar.md).
- Gap-free delivery holds datagrams during this exchange, which lengthens it: a point for [How should the peer link deliver game datagrams?](09-datagram-delivery.md).
- The log lines should mark the `+0x45` byte going to 0 and back, since step counts can't tell this block from a wait for input: [Which log lines does a session need to measure itself?](19-session-log-lines.md).
- The three-player run should show the block on the player already in: [What does a third player do to a session?](18-third-player.md).
- A new ticket: [How long can a joining guest that hangs keep everyone from stepping?](27-hung-join-blocks-all.md)
**Marked inferred:** that datagrams held back behind a gap lengthen the join exchange is a reading of the research, which only inferred that the link should not hold datagrams in that phase.

**Corrected later by** [How long can a joining guest that hangs keep everyone from stepping?](27-hung-join-blocks-all.md)**:** the join timeouts of 30 to 60 s don't end the wait; the 10 s kick does, or nothing.

**Corrected later by** [Can a game take part in a join and never report the game state loaded?](28-never-reports-loaded.md)**:** every PC runs the 10 s loop and only the send is the host's; the 10 s doesn't bound the handler nested in it.

**Measured later in** [Put a set delay and jitter on the peer link between two local instances](05-delay-and-jitter-settings.md)**:** at a set 450 ms RTT the block was 149 flips on the host (about 5 s), not the 3 s the formula gives.
