# Can the host's snapshot go away while a guest is still fetching it?

Type: research
Status: resolved
Claimed by: research agent (work-through session 2, 2026-11-16)
Findings: branch `wayfinder/netcode-third-player`, file `docs/research/netcode-snapshot-replaced-mid-join.md` (`f4dacff`)
Blocked by: none
Map: [Online co-op netcode](../map.md)

## Question

[Can a game take part in a join and never report the game state loaded?](28-never-reports-loaded.md) found that a joining guest fetches the game state from the host as resources, and that a fetch which goes quiet while the connection stays up holds every PC for 600 s or more. The candidate cause closest to real play: the host's in-memory snapshot is replaced or freed while a guest is still fetching it, because a second guest joins, the first rejoins, or someone leaves during the join. A read that fails on the host mid-transfer answers with silence for an in-memory resource (`eboot+0x39b730`).

Establish, from the code:

- what owns the snapshot's resources on the host and when they are released or replaced;
- what the host does with a second request for the game state while a block is still on (not found in [What stops the host's simulation when a guest starts to join?](23-join-start-stall.md) either);
- whether a guest still fetching the first snapshot then gets silence, empty pieces, or a clean failure;
- with three or more players, what a second player asked with kind 2 does, since that request is never timed.

Done when each point is marked known, observed, inferred or not found. The three-player run with overlapping joins in [What does a third player do to a session?](18-third-player.md) would confirm it. Start from the findings file of that ticket. Ghidra is allowed; see the map's Notes.

## Answer

Resolved 2026-11-16 by the research agent, from the code and existing logs; no game was started. Addresses, log lines and the basis of each mark are in the findings file.

**No, by any road found.**

**Who owns the snapshot** (known; the files' state observed). It isn't kept in memory: it is written to the game's temporary cache file (`output/temp_orbis`, 512 MB, 4,096 entries) under the hash of its bytes. A second snapshot adds entries, or writes nothing if the bytes are already there. An entry goes only when the ring evicts it, which steps over any entry with a reader, or on a hash mismatch. A leave frees the host's sending places for that player and deletes nothing. On both test instances the file has a few hundred entries and nothing was ever evicted.

**A second request while a block is on is left unread until the first exchange is over** (known for the rule; an overlapping run is inferred, no log has one). The wait reads only the "unblock" class of session messages. Each exchange has its own key. One side door exists: a request that was put off earlier (`eboot+0x878cd0` true) is handled inside the wait; it only adds entries, and whether it ever runs wasn't found.

**The first guest gets its bytes as before** (inferred from the cache rules): the host holds a reader on the entry for the whole transfer. The bad endings, none in a log:
- the entry gone before the open, or its bytes overwritten: a clean failure in seconds;
- a file read that returns nothing: empty pieces and a hang without limit (known). No cause for such a read in the port was found;
- **a new silence** (known): the host ignores a request for a resource it is already sending to the same asker, and a guest that asks twice waits 600 s. No honest path into that was found.

**Three or more players** (known rules, inferred outcomes, no log). The untimed second player is stamped by its first piece, and is only asked after the first one failed. With four or more players, two leaves in a row can strand a fetch for good.

**Logs** (observed): the `xfer` part of the `[slots]` lines reads another manager (`eboot+0x12cc4e8`), not the resource transfer manager that carries the snapshot (`eboot+0x1315b18`). No log line reads the latter. In two rejoin runs the guest's record in that other manager never drains; that is the rejoin without a spawn, after the unblock.

**Corrections to the findings of [Can a game take part in a join and never report the game state loaded?](28-never-reports-loaded.md):** the snapshot is sent from the cache file, not from memory; the "silence on a failed read" belongs to other stores, not the snapshot, so "the entry freed or replaced while being sent" doesn't hold; a leaving player is cleared from the download places. **And of [What stops the host's simulation when a guest starts to join?](23-join-start-stall.md):** a second request is left unread during a wait, neither nested nor refused.

**Needs a run:** three players with overlapping joins and a leave inside a join's block, with `LBP3_SYNC_PROBE=2` on all three.

**What follows from it:**
- The join's hang without end has no cause found in honest play. The watchdog question in [Which of the game netcode's own values does the port change?](14-game-netcode-values.md) stands as a safety net only.
- The resource transfer manager's sending and download places want log lines: [Which log lines does a session need to measure itself?](19-session-log-lines.md).
- The overlapping joins go into [What does a third player do to a session?](18-third-player.md). Four players are beyond the rig's three instances.
- **For the joining-and-leaving lane:** whether the rejoin without a spawn is the "ignore a request that matches a transfer under way" rule in the other manager. The runs are rt2 and rt4.
