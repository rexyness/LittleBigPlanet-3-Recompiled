# How long can a joining guest that hangs keep everyone from stepping?

Type: research
Status: resolved
Claimed by: research agent (work-through session 2, 2026-11-16)
Findings: branch `wayfinder/netcode-third-player`, file `docs/research/netcode-hung-join-blocks-all.md` (`bd5b966`)
Blocked by: none
Map: [Online co-op netcode](../map.md)

## Question

[What stops the host's simulation when a guest starts to join?](23-join-start-stall.md) found that at every join each PC in the session stops stepping until every PC, the joining guest included, has reported the game state loaded, and that this wait has no time limit of its own (the loop at `eboot+0x34a000`; the host's "unblock" from `0x3a8070` and `0x3a8180`). Only the slot's removal ends it.

Establish:

- what ends the wait when the joining guest never reports: the 10 s kick, a `TIMEOUT_...` key of the network settings file (30 to 60 s), or nothing, and which comes first when the guest's link is alive but its game is frozen (a first-time shader build, as on 15 November) against when its datagrams stop;
- what the players already in see meanwhile, and whether they can leave;
- whether this is the state the rejoin without a spawn stops in ([rejoin_no_spawn.md](../../lbp3-port/coop/rejoin_no_spawn.md): the guest's slot for the host stays at join state 9). The joining-and-leaving lane owns that bug; say only whether the two meet;
- whether the port can end or shorten the wait without touching the simulation: through a timeout key, or by ending the connection itself.

Done when each point is marked known, observed, inferred or not found, with addresses and log lines. Start from the findings file of that ticket. Ghidra is allowed; see the map's Notes. A two-instance run with a frozen guest needs the test accounts' sign-in: [Can the three test accounts sign in and find each other?](24-test-accounts-sign-in.md).

## Answer

Resolved 2026-11-16 by the research agent, from the code and the logs of earlier runs; no game was started. Addresses, log lines and the basis of each mark are in the findings file.

**What ends the wait depends on how the joining guest hangs.**
- **Its datagrams stop: the game's 10 s kick** (known from code; inferred that it ends the wait; no log of it). The wait loop runs the full network update on every pass, the keepalive test included. Removing the player makes the host send the "unblock". The peer link's own limit is 15 s, so it comes second; a BYE ends the wait at once.
- **Its link is alive but its game is frozen: the same 10 s** (inferred). Keepalives come only from the game's own loops; there is no network thread for them, and the link says nothing for a frozen game. A shorter freeze only lengthens the block. On 15 November the longest freeze was 3.5 s, so the real case never reached the kick (observed).
- **Its game runs but never reports "loaded": nothing ends it** (known for the timers, inferred for the outcome). Join state 9 has no timeout on either table, and no `TIMEOUT_...` key applies. Whether a game can get into that state wasn't found.
- The per-slot timeouts are four times the values the `[slots]` lines print (known): 240 s and 360 s for the ones that run with three players, and they are aimed at the players already in.

**What the players already in see** (known and inferred): frames from the wait's own presenter through the loading-screen function, 34.3 ms apart; what the picture looks like wasn't found. No input is used and no menu opens. Closing the window always works, and on `fix/coop-online` it sends BYEs.

**The rejoin without a spawn doesn't stop here** (observed, one run). In the failed rejoin both games take the "unblock" and the host goes on stepping while the guest's table keeps the host at state 9. The two meet only in state 9 having no timeout.

**Can the port end or shorten the wait:**
- **A settings key: no** (known).
- **The kick limit in memory:** possible, but the same float kicks in the middle of play and sets the 9 s message threshold; the wrong lever (inferred).
- **DEAD from the port: yes** (a known chain, never run). Only the host's port ending the connection unblocks everyone. In the first two cases the game already acts at 10 s, so an earlier cut also drops guests who would have thawed. In the third the port would be the only thing that ever acts, and it would need a watchdog on the `+0x45` byte and the host thread's "loaded" entries.

**Corrections to the findings of [What stops the host's simulation when a guest starts to join?](23-join-start-stall.md):** the join timeouts of 30 to 60 s don't end the wait. It ends at 9 to 10 s by the kick, or not at all.

**Needs a run** (and the test accounts' sign-in): a two-instance join with the guest suspended right after the hash arrives, which also shows what is on screen; a DEAD raised by the port during a wait.

**What follows from it:**
- Whether the port should say DEAD for a player whose link answers but whose game has been silent for some seconds, and after how many, goes to [What is the netcode bar?](07-netcode-bar.md) and [Which of the game netcode's own values does the port change?](14-game-netcode-values.md).
- The log lines should name who the host is waiting for (the "loaded" bytes of the host thread's entries): [Which log lines does a session need to measure itself?](19-session-log-lines.md).
- The suspended-guest run goes into [How does today's online co-op hold up at set delays and loss?](06-coop-under-set-conditions.md).
- New tickets: [Can a game take part in a join and never report the game state loaded?](28-never-reports-loaded.md) and [Does a game that thaws after more than 10 s remove every player before it reads what arrived?](29-thaw-removes-everyone.md)

**Corrected later by** [Does a game that thaws after more than 10 s remove every player before it reads what arrived?](29-thaw-removes-everyone.md)**:** a thawing game reads its socket before the keepalive test. It loses everyone because the other PCs removed it and said so.

**Corrected later by** [Can a game take part in a join and never report the game state loaded?](28-never-reports-loaded.md)**:** the failure path restores the old world and leaves; the resource system gives up after 600 s of silence and 8 rounds; there is a second wait before the report.
