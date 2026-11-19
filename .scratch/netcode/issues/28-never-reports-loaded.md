# Can a game take part in a join and never report the game state loaded?

Type: research
Status: resolved
Claimed by: research agent (work-through session 2, 2026-11-16)
Findings: branch `wayfinder/netcode-third-player`, file `docs/research/netcode-never-reports-loaded.md` (`d7aeddf`)
Blocked by: none
Map: [Online co-op netcode](../map.md)

## Question

[How long can a joining guest that hangs keep everyone from stepping?](27-hung-join-blocks-all.md) found that when a game keeps running and sending but never reports "loaded", nothing ends the wait in which no PC steps: join state 9 has no timeout on either table. Whether a game can get there wasn't found.

Establish, from the code:

- whether the fetch of the snapshot's resources can stall without failing (the loop at `eboot+0x236550` has no limit);
- what the failure path through `0x34caf0` does to the wait;
- whether the second pass through the wait, for the data labels, can hang the same way;
- for each way found, what would have to go wrong on the path or the server for it to happen.

Done when each point is marked known, observed, inferred or not found. If a game can get there, say what the longest honest wait is, since a watchdog in the port would need that as its limit. Start from the findings file of that ticket. Ghidra is allowed; see the map's Notes.

## Answer

Resolved 2026-11-16 by the research agent, from the code and existing logs; no game was started. Addresses and the basis of each mark are in the findings file.

**Yes, but only when a resource it waits for goes quiet while the connection stays up.** A fetch that fails doesn't hang the session.

**The fetch can stall** (known).
- The wait on each resource of the game state has no time limit of its own.
- In a session a missing resource is asked of the other players first, over the game's own streams; HTTP to the game server comes only after they fail.
- The only timer is 600 s without an answer from the asked player. It restarts on every piece, an empty one included, and a timed-out request is tried up to 8 times.
- So a silent host holds every PC for at least 600 s, by the code up to about 80 minutes (inferred).
- A second wait with no limit comes before the report: until no load of priority under 3 is in progress on that PC (known).
- **Not found:** a cause shown to happen. Candidates: the host's snapshot replaced by a second join while it is being sent; a request dropped silently when no connection object is found.

**A failed fetch ends cleanly** (known up to the leave, inferred after). The game puts its previous world back, shows a notice and goes back to its own Pod, which ends the connection and unblocks the rest, within seconds. If the restored world has unsaved changes it asks "leave?" first, and everyone waits on that player.

**The pass for data labels** hangs only on a stalled fetch, the same way (known). No log has ever run it: in the Pod the message carries no labels.

**The port's layers** (inferred): the peer link and the socket bridge can't cause it, since they don't read datagrams. Its HTTP answers return within 10 to 30 s, so they can lengthen a block but not stall one. Keepalives and resource messages share a stream (known).

**The longest honest wait has no fixed number** (inferred): about 0.8 s plus five round trips plus the state's size over the send allowance, which is 12,288 bytes a second at the start and 6,144 at the floor with two players (4,096 with three or four). 1 MB at the floor is 163 s. The size of a real level's state wasn't found. So a watchdog in the port has to go by progress: no resource byte sent to the waiting guest for some seconds while no PC steps. The fields to read are in the findings file.

**Corrections to earlier findings:**
- [How long can a joining guest that hangs keep everyone from stepping?](27-hung-join-blocks-all.md): the failure path restores the old world and leaves, it doesn't build an empty one; the resource system does give up, after 600 s of silence and 8 rounds; there is a second wait before the report.
- [What stops the host's simulation when a guest starts to join?](23-join-start-stall.md): every PC runs the 10 s loop, only the send is the host's, and the 10 s doesn't bound the handler nested in it.

**Needs a run:** a join into a real level with the session log lines on (bytes fetched, length of the block); three players with overlapping joins and a leave during a join.

**What follows from it:**
- A join into a real level costs the state's size over the send allowance on top of the round trips. Measuring it goes into [How does today's online co-op hold up at set delays and loss?](06-coop-under-set-conditions.md); the bytes sent to a joining guest go into [Which log lines does a session need to measure itself?](19-session-log-lines.md).
- Whether the port gets a watchdog that goes by progress is part of the one rule in [Which of the game netcode's own values does the port change?](14-game-netcode-values.md).
- Overlapping joins go into [What does a third player do to a session?](18-third-player.md).
- A new ticket: [Can the host's snapshot go away while a guest is still fetching it?](30-snapshot-replaced-mid-join.md)
- **For the user and the joining-and-leaving lane, outside this map's route:** resources a game gets from another player bypass the port's download filter (`CheckDownload`), which applies to HTTP only. It is in the map's Not yet specified.

**Corrected later by** [Can the host's snapshot go away while a guest is still fetching it?](30-snapshot-replaced-mid-join.md)**:** the snapshot is sent from the game's cache file and can't be freed or replaced under a guest fetching it.
