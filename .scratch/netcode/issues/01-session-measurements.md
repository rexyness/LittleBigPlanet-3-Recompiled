# What did the 15 November session with the friend measure?

Type: research
Status: resolved
Claimed by: research agent (charting session, 2026-11-15)
Findings: branch `research/netcode-session-measurements` (`ef939cb`, local only), file `docs/research/netcode-session-measurements.md`
Blocked by: none
Map: [Online co-op netcode](../map.md)

## Question

The netcode bar needs numbers to stand against. Take them from the two logs of the 2026-11-15 session:

- **Host:** `D:\Recomps\lbp3-recompiled\analysis\play_log.txt` (20:10 to 21:01).
- **Friend:** `D:\Recomps\lbp3-recompiled\analysis\online\friend_report_20261115_192711.zip` (his clock is offset from the host's; line the logs up on a shared event).

For each connection:

- the time from the game asking for it to ESTABLISHED, and from there to the guest being in the Pod;
- the path (relay or direct);
- RTT in each direction: median, 95th percentile, worst;
- game datagram rates each way;
- DATA3's work: filled from copies, sent again, skipped, and how long delivery waited behind each gap;
- every stall and every freeze: start, length, and the cause where the logs show one;
- how it ended and which side ended it;
- joins and level changes: how long they took and how much was sent.

Also list what the logs can't show, and which log lines would be needed to show it.

Done when there is one table per connection, a summary line per measure across the session, and the list of missing measurements. [rejoin_no_spawn.md](../../lbp3-port/coop/rejoin_no_spawn.md) has a first reading of the same logs; check its numbers rather than repeat them.

## Answer

Resolved 2026-11-15 by the research agent, from the host's log (all six connections) and the friend's (connections 1 and 2 only). A time on a line without a stamp is good to about ±0.8 s. The findings file has one table per connection.

| Measure | Across the session |
|---|---|
| Connections | 6, connected for 38.5 min in all (11 min 33 s, 1 min 21 s, 4 min 26 s, 4 s, 3 s, 21 min 4 s) |
| Asking to ESTABLISHED | 4.0 to 6.0 s and 3.0 to 5.0 s on the side that waited. About 4 s of that is the link's fixed wait before it tries the relay (inferred) |
| ESTABLISHED to the guest in the Pod | 17.5 to 19.5 s, read once from the presence text. Otherwise 8 to 18 s until the guest sends at its full rate, which is a stand-in. When a Sackboy appears isn't logged |
| Path | The relay path in 461 of 461 host summaries and 153 of 153 of the friend's |
| RTT (one sample per 5 s) | Host: median 455 ms, 95th percentile 590, worst 9,800. Friend: median 453, 95th percentile 525, worst 10,000 (the cap) |
| Each player's leg to the online server | Host 180 ms, friend 279 ms. Together 459 ms, so the relayed RTT is the sum of the two legs |
| Game datagram rates | Guest: 30.0 a second. Session owner: median 41 to 43, 95th percentile 51 to 64, peak 173 a second |
| DATA3 | Host: 24 filled from copies, 164 requests sent, 250 skipped (all in the last 10 s of connection 6). Friend: 20 filled, none skipped |
| Wait behind a gap | Not logged. About 0.47 s for the one request episode that was repaired (inferred); 2.0 s per skip |
| Freezes | Host: 27 windows with a frame gap of 100 ms or more, longest 648 ms, 9.0 s in all. Friend: 22 windows, longest 3,456 ms, 21.2 s in all. Pipeline builds in 24 of the 27 and in all 22 |
| Stalls | Can't be seen: neither log has simulation-step lines |
| Delivery delays, friend to host, with no loss | Up to 3.2 s (connection 1) and 9.4 s (connection 2), measured from both logs. Six more spells of 1.0 to 2.4 s RTT later (direction inferred) |
| Endings | One by the friend's game. Three by the host's game on its kick path, 9 to 10 s after the last usable datagram. Two by the host's game 3 to 5 s after ESTABLISHED, on other call paths, with datagrams still arriving |
| Joins | 8 to 18 s; 292 to 342 datagrams from the host (1,634 when the two swapped roles) |
| Level changes | 15 to 35 s of unusual traffic; 724 to 3,003 datagrams from the session owner, single windows at three times its steady rate, while the guest sent 46 to 92% of its 30 a second |

**The three findings that matter for targets:**
- **Relay only, at 455 ms.** It is the sum of the two players' legs to the server. One way is about 0.22 s.
- **The game's 10 s kick is the cliff.** All three involuntary endings were kicks. A delay of 9.4 s with no loss, so nothing for DATA3 to repair, came within a second of it. Connection 6 ended after DATA3 skipped 250 datagrams. The two measures that matter most, the longest time the game gets nothing and the worst one-way delay, aren't logged today.
- **A freeze costs 30 datagrams per frozen second,** on either PC. The host froze too.

**Where it differs from the first reading** ([rejoin_no_spawn.md](../../lbp3-port/coop/rejoin_no_spawn.md)):
- Connection 1 began with the host as the guest in the friend's Pod for 85 s; the two swapped at 20:15:03.
- That reading's table lacks connections 5 and 6.
- The 10 s delay began at about 20:25:59, and there were more delay spells than its three.
- Its "one-way stalls" are delivery delays on the network, not stalls.

**Not determined:** whether the friend spawned in connections 3 and 6; why the host's game ended connections 4 and 5 within seconds; what held the friend's datagrams for 9.4 s; whether connection 6 ended because of the host's line or the server.

**What follows from it:** the logs lack most of what a bar would be measured by. [Which log lines does a session need to measure itself?](19-session-log-lines.md) takes the list.
