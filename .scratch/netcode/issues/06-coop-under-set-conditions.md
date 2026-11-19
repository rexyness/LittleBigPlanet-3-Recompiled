# How does today's online co-op hold up at set delays and loss?

Type: task
Status: resolved
Claimed by: work-through session 4 (second ticket, the user said to carry on), 2026-11-17
Blocked by: 01, 05, 19
Map: [Online co-op netcode](../map.md)

## Question

Run two instances on this PC (the test accounts, under the game lock) through a join, ten minutes in the Pod and a level start, across a matrix of conditions drawn from the numbers in [What did the 15 November session with the friend measure?](01-session-measurements.md), set with the switches from [Put a set delay and jitter on the peer link between two local instances](05-delay-and-jitter-settings.md).

At least:

- RTT: direct-like (30 ms or less), 150 ms and 450 ms;
- loss at each: 0%, 1% and 5%;
- 450 ms with a 10 s one-way hold during the join;
- a freeze of the guest's game of 1 s and of 3.5 s (the longest on 15 November), at the default `FramesBeforeAgressiveCatchup` and at a lower one.

The measures come from the lines added by [Which log lines does a session need to measure itself?](19-session-log-lines.md).

Record for each cell:

- the time to join and to spawn;
- the game's send budget on both sides through the join and the level start, and any cut ([What makes the game netcode cut its send budget, and does the peer link trigger it?](15-send-budget-cuts.md));
- whether the guest's game sends INPUTS while it loads the level, and the longest gap in them ([Does a guest that is still loading make the host wait on every frame?](21-loading-guest-wait.md) left this open; the run needs `LBP3_TRACE_SIM`);
- stalls: how many, and the longest;
- the gaps in the guest's input as the host's game got it, and how often the host waited ([What does the host do with a guest's late input?](17-late-guest-input.md));
- kicks, and the longest time the game got nothing from the other player (the kick comes at 10 s);
- how long DATA3 held datagrams behind a gap;
- the delay between the guest's input and their Sackboy moving on the host, if the logs can show it.

Done when the table is filled and it names the first condition at which co-op stops being playable, with what breaks first.

**From** [How long can a joining guest that hangs keep everyone from stepping?](27-hung-join-blocks-all.md)**:** add a join with the guest's process suspended right after the game state's hash arrives, with `LBP3_TRACE_SIM=1` and `LBP3_SYNC_PROBE=2`. Expected on the host: no steps for 9 to 10 s, then the kick, then steps. Record what is on screen meanwhile.

**From** [Does a game that thaws after more than 10 s remove every player before it reads what arrived?](29-thaw-removes-everyone.md)**:** add a run with the kick limit raised on one instance and only the main thread of the other suspended for 12 s, with `LBP3_TRACE_SIM=1`. Record whether the connection survives and how the session recovers (catch-up, resends, the host's wait for input).

**From** [Can a game take part in a join and never report the game state loaded?](28-never-reports-loaded.md)**:** run one join into a real level, not only the Pod: the block then also costs the state's size over the send allowance (1 MB at the floor is 163 s). Record bytes fetched and the block's length.

**From** [Put a set delay and jitter on the peer link between two local instances](05-delay-and-jitter-settings.md)**:** a first point of the matrix is in: at a steady 450 ms RTT with no loss, steady play in the Pod had no flip without a step in 56 s, and the join's block was about 5 s (149 flips). Use the `netcode/session-log` build (`D:\Recomps\lbp3-wt\netdelay\build`, `EXE_SRC` of the rig), whose `[p2p-win]` lines give the link's side of every run.

**From** [Which lost datagram ends a session?](13-fatal-datagram.md)**:** at no added delay, two instances on this PC make the game's RTT wrong for seconds at a time, and it then resends nothing. Until [How does the port keep the game's RTT right on a path faster than a frame?](32-rtt-on-fast-paths.md) is built, make 30 ms of delay each way the fastest point (`LBP3_P2P_TEST_DELAY=30` on both; no bad sample in four minutes there), or the loss points measure that fault and not the game. `LBP3_RTT_CLAMP=1` keeps a session alive at no delay but makes the game's RTT read about 5 ms and its send budget fall to the floor, so it is no stand-in. **Add to the matrix, with gap-free delivery off:** drops during a join, drops during a level load, and drops with the send budget at the floor. The fatal-datagram runs covered steady play in the Pod only. Runtime branch `netcode/fatal-datagram`. `tools/rig3/launch2.sh` starts two instances with settings for one instance only, and `LBP3_CONN_PROBE=1` gives the game's own RTT, unacknowledged and held counts and send budget once a second.

**From** [Which log lines does a session need to measure itself?](19-session-log-lines.md)**:** the lines are in (runtime branch `netcode/session-log-game`; the build is in `D:\Recomps\lbp3-wt\netdelay\build`). What each measure of this ticket reads:

- stalls: `[ses-win]`, "flips with no step (longest run N)". On a guest the step count itself runs a little over the flips (prediction moves the counter), so use the no-step figures;
- the host's waits and the gaps in a guest's input: `[ses-win]` on the host, "waits over 50 ms" and "INPUTS [n] ... longest gap";
- the join's block and who is awaited: `[ses] ... join block` and `game state exchange`;
- the send budget and its cuts, the game's RTT beside the link's: `[ses-conn]`;
- the spawn: `[ses] ... players:` (the guest's state 11 on its own PC; on the host the host thread's slot reaching 11);
- the longest time the game got nothing, and DATA3's holds: `[p2p-win]`;
- a connection's end: `[ses] ... the game ends its connection`.

[tools/rig3/sl_run.sh](../tools/rig3/sl_run.sh) is a starting point for a cell: `sl_run.sh <tag> LBP3_P2P_TEST_DELAY=<ms each way>`. Three points are in that ticket's answer already (no delay, 30 ms and 225 ms each way, no loss, about three minutes each).

In the join into a real level, watch for resource bytes inside the block: the `join block` lines carry the transfer manager's places only at the block's start, every 2 s and at its end, and no Pod join showed any. If a level's block is long and still shows none, the game state does not travel through those places and the line needs another source.

## Answer

Resolved 2026-11-18, from 41 two-instance runs on this PC on the evening of 17 November. All of it is observed in the session log lines of both games unless marked otherwise; each run's summary is in [runs/06](../runs/06/) and its logs in `D:\Recomps\lbp3-wt\coop\A\stderr_<tag>.txt` and `B\stderr_<tag>.txt`.

**The first condition at which co-op stops being playable is loss in bursts on a slow path, and what breaks first is the guest's stepping.** At a 450 ms RTT with gap-free delivery on, two datagrams lost in a row out of every 101 cost nothing to speak of (the guest without a step on 2.7% of its frames, never more than two in a row), and three in a row break it: the guest has no simulation step on 33% of its frames, in stalls of over half a second, with the catch-up loading screen coming up over the game every few seconds. At four in a row it is 40%, with stalls of a second. The host loses 18 to 23% of its steps. Nothing is kicked and nothing diverges: the session stays up and is not worth playing. **Gap-free delivery moves the line by exactly its two copies:** without it, two in a row already break it (25% on the guest, stalls of nearly a second), and from three up it makes no real difference (33% with it and 29% without at three, 40% and 44% at four). The cost of the same bursts grows with the RTT: the guest loses 12% of its steps at 60 ms (no stall over a third of a second), 18% at 150 ms (none over half a second) and 40% at 450 ms.

**What does not break it:**

- **Steady delay, up to a 1000 ms RTT.** No stall in play and no kick at 60, 150, 450, 700 or 1000 ms. Delay costs waiting, not stepping: the join's block and the guest's standstill at a level start grow with the RTT (table). Past 667 ms the guest's own Sackboy must answer late, by the prediction limit of [What does the host do with a guest's late input?](17-late-guest-input.md); the logs can't show that.
- **Single lost datagrams, at 1% and 5%, at every RTT tried, with gap-free delivery on or off** (a single loss is one that is not next to another: with it off, two in a row are already a burst that breaks play at 450 ms). With it on, the link fills every loss from the next datagram's copy and holds nothing back. With it off, the game repairs the loss itself: one stall of about a second in twelve minutes of play (n450_l1), and joins and level starts go through. So the game's own repair is enough for single losses, also during a join and a level load, which [Which lost datagram ends a session?](13-fatal-datagram.md) had left untested.

**What a cell is.** Kistopawa hosts, Kistopawa2 joins, LAN path, the same delay each way on both games (`LBP3_P2P_TEST_DELAY`), loss on what each game receives (`LBP3_P2P_TEST_LOSS=N` drops one in N, `N,k` drops k in a row of every N; 101 and 19 for 1% and 5%, so that the drops fall on every point of the guest's 30 datagrams a second). The guest joins; three minutes in the Pod (two in the burst and far cells) with the guest moving; the host starts the Introduction; a minute in the level with both moving ([tools/rig3/cell.sh](../tools/rig3/cell.sh), [cell_sum.py](../tools/rig3/cell_sum.py)). The ticket asked for ten minutes in the Pod: the two long runs at the end of the table have them. The fastest point is a 60 ms RTT, not 30 ms or less: below a frame the port's clock fault spoils the game's RTT ([How does the port keep the game's RTT right on a path faster than a frame?](32-rtt-on-fast-paths.md)).

Every cell was checked before it counted: the guest got in, the level start happened, both games kept 150 flips per 5 s outside the freezes, and the loss setting dropped datagrams.

| Cell | Host: guest allowed to in | Join block, host / guest | Host: flips with no step in the Pod (longest run) | Guest: the same, and windows with a run of 30 or more | Longest gap in the guest's INPUTS on the host | Level start: longest run with no step, host / guest (flips) | Loading screen over the guest's play (times) | Send budget: lowest and cuts, host / guest | The game's RTT, host / guest (ms) | Longest hold behind a gap | Longest wait for a datagram |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| c60_l0 | 4.1 s | 1.6 s / 2.1 s | 0.2% (1) | 0.8% (2), 0 | 67 ms | 28 / 28 | 0 | 32,768, 0 / 16,384, 0 | 67 / 61 | 0 ms | 409 ms |
| c60_l1 | 3.7 s | 1.3 s / 1.8 s | 2.3% (1) | 0.9% (1), 0 | 85 ms | 28 / 28 | 0 | 32,768, 0 / 16,384, 0 | 68 / 63 | 0 ms | 483 ms |
| c60_l5 | 3.9 s | 1.3 s / 2.0 s | 4.9% (1) | 0.6% (1), 0 | 70 ms | 28 / 27 | 0 | 32,768, 0 / 16,384, 0 | 70 / 58 | 0 ms | 483 ms |
| c150_l0 | 5.2 s | 1.9 s / 2.6 s | 0.0% (0) | 0.0% (0), 0 | 38 ms | 27 / 33 | 0 | 32,768, 0 / 16,384, 0 | 167 / 144 | 0 ms | 372 ms |
| c150_l1 | 5.4 s | 2.0 s / 2.9 s | 0.0% (1) | 0.9% (1), 0 | 68 ms | 26 / 32 | 0 | 32,768, 0 / 16,384, 0 | 167 / 152 | 0 ms | 342 ms |
| c150_l5 | 5.4 s | 2.0 s / 2.8 s | 4.4% (1) | 0.5% (1), 0 | 69 ms | 26 / 32 | 0 | 32,768, 0 / 16,384, 0 | 170 / 148 | 0 ms | 335 ms |
| c450_l0 | 10.8 s | 4.1 s / 5.3 s | 0.0% (0) | 0.0% (0), 0 | 36 ms | 26 / 48 | 0 | 32,768, 0 / 16,384, 0 | 467 / 444 | 0 ms | 404 ms |
| c450_l1 | 11.1 s | 4.1 s / 5.6 s | 1.9% (1) | 1.2% (1), 0 | 68 ms | 26 / 48 | 0 | 32,768, 0 / 16,384, 0 | 467 / 445 | 0 ms | 333 ms |
| c450_l5 | 11.1 s | 4.3 s / 5.6 s | 0.0% (1) | 0.8% (1), 0 | 69 ms | 26 / 48 | 0 | 32,768, 0 / 16,384, 0 | 470 / 458 | 0 ms | 237 ms |
| c700_l0 | 15.7 s | 5.9 s / 7.9 s | 0.0% (0) | 0.0% (1), 0 | 37 ms | 26 / 65 | 0 | 32,768, 0 / 16,384, 0 | 733 / 700 | 0 ms | 367 ms |
| c700_l1 | 15.5 s | 5.8 s / 7.7 s | 2.3% (1) | 1.2% (2), 0 | 68 ms | 26 / 65 | 0 | 65,536, 0 / 16,384, 0 | 734 / 701 | 0 ms | 366 ms |
| c1000_l0 | 21.1 s | 8.0 s / 10.6 s | 0.1% (1) | 0.0% (0), 0 | 52 ms | 25 / 84 | 1 | 32,768, 0 / 16,384, 0 | 1033 / 1000 | 0 ms | 273 ms |
| n60_l1 | 3.9 s | 1.5 s / 2.0 s | 2.2% (1) | 3.3% (3), 0 | 68 ms | 26 / 26 | 0 | 32,768, 0 / 16,384, 0 | 67 / 55 | 0 ms | 466 ms |
| n60_l5 | 4.2 s | 1.7 s / 2.3 s | 4.1% (1) | 3.6% (1), 0 | 68 ms | 26 / 26 | 0 | 32,768, 0 / 16,384, 0 | 67 / 57 | 0 ms | 400 ms |
| n450_l1 | 11.0 s | 4.1 s / 5.6 s | 2.3% (25) | 0.9% (1), 0 | 967 ms | 26 / 49 | 0 | 65,536, 0 / 16,384, 0 | 467 / 444 | 0 ms | 289 ms |
| n450_l5 | 13.6 s | 5.1 s / 7.5 s | 4.2% (1) | 0.0% (0), 0 | 69 ms | 26 / 48 | 1 | 32,768, 0 / 16,384, 0 | 467 / 444 | 0 ms | 267 ms |
| b60_b4 | 4.4 s | 1.8 s / 2.5 s | 6.5% (5) | 11.7% (9), 0 | 229 ms | 26 / 35 | 0 | 32,768, 0 / 10,240, 2 | 79 / 77 | 61 ms | 361 ms |
| b150_b4 | 5.6 s | 1.9 s / 2.7 s | 10.6% (9) | 17.9% (14), 0 | 320 ms | 26 / 33 | 0 | 32,768, 0 / 16,384, 0 | 201 / 179 | 151 ms | 484 ms |
| b450_b2 | 11.0 s | 4.2 s / 5.5 s | 4.0% (2) | 2.7% (2), 0 | 102 ms | 26 / 49 | 0 | 32,768, 0 / 16,384, 0 | 468 / 446 | 0 ms | 366 ms |
| b450_b3 | 11.8 s | 4.3 s / 5.8 s | 18.2% (16) | 32.8% (18), 0 | 586 ms | 26 / 49 | 4 | 32,768, 1 / 32,768, 0 | 605 / 569 | 451 ms | 618 ms |
| nb450_b2 | 13.3 s | 4.6 s / 6.1 s | 22.9% (28) | 25.1% (28), 0 | 1003 ms | - | 3 | - | - | - | - |
| nb450_b3 | 15.0 s | 4.3 s / 5.8 s | 26.1% (29) | 28.8% (36), 1 | 1001 ms | 26 / 59 | 35 | 65,536, 0 / 16,384, 0 | 473 / 452 | 0 ms | 398 ms |
| b450_b4 | 11.9 s | 4.1 s / 5.5 s | 22.9% (17) | 40.4% (31), 3 | 620 ms | 30 / 58 | 4 | 24,576, 1 / 16,384, 0 | 634 / 602 | 451 ms | 1069 ms |
| b450_b10 | 12.3 s | 4.3 s / 5.7 s | 11.4% (23) | 24.7% (37), 2 | 820 ms | 26 / 50 | 0 | 32,768, 2 / 16,384, 0 | 573 / 548 | 451 ms | 1340 ms |
| b450_b30r | 10.1 s | 3.0 s / 4.6 s | 16.1% (29) | 36.3% (41), 14 | 1486 ms | 28 / 72 | 34 | 27,648, 7 / 10,240, 4 | 692 / 746 | 451 ms | 1950 ms |
| nb60_b10 | 3.7 s | 1.4 s / 1.9 s | 4.4% (13) | 9.7% (14), 0 | 468 ms | 28 / 28 | 0 | 32,768, 0 / 16,384, 0 | 67 / 55 | 0 ms | 535 ms |
| nb450_b4 | 15.1 s | 4.3 s / 5.8 s | 26.5% (28) | 44.1% (33), 2 | 1001 ms | 29 / 68 | 37 | 32,768, 0 / 16,384, 0 | 490 / 456 | 0 ms | 466 ms |
| nb450_b10 | 10.8 s | 4.1 s / 5.3 s | 10.7% (24) | 28.4% (32), 5 | 1001 ms | 28 / 85 | 27 | 32,768, 0 / 32,768, 0 | 477 / 449 | 0 ms | 667 ms |
| c450_l5x | 11.1 s | 4.2 s / 5.8 s | 5.1% (13) | 0.2% (1), 0 | 69 ms | 26 / 49 | 0 | 32,768, 0 / 16,384, 0 | 470 / 447 | 0 ms | 299 ms |
| b450_b4x | 12.7 s | 4.6 s / 6.1 s | 23.9% (18) | 40.9% (31), 1 | 634 ms | 30 / 61 | 109 | 32,768, 2 / 16,384, 0 | 634 / 601 | 451 ms | 1117 ms |

How to read the tags: `c` is gap-free delivery on, `n` off; the number is the RTT in ms; `l0`, `l1`, `l5` is 0%, 1%, 5% single loss; `b2`, `b3`, `b4`, `b10`, `b30` is that many datagrams lost in a row out of every 101 (301 for ten, 601 for thirty), counted on what each game receives. The host receives the guest's 30 a second, so for it four in a row is 133 ms every 3.4 s, ten a third of a second every 10 s and thirty a second every 20 s. The guest receives 1.2 to 2 times as many datagrams from the host, so its bursts come that much more often and are that much shorter; `x` is the ten-minute rerun. A flip is a frame shown, 30 a second; a run of 30 flips with no step is a stall of a second.

**The measures the ticket asked for, in words:**

- **Time to join and to spawn.** From the host's allow to the guest being in: about 4 s at 60 ms, 5 s at 150, 11 s at 450, 16 s at 700 and 21 s at 1000. Inside it, every PC stands still for the join's block: 1.3 to 1.7 s on the host at 60 ms, 4.1 s at 450 ms, 8.0 s at 1000 ms, and 0.5 to 2.6 s longer on the guest. Loss barely moves it (13.6 s to get in at 450 ms with 5% loss and no gap-free delivery).
- **Send budget.** The host's toward the guest starts at 32,768 and doubles as play goes on; the guest's stays at 16,384. No cut in any cell of the grid. Cuts came only with bursts (seven on the host and four on the guest with one-second outages, the guest's at the 10,240 floor) and after a 12 s freeze.
- **Does the guest send INPUTS while it loads the level?** No, not for a while: at every level start the host saw a gap in the guest's INPUTS of 1.2 to 1.3 s at 60 ms, 1.8 s at 450 ms and 3.0 s at 1000 ms, and waited: 25 to 30 flips with no step on the host, 26 to 85 on the guest. That answers what [Does a guest that is still loading make the host wait on every frame?](21-loading-guest-wait.md) left open: a level start costs everyone about a second, and the guest more on a slow path.
- **Stalls.** None of a second or more in the Pod or the level in any cell without bursts, bar the one in n450_l1. Single losses show as single flips with no step on the host (a frame waited for, then caught up): in most cells about as many percent of flips as the loss rate, in two next to none (c150_l1, and c450_l5 in its three-minute run; its ten-minute rerun had 5.1%). Why those two differ wasn't looked into.
- **The host's waits.** In the table as the gap in the guest's INPUTS. The host waits about as long as the gap, up to a second (the wait rule), then catches up.
- **Kicks.** None in any cell of the grid, the bursts or the far cells. The longest the game went without a datagram was 2.0 s (one-second outages at 450 ms).
- **DATA3's holds.** Nothing was ever held with single losses (the copies fill them). With bursts it held datagrams for up to 451 ms, one round trip, and asked again; nothing was given up. It also sends each missing datagram again many times over: in b450_b4 the guest sent 2,544 datagrams again for the 184 the host was missing after the copies (368 dropped, 184 filled), and the host 3,515 for the guest's 282, so 13 to 14 sends per missing datagram; b450_b30r comes to the same. (The log's own "sent again of N asked" sets one side's resends beside that same side's requests and can't be read as a ratio.) The copies cover exactly two datagrams of a burst: 184 of 368 dropped were filled at four in a row, 72 of 360 at ten, 64 of 960 at thirty. With bursts and gap-free delivery on, the game's own RTT rose to 570 to 750 ms on a 451 ms path, so the hold slows the game's own repair too.
- **The delay between the guest's input and their Sackboy moving on the host:** the logs can't show it. Not measured.

**The freezes** (the guest's frame thread stopped with `LBP3_TEST_FREEZE`, twice each per run; the link's thread runs on, as in a shader build):

| Cell | RTT | Catch-up window | Host: longest run with no step after a 1 s freeze | After a 3.5 s freeze |
| --- | --- | --- | --- | --- |
| f60 | 60 ms | 30 (default) | 24 and 20 flips | 23 and 28 flips |
| f450 | 450 ms | 30 | 15 and 22 | 30 and 30 |
| f60_c10 | 60 ms | 10 | 9 and 8 | 10 and 10 |
| f450_c10 | 450 ms | 10 | 10 and 8 | 9 and 10 |

- **A freeze of the guest stalls every player for up to a second, however long the freeze**: the host waits for the guest's input for the catch-up window and then goes on without it. That is 15 November's "everyone stands still when his game hitches".
- **With `FramesBeforeAgressiveCatchup` at 10 on both games the stall is a third of a second.** The value was written into the game's settings in memory (`LBP3_TEST_CATCHUP_FRAMES`). What it does to play with loss or bursts wasn't run. This is the first measured lever for [Which of the game netcode's own values does the port change?](14-game-netcode-values.md).

**The odd cases:**

- **A 10 s one-way hold during the join at 450 ms (h450): the join fails.** The host's game got nothing for 10.1 s and kicked the guest. A hold as long as the kick limit is a kick; it didn't need the join.
- **A guest that hangs as the join's block begins (hung60, its process suspended for 15 s):** the host stood still for 10.6 s, then kicked the guest 9.7 s after its last datagram and stepped again, as [How long can a joining guest that hangs keep everyone from stepping?](27-hung-join-blocks-all.md) predicted. The guest's own block was 21.3 s.
- **A 12 s freeze of the guest with the kick limit raised to 30 s on both games (thaw60, `LBP3_TEST_KICK_SECONDS`): the session survives.** The host stalled for 21 flips at most, held 270 unacknowledged messages and cut its send budget three times (to 13,824); the guest came back behind the catch-up loading screen and both went on into the level start. With the limit at 10 s this is a kick ([Does a game that thaws after more than 10 s remove every player before it reads what arrived?](29-thaw-removes-everyone.md)).
- **A join into a level (lv60, lv450; the host alone in the Introduction first):** the block is no longer than in the Pod (1.8 s and 2.4 s at 60 ms; 4.4 s and 6.2 s at 450 ms). The game state was 310,275 bytes, seen in a download place of the guest inside the block (18,200 of them fetched 4 s in), and a 44,546-byte resource followed after it. The link's own counts agree: in lv450 the guest's game was given 337,875 bytes in the 5 s window that lies inside its block, and about 165,000 in each of the next two. So the state does travel through the resource transfer manager's places and fits inside the block. That is far more per second than the send budget the host's connection showed at the time (16,384, then 32,768), so either first sends of a resource aren't held to the budget or the budget isn't bytes per second as [What makes the game netcode cut its send budget, and does the peer link trigger it?](15-send-budget-cuts.md) read it; not looked into. The host's sending place was never caught in use, and the guest's download place only once (the lines sample it every 2 s inside a block). The Introduction is a small level; a big community level wasn't tried.

**What "playable" rests on.** Nobody watched these runs. "Not worth playing" is read from the step counts: a guest that steps on 60% of its frames, in stalls of up to a second, with a loading screen over the game every few seconds. Where the line lies between that and the 12% of the same bursts at 60 ms is for [What is the netcode bar?](07-netcode-bar.md) to say.

**Not run, and why:**

- **The last four burst cells crossed midnight and ran on the later build.** b450_b2, b450_b3, nb450_b2 and nb450_b3 ran on `b350def`, after the exchange line's fix. nb450_b2 ran over midnight, which the summary script's clock doesn't follow: its Pod figures are from before midnight and stand, its level and link columns are empty. Bursts of two and three were run at 450 ms only.
- **The first b450_b30 run was thrown away.** The rig pressed "allow" before the host's prompt was up and the join timed out; `join.sh` now waits for the prompt, and b450_b30r is the run that counts. Its files are still in runs/06, with h450 and hung60, the two joins that failed by design.
- **What is on screen during a hung join** (hung60) wasn't recorded: no screenshot was taken inside the block.

- **The build changed once after the cells.** All cells ran on runtime commit `35c978c`. The fix to the session log's exchange line that [What are the entries the host thread keeps awaiting after a join?](34-awaited-entries-after-join.md) asked for came after them (`b350def`, checked in one short run, v60: the line now ends with "over" and no longer lists the kept labels entries). In the logs of the cells that line still shows the kept entries as awaited. The tick gate was last run before the three test settings and this fix went in; they are test-only or print-only, and the gate goes with the branch's landing.

- **Drops with the send budget at the floor and gap-free delivery off.** The only way found to put the budget at the floor without bursts is the RTT clamp at no delay, which bends everything else. The guest's budget did sit at the floor in b450_b30r, with gap-free delivery on.
- **A lower catch-up window under loss**, see above.
- **Runs on anything but the Pod and the Introduction**, and with three players.
- **The rig idles into the game's attract video.** A host left alone and untouched for about two and a half minutes plays a video and its simulation stops. It spoiled the tail of the two failed joins and nothing else; a cell must not leave a lone host idle that long.

**What follows:**

- [What is the netcode bar?](07-netcode-bar.md) has its numbers: what a join, a level start, a freeze and a burst cost at each RTT, and that steady delay and single loss cost almost nothing but waiting.
- [How should the peer link deliver game datagrams?](09-datagram-delivery.md): for single losses the game needs no help. For bursts gap-free delivery didn't help and may hurt (the hold of a round trip, the game's RTT pushed up, 48 times more sent again than asked). What would help with bursts wasn't tried.
- [Which of the game netcode's own values does the port change?](14-game-netcode-values.md): the catch-up window (a second down to a third) and the kick limit (a 12 s freeze survived at 30 s) both work as levers, each measured once.
