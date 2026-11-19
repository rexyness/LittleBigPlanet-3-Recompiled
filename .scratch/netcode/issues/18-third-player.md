# What does a third player do to a session?

Type: task
Status: resolved
Claimed by: work-through session 2, 2026-11-16
Blocked by: 24
Map: [Online co-op netcode](../map.md)

## Question

Online co-op with three players has never been run on this port. From the code, [What does the game netcode tolerate, and which of its limits can be reached?](03-game-netcode-limits.md) says: connections are a full mesh (every game connects to every other player in the room), simulation data is a star through the host, and the 10 s keepalive test runs on every connection. So a failed guest-to-guest path should make those two guests remove each other even while both still reach the host.

Run three instances on this PC (the three test accounts, under the game lock):

- all three in one host's Pod for ten minutes, then a level start;
- the same with the path between the two guests cut. No link change is needed for that: see below.

Record who connects to whom, what a guest sends another guest, and what the session does once the guest-to-guest path is gone.

**Also record** how long the player already in stops stepping when the third joins: [What stops the host's simulation when a guest starts to join?](23-join-start-stall.md) reads from the code that every PC stops until all have loaded the game state. Run all three with `LBP3_TRACE_SIM=1` and `LBP3_SYNC_PROBE=2`.

Done when the mesh is confirmed or corrected, and the behaviour with a failed pair is described. The path ladder then has to hold for every pair in a session, not only guest and host.

**Tried on 2026-11-16, not run.** The three instances started and came up signed out, so nothing was measured. What that attempt left:

- **The rig:** [tools/rig3](../tools/rig3/) (`launch3.sh`, `lib.sh`). It takes the game lock, starts A, B and C from `D:\Recomps\lbp3-wt\coop` on a private copy of the `fix/coop-online` build, and stops if an instance isn't signed in. That build's sync probe prints the room, the connection list and the signaling table once a second (`[slots]` lines), and `LBP3_MSG_PROBE=1` logs each message a game takes off a player's queue. That covers "who connects to whom" and "what a guest sends another guest" with no new build.
- **Sign-in.** The instances have no `user\config\online_account.txt`; the earlier two-instance runs had the password in their session's environment. [Can the three test accounts sign in and find each other?](24-test-accounts-sign-in.md) settles it.
- **Kistopawa3 had no friends** on 2026-11-13 (`coop\C\stderr_refuse.txt`), so it can't pick the host in the Pod computer's friend list. Same ticket.
- **The cut pair may come free (see the corrections below).** The link refuses a connection from anyone who isn't a shadNet friend (join protection, `MayConnect` in `np_shadnet.cpp`; the other side gets "no answer in 30 s"). With Kistopawa3 a friend of the host only, a plain run is the cut-pair case, and `LBP3_JOIN_ALLOW=Kistopawa,Kistopawa2,Kistopawa3` on all three is the full mesh.
- **Use `LBP3_P2P_PATH=lan`.** Three pairs on the relay path would put three sessions' traffic on LBPPC's server.

**Inferred from that, to check in the run:** in real play two guests who are each a friend of the host, but not of each other, have their pair refused by the port's own rule. If the mesh is as [What does the game netcode tolerate, and which of its limits can be reached?](03-game-netcode-limits.md) reads it, they remove each other after 10 s. [Should join protection let two guests of one host connect?](26-join-protection-guests.md) decides what to do about it.

**Corrections, 2026-11-16 (end of the same session):**
- **The cut pair is not established.** The friend check applies only to a player the link does not know yet. `Link::Activate` adds a player the game itself asks for, with no check, and in two-player logs both sides print `connecting to`. If each guest game activates toward the other, neither link refuses. So the cut pair may still need a link test setting, built on the `fix/coop-online` lane head once it has landed. The first run shows which.
- **That launch overwrote logs.** It used the tag `m1`, and `coop\A` and `coop\B` already held `stderr_m1.txt` and `stdout_m1.txt` from 13 November. Those are lost; the files there now are the signed-out launch of 2026-11-16 20:31. No note or findings file cites run m1 (m2 is cited and is intact).
- **The committed rig is not the one that ran.** It gained a run folder, the exe copy step and the sign-in stop, and has only passed a syntax check. Pick a tag with no log yet.

**From** [Can a game take part in a join and never report the game state loaded?](28-never-reports-loaded.md)**:** include overlapping joins (the third starts joining while the second is still loading) and a leave during a join. See [Can the host's snapshot go away while a guest is still fetching it?](30-snapshot-replaced-mid-join.md).

**From** [Can the host's snapshot go away while a guest is still fetching it?](30-snapshot-replaced-mid-join.md)**:** with overlapping joins, expect the host's game to take the second request only after its first "unblock", then a second hash and unblock; the second block's length says whether the PCs already in kept their world. Use `LBP3_SYNC_PROBE=2` on all three.

## Answer

Resolved 2026-11-16 with two three-instance runs on this PC (Kistopawa hosting, Kistopawa2 and Kistopawa3 joining through the Pod computer's friend entry), on the `fix/coop-online` build of 18:59 that day, `LBP3_P2P_PATH=lan`, the sync probe at 2, the message probe and the sim trace on. Logs: `D:\Recomps\lbp3-wt\coop\A|B|C\stderr_tp1.txt` and `stderr_tp2.txt`. Driven with [tools/rig3](../tools/rig3/) (`launch3.sh`, `join.sh`).

**The mesh is confirmed** (observed, run tp2). With all three in, each game has three connection objects and two signaling entries, and each link is connected to both other players. Each guest's game asks to connect to the other guest as soon as it joins (`connecting to` on both sides). The room list is the same on all three.

**Simulation data is a star through the host** (inferred from the counts). In steady play the host sends about 62 datagrams a second, each guest about 32: 30 to the host and about 2 to the other guest, which is the keepalive rate.

**Three players hold** (observed, tp2). Ten minutes in the host's Pod with all three moving: no connection ended, no gap of two flips with no step on any PC (no-step flips 0.6 to 2.8%, never two in a row), the world hashes agree to the end. Then the host started the Introduction: one block with no step on each PC (33, 26 and 27 flips, about a second), and all three were in the level.

**The join's block reaches the player already in** (observed, tp2). When the third player joined, the second stopped stepping for about 54 flips (1.8 s) and the host for about 24, as [What stops the host's simulation when a guest starts to join?](23-join-start-stall.md) read from the code.

**A pair that can't connect keeps the third player out for good** (observed, run tp1, where the two guests weren't friends of each other):
- Both guests' games asked to connect to each other. Each link asked the online server where the other player is and got "the server doesn't know where that player is" (error 26). The server gives a player's address to friends only.
- With no address to try, both links gave up after 30 s (`no answer in 30 s`, signaling error 0x8055270f).
- The third player never got into the session. Its game stayed in its own Pod with no message, still connected to the host and exchanging about two datagrams a second with it, for as long as the run went on (two minutes). Its room list held only itself. The host and the second player played on and showed nothing.
- So the prediction is corrected: the two guests don't remove each other after 10 s, because the connection never exists for the keepalive test to run on. The join just doesn't finish.
- `LBP3_JOIN_ALLOW` was set on all three, so the port's own friend check played no part. The earlier note here that "the cut pair comes free" from that check was wrong in its cause and right in its effect: an unfriended pair is cut, by the server's lookup.

**What it means for real play:** a third friend can join a session only if they are also a friend of every guest already in it. Otherwise they sit in their Pod and nobody is told why.

**Not run:**
- a guest-to-guest path that dies in the middle of play (it needs a link test setting): [What does a session do when the path between two guests dies in play?](31-guest-pair-dies-in-play.md);
- overlapping joins and a leave during a join; they go to the same ticket;
- four players (the rig has three instances).

**A slip in the run:** the host's Popit was opened by mistake and closed again without using anything. The test accounts' saves weren't otherwise touched.
