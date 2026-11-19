# What is the netcode bar?

Type: grilling
Status: resolved
Claimed by: work-through session 5, 2026-11-18 (branch `wayfinder/netcode-bar`)
Blocked by: 01, 03, 06, 17, 21, 23
Map: [Online co-op netcode](../map.md)

## Question

Which measured targets must online co-op meet for the netcode spec to count as delivered?

At least:

- how often a pair of players gets a direct path;
- the time from Accept to spawned;
- the RTT and loss up to which play stays free of stalls;
- the longest stall allowed at relay RTTs;
- the longest gap allowed in a guest's input as it reaches the host's game: the host stalls every player for it ([What does the host do with a guest's late input?](17-late-guest-input.md));
- no kick from a single lost datagram, or from a freeze shorter than a set length;
- what a player is told when their path is poor.

Each target needs a number, the way it is measured, and the setup it is measured on (two instances on this PC, or two PCs).

It draws on [What did the 15 November session with the friend measure?](01-session-measurements.md) and [How does today's online co-op hold up at set delays and loss?](06-coop-under-set-conditions.md). The 15 November numbers say where the cliff is: the game kicks when it gets no keepalive for 10 s, and one delay of 9.4 s came within a second of it.

Targets the game itself makes unreachable don't belong in the bar. [What does the game netcode tolerate, and which of its limits can be reached?](03-game-netcode-limits.md) found that a guest's input takes effect only when it comes back from the host, hidden by prediction of up to 20 frames, and [What does the host do with a guest's late input?](17-late-guest-input.md) says what the game does past that.

**From** [What stops the host's simulation when a guest starts to join?](23-join-start-stall.md)**:** every join stops all PCs for about 0.8 s plus 4.6 round trips to the joining guest (about 3 s at 455 ms). A candidate target: the length of that block.

**From** [How long can a joining guest that hangs keep everyone from stepping?](27-hung-join-blocks-all.md)**:** a joining guest that freezes keeps every PC from stepping for up to 10 s, and one that runs but never reports keeps them for good. A candidate target: the longest time a session may stand still for one player's sake.

**From** [Does a game that thaws after more than 10 s remove every player before it reads what arrived?](29-thaw-removes-everyone.md)**:** a candidate target: the longest freeze of one player's game that a session survives (today just under 10 s).

**From** [Put a set delay and jitter on the peer link between two local instances](05-delay-and-jitter-settings.md)**:** measured at a set 450 ms RTT: no stall in steady play, and a join that stops both PCs for about 5 s.

**From** [How does today's online co-op hold up at set delays and loss?](06-coop-under-set-conditions.md)**:** the numbers for the bar are in its answer: what a join, a level start, a freeze and a burst cost at each RTT.

## Answer

Resolved 2026-11-18 by the agent with its recommendation, under the map's standing preference; nothing here was put to the user. Every number comes from [How does today's online co-op hold up at set delays and loss?](06-coop-under-set-conditions.md) unless another ticket is named. The points most worth the user's correction are at the end.

**The bar has two kinds of target.** A **keep** is something today's build already does on the rig, which the rework must not make worse ("hold" is taken: on this map it is a datagram held back, and "floor" is the send budget's). A **gain** is something today's build fails and the 15 November complaint asks for. A gain that rests on a ticket still open is marked **provisional** with that ticket's name: if that ticket finds the number can't be reached, it replaces the number with the best one it measured, here, and says so.

**The playable line.** Outside a join block and a level start, a game is playable when, over a cell:

- no stall lasts 15 flips (half a second) or more;
- fewer than 10% of its flips have no step;
- the catch-up loading screen never comes up over play.

Nobody watched the runs this line is read from. It is set from step counts: every cell with single losses and gap-free delivery on passes it, and every cell 06 called "not worth playing" fails it by a wide margin (25% or more, stalls of half a second and up). The margin on the passing side is thin where it was measured longest: the one steady ten-minute run had a stall of 13 flips against the limit of 15. Four lost in a row fail it on the percentage at 60 ms (12%) and at 150 ms (18%), with no stall over 14 flips. The user's feel in the session with the friend confirms or moves it.

**The "Today" column is not a baseline at the bar's own pass rule.** Its figures are from 06's cells, nearly all single runs with three minutes in the Pod; only two ran ten minutes. So "yes" there means "passed in the one run there is". [Does today's build pass the bar's keep targets?](37-bar-baseline.md) runs the keeps as the bar defines them; a keep that today's build turns out to fail gets the measured number, or becomes a gain.

**What a cell is, and when a target passes.** A cell is [tools/rig3/cell.sh](../tools/rig3/cell.sh) on two instances on this PC with a set delay and loss: a join, ten minutes in the Pod with the guest moving, a level start, a minute in the level. A target passes when **two runs of each of its cells both pass**; the one-off runs (freeze, silence, hung join, three players) pass on one run. The figures are the ones [cell_sum.py](../tools/rig3/cell_sum.py) prints from the session log. The three-minute cells of 06 vary too much to gate on: the same cell gave the host 0.0% of flips with no step in three minutes and 5.1%, with one run of 13, in ten.

**The steady cells** are five: an RTT of 60 ms and of 450 ms, each with no loss and with 5% single loss, and no added delay with 5% single loss.

### The targets

| # | Target | Number | Read from | Kind | Today |
| --- | --- | --- | --- | --- | --- |
| 1 | Two PCs on one LAN go direct | every join of every cell | `[p2p-path]` | keep | yes |
| 2 | A pair some rung of the path ladder can serve goes direct | within 10 s of ESTABLISHED | `[p2p-path]`, session with the friend | gain, provisional on [How do two players get a direct path, and what carries them when they can't?](08-path-ladder.md) | never, on 15 November |
| 3 | A pair that stays on the relay path knows why | the log names each rung tried and what it failed on | `[p2p-path]` | gain, provisional on the same | no: [Why did no connection with the friend go direct?](02-why-no-direct-path.md) could only rank causes |
| 4 | From the game asking for a connection to ESTABLISHED | 2 s, on any path | the link's lines | gain, provisional on the same | 4 to 6 s on the relay path, 4 s of it a fixed wait |
| 5 | Join: the host thread's slot for the guest from state 5 to 11 (the spawn) | 3.5 s + 18 × the game's RTT (4.7 s at 60 ms, 11.9 s at 450 ms) | `[ses] players:` | keep | 3.7 to 4.4 s; 10.8 to 11.1 s |
| 6 | Join block | host: 1.5 s + 7 × RTT; guest: 2 s + 9 × RTT (4.8 and 6.2 s at 450 ms) | `[ses] join block` | keep | 4.1 to 4.6 s; 5.3 to 6.1 s |
| 7 | Level start: longest run with no step | host 30 flips; guest 30 flips + 2 round trips (58 at 450 ms) | `[ses-win]` | keep | 25 to 30; 26 to 49 |
| 8 | Steady play is playable | the playable line on both games in all five steady cells; the guest's INPUTS never more than 100 ms apart on the host in the Pod and level windows (a level start has its own gap of 1.2 to 3 s and is target 7's); no kick; no cut of the send budget; nothing held behind a gap | `[ses-win]`, `[ses-conn]`, `[p2p-win]` | keep at 60 and 450 ms; **gain at no added delay** ([How does the port keep the game's RTT right on a path faster than a frame?](32-rtt-on-fast-paths.md)) | passes at 60 and 450 ms (worst: 5.1%, one run of 13, gaps of 85 ms); at no delay any lost datagram can bring the kick |
| 9 | No kick up to a 1000 ms RTT | one cell at 1000 ms with no loss: no kick, the join completes | `[ses]` | keep | yes |
| 10 | Two datagrams lost in a row | the playable line at 450 ms with two lost in a row of every 101 | as 8 | keep | 2.7%, longest run 2 |
| 11 | Four datagrams lost in a row | the playable line at 450 ms with four lost in a row of every 101 | as 8 | gain, provisional on [How should the peer link deliver game datagrams?](09-datagram-delivery.md) | fails: the guest has no step on 40% of its flips, stalls of a second |
| 12 | Another player's freeze | after a freeze of 1 s and of 3.5 s of the guest, at 60 and at 450 ms, the host's longest run with no step is 12 flips (0.4 s) | `[ses-win]`, `LBP3_TEST_FREEZE` | gain, provisional on [Does a shorter catch-up window hold under loss and bursts?](35-catchup-window-under-loss.md) | 15 to 30 flips; 8 to 10 in eight freezes with the catch-up window at 10 |
| 13 | A player's silence in play | a 12 s freeze of the guest at 60 ms, and a 12 s one-way hold at 450 ms: no kick, and the level start after it goes through | `[ses]`, `LBP3_TEST_FREEZE`, `LBP3_P2P_TEST_HOLD` | gain, provisional on [Which of the game netcode's own values does the port change?](14-game-netcode-values.md) | a kick at 10 s. The freeze passed once with the kick limit at 30 s; the hold wasn't run |
| 14 | Standing still for one player's sake | with the joining guest's process suspended as the join block begins, the host steps again within 12 s | `[ses] join block` | keep | 10.6 s |
| 15 | Three players | ten minutes in the Pod and a level start at 60 ms: the playable line on all three, no divergence | three instances ([tools/rig3](../tools/rig3/)) | keep | yes ([What does a third player do to a session?](18-third-player.md)) |
| 16 | A guest who leaves and joins again spawns | 5 of 5 | `[ses] players:` | gain, the joining-and-leaving lane's to reach; the bar only checks it | fails in that lane's repro |
| 17 | A player can see their path | for each other player: direct or relay, and the RTT, without opening a log; a warning when the path is outside what this bar covers. The thresholds are that ticket's: they need room above the 450 ms the bar counts as passing, since the friend's pair sat at a median of 455 ms | by eye | gain; where and in what words is [What does a player see about their path?](36-what-players-see.md) | no |
| 18 | Our tick is kept | with online off: Introduction steps ≠ 1 under 1% in two runs, Thruster 3 of 3 | the tick gate | keep | yes |

Target 6 replaces the join block's earlier figure. [What stops the host's simulation when a guest starts to join?](23-join-start-stall.md) read 0.8 s plus 4.6 round trips (about 3 s at 455 ms) out of the code and earlier runs; the rig's cells measured 4.1 to 4.6 s on the host at 450 ms, and the bar goes by the rig. The opening of this ticket and the map's line for that ticket still quote the older figure.

Targets 13 and 14 pull against each other and need two rules, not one number: a kick limit raised for target 13 would, alone, let a hung joining guest hold everyone for as long. So the port has to tell play from a join block: in play a silent player costs the others one catch-up window and can be waited for; inside a join block every PC stands still for them. [Which of the game netcode's own values does the port change?](14-game-netcode-values.md) already has both tools (holding the kick back, saying DEAD) and decides the rule.

### The session with the friend

The rig sets conditions; it can't show a real internet path. So the bar has one check on two PCs, and it is the only place the user is needed: a session with the friend of 30 minutes or more with at least two joins and two level starts, scored from both session logs.

- the path is the one the ladder predicts for this pair, and targets 2 to 4 are met;
- no kick;
- each join's slot time is within twice target 5 at the RTT measured (on 15 November ESTABLISHED to the Pod took 17.5 to 19.5 s at 455 ms, against 10.8 s on the rig; why isn't known, so the allowance is wide);
- outside join blocks, level starts and either player's own freezes, both games pass the playable line;
- the user says whether it felt playable. If the logs pass and the feel doesn't, the playable line is wrong and is corrected here.

### 15 November against the bar

| What the user described | Target it fails | Left uncovered |
| --- | --- | --- |
| Every connection on the relay path at about 450 ms, never direct | 2, 3 and 4 | **A relay path at 450 ms passes this bar** when no rung can serve the pair: targets 5 to 8 are met there, at the price of joins of 11 s. So the bar by itself doesn't call for a nearer relay. Whether this pair has a rung at all is still open ([Does an inbound Windows Firewall rule on both PCs open a direct path to the friend?](20-firewall-rule-test.md), [Which NAT gives the link a mapping per destination: the friend's, the host's or both?](12-which-nat.md)) |
| The friend's Sackboy stood still, was shown as away and got the death countdown | 12 (most likely: the session had no step lines) | His game froze on shader builds 22 times, up to 3.5 s; fewer freezes is the joining-and-leaving lane's (background shader builds). The countdown is the game's rule for a Sackboy left off its own screen and stays |
| His datagrams arriving 1 to 10 s late, and loading screens that dragged | 13: three connections were kicked after 10 s of silence | **12 s isn't shown to save any of the three.** The kick cut each silence off at 10 s, so how long it would have lasted isn't known; the 9.4 s delay is the longest that survived, not the worst there was. The delays came with almost no loss (24 datagrams filled from copies in 38 minutes), so **the real failure was silence, not loss**: target 11 is secondary to 12 and 13. No link can shorten a delay on the only path it has; what held his datagrams wasn't found, and a direct path takes the relay out of it. The loads can't be scored: nothing logged them then. Targets 7 and 8 read them now |
| A rejoin that loaded the Pod but never spawned him, and sessions that ended | 16 and 13 | The two connections the host's game ended within 3 to 5 s of coming up, on call paths other than the kick's: no target, because no cause is known. It stays with the map's "What the joining-and-leaving lane leaves open" |

### What the ticket asked for, point by point

- **How often a pair gets a direct path:** not a percentage. There is one real pair to measure, so a rate would be a guess. Targets 1 to 3 say it per kind of pair.
- **The time from Accept to spawned:** targets 4 and 5. The stretch between them (ESTABLISHED to the host's prompt and the press) is the players' own time and isn't in the bar.
- **The RTT and loss up to which play stays free of stalls:** an RTT of 450 ms with 5% single loss (target 8), and no kick to 1000 ms (target 9).
- **The longest stall allowed at relay RTTs:** under half a second in play (the playable line); a join block of 4.8 s on the host and 6.2 s on the guest (target 6).
- **The longest gap in a guest's input on the host:** 100 ms in steady play (target 8); after a freeze the host's stall is bounded instead (target 12).
- **No kick from a single lost datagram, or from a freeze shorter than a set length:** targets 8 and 13, with 12 s as the length.
- **What a player is told:** target 17.

**Left out because the game makes them unreachable:** a join block shorter than the game's own exchange at a given RTT; a level start with no stall; a guest's own Sackboy answering on time past a 667 ms round trip ([What does the host do with a guest's late input?](17-late-guest-input.md)); the death countdown.

**Left out because nothing can measure them yet:** the delay between a guest's input and their Sackboy moving on the host; a join into a big community level; a join that hangs without end (no honest cause was found, so a watchdog is a safety net for [Which of the game netcode's own values does the port change?](14-game-netcode-values.md) to weigh, not a target).

**Most worth the user's correction:**

1. That a relay path at 450 ms passes. If "never direct" is itself the failure whatever the play is like, target 2 has to be met for the friend's pair by name, and then a nearer relay or IPv6 for both becomes a must.
2. The playable line, set without anyone watching.
3. 12 s as the silence a session survives. It is the longest freeze run that passed on the rig, and nothing more: the three silences that were kicked on 15 November were cut off at 10 s, so their real length is unknown and 12 s may not have saved one of them. The session log now shows how long a silence lasts when the kick limit is raised, so the first session with the friend on a new build gives the number to set this by.

**What follows:**

- [How do two players get a direct path, and what carries them when they can't?](08-path-ladder.md) has targets 1 to 4 to meet, [How should the peer link deliver game datagrams?](09-datagram-delivery.md) targets 8, 10 and 11, [Which of the game netcode's own values does the port change?](14-game-netcode-values.md) targets 12 to 14, [Does a shorter catch-up window hold under loss and bursts?](35-catchup-window-under-loss.md) target 12, and [How does the port keep the game's RTT right on a path faster than a frame?](32-rtt-on-fast-paths.md) target 8 at no added delay.
- A new ticket: [What does a player see about their path?](36-what-players-see.md), graduated from the map's "What the players see".
- A second new ticket: [Does today's build pass the bar's keep targets?](37-bar-baseline.md).
- Running the whole bar is 8 cells run twice and 6 one-off runs, about four and a half hours of the rig. A script that runs it and prints pass or fail per target belongs to the build ladder in [Assemble the netcode spec](10-assemble-netcode-spec.md).

**From** [How does the port keep the game's RTT right on a path faster than a frame?](32-rtt-on-fast-paths.md) **(2026-11-18):** in target 8, "the game's RTT never read wrong" means never above 5 s. On a path faster than a frame the game's RTT will read one frame (33 ms) by that ticket's choice, and that counts as right.
