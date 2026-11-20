# What the 15 November session with the friend measured

Research for [issue 01](../../.scratch/netcode/issues/01-session-measurements.md) of the [online co-op netcode map](../../.scratch/netcode/map.md). Reading only: no game was started and nothing was built.

The two players are called **the host** and **the friend** throughout. The host is the player whose log is `play_log.txt`. Where it matters whose session a stretch was played in, the text says so, because the two swapped once (see [Who was whose guest](#who-was-whose-guest)).

## Sources

| Short name | File | What it is |
|---|---|---|
| **H** | `D:\Recomps\lbp3-recompiled\analysis\play_log.txt` | The host's game log, one run, 26,025 lines, about 20:10:35 to 21:01:57. |
| **F** | `play_launcher_log.1.txt` inside `analysis\online\friend_report_20261115_192711.zip` | The friend's game log, one run, 8,690 lines, 19:10:19 to 19:26:46 on his clock. |
| F2 | `play_launcher_log.txt` in the same zip | A 7 s run of the friend's game (19:26:49 to 19:26:56). No connection in it. |
| code | `D:\Recomps\lbp3-recompiled`, `main` `519f220` (v0.1.5) | What each log line means: `runtime/src/net/p2p_link.cpp`, `p2p_link.h`, `runtime/src/hle/np_shadnet.cpp`, `videoout.cpp`, `net_offline.cpp`. |
| design note | `.scratch/lbp3-port/best/shadnet_design.md` | The link, DATA3, the game's 10 s limit. |
| first reading | `.scratch/lbp3-port/coop/rejoin_no_spawn.md` | An earlier reading of the same logs, checked [below](#against-the-first-reading). |

A citation `H:2392` means line 2392 of the host's log; `F:8095` is a line of the friend's.

**Builds.** The host ran v0.1.5. The friend ran v0.1.3 or v0.1.4: his log has the exact-maths line (F:4, since v0.1.3) but not the `IsOnline__` hook the host's has (H:90). `p2p_link.*`, `np_shadnet.cpp`, `net_offline.cpp` and `videoout.cpp` are the same files in v0.1.3, v0.1.4 and v0.1.5 (`git diff` between the tags is empty), so both logs' lines mean the same thing.

**Coverage.** The host's log covers all six connections. The friend's log covers connections 1 and 2 only. For connections 3 to 6 there is no friend-side measurement of anything.

## How the numbers were taken

- **Times.** Only four kinds of line carry a wall-clock time: the link's 5 s summary, its "left" and "never came" lines, and the "the game ends connection" line (`p2p_link.cpp:735`, `:678`, `:346`, `np_shadnet.cpp:1388`). Every other line was timed from the `[video]` lines around it. Each `[video]` line closes a window of 30 frames and gives their average length (`videoout.cpp:224`), so the lines form a frame clock. Checked by leaving each stamp out and predicting it from the others: the host's 476 stamps all fall inside their predicted interval, median error 0.18 s, worst 0.58 s; the friend's 155 stamps likewise, median 0.22 s, worst 1.1 s. An unstamped time below is good to about ±0.8 s. Times between two unstamped lines are given as a range.
- **The two clocks.** Host clock = friend clock + 1 h + 0.27 s. This comes from matching the two logs' datagram counts while the sender ran at a steady 30 per second: host → friend gives offset minus delay = 0.044 s (13 windows), friend → host gives offset plus delay = 0.489 s (53 windows). The BYE gives the same sum, 0.502 s (F:7819 to H:8007). The 0.27 s assumes the delay is equal each way; the two delays together come to 0.445 s. All times below are on the host's clock unless marked "friend's clock".
- **Rates.** The link prints one line every 5 s while anything flows: game datagrams sent and received in that window, each connected player's path and RTT, and the DATA3 counters so far (`p2p_link.cpp:711-737`). A 5 s span with no line is a window with no datagrams. "Sent" counts each datagram the game handed to the link once; repeats are not counted (`p2p_link.cpp:1062`). "Received" counts a datagram when it arrives at the link, not when the game gets it (`p2p_link.cpp:604`). Cross-check: all 41 "game datagram N to" marker lines (34 in H, 7 in F) fall in the window the sums predict.
- **RTT.** Each summary line shows the last PONG's round trip at that moment (`p2p_link.cpp:668`, `:721`). That is one sample per 5 s out of about five pings (`keepaliveMs = 1000`, `p2p_link.h:76`), it stays stale while no PONG arrives, and it is capped at 10 s. It is a round trip, so "each direction" below means "as each side measured it". PING is answered by the link's own thread (`p2p_link.cpp:663-664`), so a game freeze does not show in it: the friend's samples stayed at 453 to 456 ms through his 3.5 s freeze (F:4085, F:4090).
- **DATA3 counters.** They count from the start of the game, not of the connection, so the tables give differences. "Filled from copies" and "skipped" are counted by the receiver. "Sent again" is counted by the sender, when it answers the other side's requests. "Asked" is the number of request packets (NACKs) this side sent, repeated about every 30 to 40 ms while a gap is open (`p2p_link.cpp:345`, `:353-360`, `:603`, `:626`). So one log line mixes the two directions.
- **Freezes.** A `[video]` window's longest frame gap and its length, with the `[perf]` line after it for pipeline and shader builds (`videoout.cpp:176`). A window of 30 frames lasts 1.00 s when nothing is wrong; "time over" is its length minus 1.00 s.
- **Whose session.** A game that owns its session sends itself about 300 loopback messages per 5 s; a guest's game sends about 10 (`[net] loopback:` lines, `net_offline.cpp:171`). The presence answers name the session's owner in `gameData` and give the status text. The two agree wherever both exist. Reading the owner out of `gameData` is inferred from that agreement.

## Session summary

| Measure | Across the session |
|---|---|
| Connections | 6 (host's connection ids 1 to 6). Connected for 38.5 min in all: 11 min 33 s, 1 min 21 s, 4 min 26 s, 4 s, 3 s, 21 min 4 s. |
| Asking to ESTABLISHED | Measured twice on the side that waited: 4.0 to 6.0 s (connection 1, the host asked) and 3.0 to 5.0 s (connection 2, the friend asked). The link does not try the relay until 4.0 s have passed (`relayAfterMs`, `p2p_link.h:76`, `p2p_link.cpp:787`), so both were about 4.5 to 5 s (inferred). Connections 3 to 6: the friend's wait is not in any log; the host's own ESTABLISHED came in the same second as its request, because the friend had connected first. |
| ESTABLISHED to the guest in the Pod | Read from the presence text once: 17.5 to 19.5 s (connection 1, the host joining the friend's Pod). Otherwise only a stand-in exists: the guest's game reaching its full 30 datagrams per second, 8 to 18 s after ESTABLISHED in four joins (inferred). When the guest's Sackboy appears is not logged. |
| Path | Relay, always: 461 of 461 host summary lines with a player in them and 153 of 153 of the friend's say "relayed". No line reports a direct packet. |
| RTT, host's samples | n = 461: median 455 ms, 95th percentile 590 ms, worst 9,800 ms. 22 samples over 600 ms, 12 over 1 s. |
| RTT, friend's samples (connections 1 and 2) | n = 153: median 453 ms, 95th percentile 525 ms, worst 10,000 ms (the cap). 6 samples over 1 s. |
| Each player's leg to the online server | Host 179 to 180 ms (30 ICMP replies, H:962 to H:2269, all before 20:13:22). Friend 268 to 288 ms, median 279 ms (26 replies, F:883 to F:2203, all before 19:13:14). Together 459 ms, against the relayed median of 453 to 455 ms. The ICMP target is the same address as the link's server. |
| Game datagrams, guest to session owner | 30.0 per second when nothing is wrong (150 ± 1 per 5 s). |
| Game datagrams, session owner to guest | Median 41 to 43 per second in steady play (204 to 215 per 5 s over connection 1's phase B and connections 3 and 6), 95th percentile 51 to 64 per second (255 to 320 per 5 s), up to 173 per second when the roles swapped (864 in 5 s). |
| Totals | Host sent 103,061 and received 65,591. Friend (to 19:26:46) sent 22,067 and received 31,327. |
| DATA3, host as receiver | 24 filled from copies; 164 requests sent in two episodes (12, then 152 in the last 15 s); 250 skipped, all in the last 10 s of connection 6. |
| DATA3, friend as receiver (connections 1 and 2) | 20 filled from copies, no requests sent, none skipped. |
| DATA3, sent again | Friend 24 (answering the host's 12 requests). Host 12 (answering the friend, connection 6). |
| Wait behind a gap | Not logged. Bounds: 2.0 s for each of the three skipped gaps (by design); about 0.47 s for the one request episode that was repaired (inferred: 12 requests at one per 39 to 40 ms); about one datagram interval, 33 ms, for a gap filled from copies (inferred). |
| Freezes, host | 27 windows with a frame gap of 100 ms or more while connected. Longest gap 648 ms. 9.0 s over in total. 24 of the 27 have pipeline builds logged in the same window. |
| Freezes, friend (connections 1 and 2) | 22 windows, all in connection 1. Longest gap 3,456 ms. 21.2 s over in total. All 22 have pipeline builds. |
| Stalls | Cannot be seen in these logs. See [Stalls](#stalls). |
| One-way delivery delays | Friend → host, without loss: up to 3.2 s (connection 1) and 9.4 s (connection 2), measured from both logs. Six more spells of 1.0 to 2.4 s RTT in connections 3 and 6, direction inferred. Host → friend: none over about 1 s in connections 1 and 2; unknown after that. |
| How connections ended | 1 by the friend's game (BYE). 3 by the host's game on its kick path: about 9.9 s and 9.3 s after the friend's last datagram arrived (connections 2 and 3), and 7.5 to 13.7 s after the last datagram the link could deliver in order (connection 6). 2 by the host's game 3 to 5 s after ESTABLISHED, on other paths. None by the link's own 15 s limit. |
| Joins | 8 to 18 s from ESTABLISHED to full rate. 292 to 342 datagrams sent by the host in that time, or 1,634 when the roles swapped. |
| Level changes | 15 to 35 s of unusual traffic each. The session owner sent 724 to 3,003 datagrams, with single windows at up to 3 times its steady rate (667 against 210 per 5 s), while the guest sent 46 to 92% of its usual 30 per second. |

## Who was whose guest

Connection 1 was not one join. It had two phases, and the "guest" changed.

| Phase | Time | Whose session | Evidence |
|---|---|---|---|
| A | 20:13:46 to 20:15:03 | The friend's. The host was the guest. | The host's loopback count fell from 298 to 255 to 10 per 5 s (H:2438, H:2512, H:2583) while the friend's stayed near 300 (F:2681). The friend's presence read "Pod 1/4" then "Pod 2/4" with his own name as the session's (H:2442, H:2632). The friend's log read the host's presence as "Pod 2/4" in the friend's session (F:2662). The host sent exactly 150 or 151 per 5 s in all 13 windows (H:2684 to H:3165). |
| B | 20:15:03 to 20:25:11 | The host's. The friend was the guest. | The host's loopback count rose from 10 to 240 and on (H:3164, H:3279); the friend's fell from 276 to 14 (F:3202, F:3278). The friend's presence named the host's session (H:3225), read "Pod 1/4" (H:3414) and then "Pod 2/4" (H:3491). The friend sent 150 per 5 s from then on. |

In connections 2, 3 and 6 the friend was the guest in the host's session. For connection 2 both logs show it (F:8167, F:8219: his loopback count falls from 244 to 10). For connections 3 and 6 it is inferred from the host's rates, which match phase B.

## Connection 1

20:13:38 to 20:25:11, 11 min 33 s.

| Measure | Value | Where |
|---|---|---|
| Who asked | The host's game (`ActivateConnection`). The friend's game answered after PEER_ACTIVATED. | H:2359; F:2380-2385 |
| Asking to ESTABLISHED | 4.0 to 6.0 s on the host (five `[video]` lines lie between). ESTABLISHED at 20:13:38.5 ± 0.8. The friend's side saw the HELLO at 19:13:37.4 ± 0.6, friend's clock, and was established in the same second. | H:2359 → H:2392-2393; F:2379-2384 |
| ESTABLISHED to guest in the Pod, phase A (the host as guest) | Host's game left its own session about 7 s after ESTABLISHED (inferred from the loopback counts). First presence reading "Pod 2/4": 17.5 to 19.5 s after ESTABLISHED, at 20:13:57.0 ± 0.8. The friend's log shows the same at 19:13:57.6 ± 0.6, friend's clock. | H:2512, H:2583; H:2392 → H:2632; F:2662 |
| Roles swap to guest in the Pod, phase B (the friend as guest) | Swap began at 20:15:03.8 ± 0.9. Friend's presence "Pod 2/4" in the host's session at 20:15:22.1 ± 0.9: about 18 s. His game was back at 150 per 5 s in the window ending 19:15:25.2, friend's clock. | H:3201-3225, H:3491; F:3452 |
| Path | Relay in all 138 host lines and all 139 of the friend's. | H:2441 to H:7975; F:2413 to F:7816 |
| RTT, host's samples | n = 138: median 454 ms, 95th 470 ms, worst 3,371 ms. 3 samples over 1 s. | same lines |
| RTT, friend's samples | n = 139: median 453 ms, 95th 473 ms, worst 3,080 ms. 3 samples over 1 s. | same lines |
| Datagrams, totals | Host sent 29,370; friend received 29,345 (the rest arrived after he had ended the connection). Friend sent 20,321; host received 20,321. | H:2441 to H:8025; F:2413 to F:7934; F:7836-7855 |
| Datagrams per 5 s, phase A | Host (guest) 150 to 151. Friend (session owner) median 183, range 151 to 318. | H:2684 to H:3165 |
| Datagrams per 5 s, phase B | Host (session owner) median 207, range 139 to 432. Friend (guest) median 150; lower only in the windows listed under freezes, the scene change and the delay. | H:3583 to H:7975 |
| DATA3, friend → host | 15 filled from copies. 12 requests sent, all in the window ending 20:23:47.9. None skipped. The friend sent 24 again for those requests. | H:2684 to H:7398; F:7203 |
| DATA3, host → friend | 20 filled from copies. No requests. None skipped. The host sent nothing again. | F:3903 to F:7638 |
| Wait behind a gap | Not logged. The 12 requests, at the one per 39 to 40 ms seen at the end of connection 6, put that one wait at about 0.47 s, close to one round trip (inferred). | `p2p_link.cpp:353` |
| Freezes | Host 17 windows, longest gap 648 ms, 6.4 s over. Friend 22 windows, longest gap 3,456 ms, 21.2 s over. | tables below |
| Delivery delay | 20:23:47 to 20:24:08: friend → host datagrams arrived up to 3.2 s late; none lost. | [delays](#delivery-delays-on-the-network) |
| How it ended | The friend's game called `TerminateConnection` at 19:25:11.435, friend's clock. The host got the BYE at 20:25:11.937 and its game was told DEAD, TERMINATED_BY_PEER (`0x80552710`). The link was healthy up to that second (150 sent, 197 received, RTT 464 ms in his last window). | F:7818-7819, F:7816; H:8007, H:8010 |
| Which side ended it | The friend's. Whether he chose to leave is not in the log. His game was back in its own session 2 to 7 s later (loopback count 51, then 301). | F:7893, F:7947 |

## Connection 2

20:25:36 to 20:26:57, 1 min 21 s. The friend's game stopped 10 s before the end. The user's report: the friend loaded into the host's Pod and his Sackboy never appeared.

| Measure | Value | Where |
|---|---|---|
| Who asked | The friend's game, 20 to 22 s after he had ended connection 1. | F:7819 → F:8066 |
| Asking to ESTABLISHED | 3.0 to 5.0 s on the friend's side (four `[video]` lines between); with the link's 4.0 s wait before the relay, 4.0 to 5.0 s. ESTABLISHED at 19:25:36.4 ± 0.9, friend's clock. On the host, PEER_ACTIVATED, its own request and ESTABLISHED all fell in one second, 20:25:36.1 ± 0.9. | F:8066 → F:8095-8096; H:8194-8198 |
| ESTABLISHED to guest in the Pod | No presence text (see [missing](#what-the-logs-cannot-show)). The friend's game left its own session about 6 s after ESTABLISHED (inferred). Both games exchanged 150 per 5 s from the windows ending 20:25:53.2 (host) and 19:25:51.4 (friend's clock): 10 to 17 s after ESTABLISHED. That he never spawned is not visible in either log. | F:8167, F:8219; H:8347; F:8250 |
| Path | Relay in all 16 host lines and all 14 of the friend's. | H:8216 to H:8858; F:8140 to F:8689 |
| RTT, host's samples | n = 16: median 457 ms, worst 9,800 ms. 2 samples over 1 s (5,062 and 9,800). Too few samples for a 95th percentile. | same lines |
| RTT, friend's samples | n = 14: median 464 ms, worst 10,000 ms (the cap). 3 samples over 1 s (4,772, 10,000, 3,694). | same lines |
| Datagrams, totals | Host sent 2,499, of which 466 after the friend's game had stopped; friend received 1,982. Friend sent 1,746 by his last summary line; host received 1,750. | H:8216 to H:8915; F:8140 to F:8689 |
| Datagrams per 5 s | Host: median 157 in the six windows where both sides were at full rate, against 204 in the same kind of window in connection 1's phase B. One window at 194. Friend: 150 to 151, except 82 and 71 (below). | H:8347 to H:8783 |
| DATA3 | Nothing in either direction: no fills, no requests, no skips. Datagrams arrived in order. | H:8216 to H:8915; F:8140 to F:8689 |
| Freezes | None of 100 ms or more on either PC. | |
| Delivery delay | 20:25:59 to 20:26:18: friend → host datagrams arrived 4.4 s, then 9.0 s, then 9.4 s late, in order, none lost. The host got 27, 12, 140 and 426 per 5 s while the friend sent 151, 150, 150 and 150. The other direction was normal: the friend received 176, 168, 150 and 154. | H:8423 to H:8531; F:8329 to F:8438 |
| Unexplained | 20:26:23 to 20:26:33: both sent less (host 108 and 164, friend 82 and 71) with frames at 33 ms on both PCs. Three status pushes and an `updateUser` from each game fall in the same 10 s. An inferred reload or stall; the logs cannot say which. | H:8662, H:8709; F:8526, F:8574; H:8587-8676 |
| How it ended | The friend's game window was closed at 19:26:46.5 ± 1.0, friend's clock. No BYE was sent. The host's last datagram from him arrived at about 20:26:47.1 (inferred: 115 in the window ending 20:26:48.3). The host's game called `TerminateConnection` at 20:26:56.995, about 9.9 s later, on its kick path. | F:8690; H:8818, H:8880-8881 |
| Which side ended it | The host's game. The friend's process had already gone. | |

## Connection 3

20:27:55 to 20:32:22, 4 min 26 s. No log from the friend.

| Measure | Value | Where |
|---|---|---|
| Who asked | The friend's game, from a new start of the game (his address had port 42742; it was 42736 before). | H:9283 |
| Asking to ESTABLISHED | Friend's side: not in any log. Host: PEER_ACTIVATED, request and ESTABLISHED in one second, 20:27:55.7 ± 0.6. | H:9283-9288 |
| ESTABLISHED to guest in the Pod | Not shown. Stand-ins: status pushes from the friend 9, 13 and 18 s after ESTABLISHED; his game at 152 per 5 s in the window ending 20:28:13.5 (13 to 18 s after); the host's own rate above 190 from the window ending 20:28:18.5. | H:9356, H:9399, H:9439; H:9436, H:9474 |
| Path | Relay in all 53 lines. | H:9306 to H:11285 |
| RTT, host's samples | n = 53: median 450 ms, 95th 504 ms, worst 1,275 ms (one sample over 1 s, at 20:30:58.7). | same lines |
| Datagrams, totals | Host sent 11,906, of which 1,053 in the last 10 s, after the friend's game had stopped. Host received 7,454. | H:9306 to H:11344 |
| Datagrams per 5 s | In the 47 windows with both sides at full rate: host median 215, 95th 255; friend 150. | |
| DATA3, friend → host | Nothing: no fills, no requests, no skips. The other direction is unknown. | |
| Freezes | None of 100 ms or more on the host. The host's frames ran slow for most of this connection (see [slow frames](#slow-frames-on-the-host)). | |
| How it ended | The online server reported the friend offline at 20:32:12.6 ± 0.6. His last datagram arrived at about 20:32:12.7 (inferred: 117 in the window ending 20:32:13.8). The host's game called `TerminateConnection` at 20:32:21.994, about 9.3 s later, on its kick path. In those 10 s the host's game sent 670 and then 383 datagrams. | H:11238, H:11247, H:11285, H:11307-11308, H:11344 |
| Which side ended it | The host's game. What stopped the friend's game is not known. | |

## Connection 4

20:33:07 to 20:33:11, about 4 s.

| Measure | Value | Where |
|---|---|---|
| Who asked | The friend's game, from another new start (port 42746), 44 to 46 s after connection 3 ended. | H:11667 |
| ESTABLISHED | 20:33:06.9 ± 0.9 on the host. | H:11671 |
| Path, RTT | Relay. One sample: 445 ms. | H:11689 |
| Datagrams | 18 sent, 19 received. | H:11689, H:11760 |
| How it ended | The host's game called `TerminateConnection` at 20:33:10.760, 3.0 to 5.2 s after ESTABLISHED. Not the kick path: the stack has `eboot+0x34a67d` and lacks `0x37b4b6`. The friend's game kept sending; three of its datagrams were dropped. | H:11706-11707, H:11726-11729 |
| Which side ended it | The host's game. Why is not in the log. | |

## Connection 5

20:36:07 to 20:36:10, about 3 s.

| Measure | Value | Where |
|---|---|---|
| Who asked | The friend's game, same address as connection 4, 175 to 178 s after it ended. | H:12947 |
| ESTABLISHED | 20:36:07.2 ± 0.8 on the host. | H:12951 |
| Path, RTT | Relay. One sample: 464 ms. | H:12967 |
| Datagrams | 22 sent, 15 received. | H:12967, H:13035 |
| How it ended | The host's game called `TerminateConnection` at 20:36:10.494, 2.0 to 4.0 s after ESTABLISHED. A third call path (`eboot+0x67371f`, `0x6539a1`), not the kick. Six of the friend's datagrams were dropped afterwards. | H:12975-12976, H:12993-13000 |
| Which side ended it | The host's game. Why is not in the log. | |

## Connection 6

20:36:21 to 20:57:25, 21 min 4 s. No log from the friend.

| Measure | Value | Where |
|---|---|---|
| Who asked | The friend's game, 10 to 12 s after connection 5 ended. | H:13085 |
| Asking to ESTABLISHED | Friend's side: not in any log. Host: one second, 20:36:21.3 ± 0.6. | H:13085-13090 |
| ESTABLISHED to guest in the Pod | Not shown. Stand-ins: status pushes 4, 8 and 13 s after ESTABLISHED; the friend's game at 148 per 5 s in the window ending 20:36:34.1 (8 to 13 s after). | H:13126, H:13165, H:13210; H:13205 |
| Path | Relay in all 252 lines. | H:13113 to H:23991 |
| RTT, host's samples | n = 252: median 460 ms, 95th 664 ms, worst 2,430 ms. 16 samples over 600 ms, 6 over 1 s. | same lines |
| Datagrams, totals | Host sent 59,246 and received 36,032. | H:13113 to H:24048 |
| Datagrams per 5 s | In the 201 windows with both sides at full rate: host median 210, 95th 320, most 592; friend 150. | |
| DATA3, friend → host | 9 filled from copies (1 at 20:49:40, 8 in the last 15 s). 152 requests sent, all in the last 15 s. 250 skipped in three lines: 125 at 20:57:17.687, 28 at 20:57:19.699, 97 at 20:57:22.911. | H:19883, H:23953, H:23960, H:23975, H:23991, H:23998, H:24048 |
| DATA3, host → friend | The host sent 12 again in the window ending 20:37:04.2, in the middle of a level load. What the friend's link filled or skipped is unknown. | H:13540 |
| Wait behind a gap | At each skip the link had held datagrams behind missing ones for 2.0 s without catching up (`kGiveUpMs`, `p2p_link.cpp:35`, `:338-343`): since about 20:57:15.7, 20:57:17.7 and 20:57:20.9. The request counts fit: 20, 96 and 36 requests in the last three windows against 0.78 s, 3.80 s and 1.43 s of such waiting, one request per 39 to 40 ms. | H:23953, H:23991, H:24048 |
| Freezes | Host 9 windows, longest gap 412 ms, 2.4 s over. | table below |
| Level changes | Three with a `play` request, one return; see [joins and level changes](#joins-and-level-changes). | |
| Delivery delays | Five spells with an RTT sample of 1.0 to 2.4 s, and a shorter run of 600 to 1,000 ms samples. | [delays](#delivery-delays-on-the-network) |
| How it ended | From 20:57:06 the host received 109, 67, 70 and 3 datagrams per 5 s while sending 217 to 506. Of the friend's datagrams numbered 35938 to 36257, 250 never came and 70 did. The host's game called `TerminateConnection` at 20:57:25.215, on its kick path. That is 7.5 s after the first skip and 9.5 s after the link first held a datagram it could not deliver. The last datagram delivered in order (number 35937) arrived after 20:57:11.5 and before the first skip, so 7.5 to 13.7 s before the kick; the log cannot place it closer. | H:23916 to H:24048, H:24021-24022 |
| What else failed then | Nine of the host's HTTP requests to the online server got no answer between 20:57:34 and 20:58:31 (WinHTTP 12002). They are the only such failures in the host's log. Five are Web API requests, which time out after 10 s (`web_request.h:21`, `np_shadnet.cpp:412`), so the first was sent at about 20:57:25 (inferred). The other four went to the game server, which is the same address as the relay (H:2, H:532, H:23314). Push events from the server still arrived at 20:57:53.9. | H:24106, H:24191, H:24254, H:24281, H:24341, H:24425, H:24476, H:24506, H:24507; H:24240 |
| Which side ended it | The host's game. The HTTP failures are the host's own requests and do not pass through the friend, so they point at the host's line or the server rather than the friend's line (inferred). The friend's side of that minute is unseen. | |

## The six endings side by side

| Connection | Ended by | Call path in the stack | Time from the last usable datagram |
|---|---|---|---|
| 1 | Friend's game | `eboot+0x3aef25` among the frames | No silence: he ended it himself. |
| 2 | Host's game | `0x37b4b6`, `0x360555` | About 9.9 s (inferred) |
| 3 | Host's game | `0x37b4b6`, `0x360555` | About 9.3 s (inferred) |
| 4 | Host's game | `0x34a67d` | 3.0 to 5.2 s after ESTABLISHED; datagrams were still arriving |
| 5 | Host's game | `0x67371f`, `0x6539a1` | 2.0 to 4.0 s after ESTABLISHED; datagrams were still arriving |
| 6 | Host's game | `0x37b4b6`, `0x360555` | 7.5 to 13.7 s after the last in-order delivery; 7.5 s after the first skip |

The stacks are a scan of return addresses, not exact frames (`np_shadnet.cpp:1390-1397`). The three with `0x37b4b6` and `0x360555` are identical to each other (H:8882-8897, H:11309-11324, H:24023-24038). Those two addresses lie inside the functions the design note names for the game's 10 s limit, `eboot+0x37b3f0` and `0x3604a0` (`shadnet_design.md:362-363`). So connections 2, 3 and 6 are kicks (inferred from the matching addresses, and from the 9 to 10 s of silence in connections 2 and 3).

The link's own limit is 15 s of silence (`deadAfterMs`, `p2p_link.h:76`). The game's 10 s came first every time, so no connection ended on the link's limit.

## Freezes

A row is one `[video]` window of 30 frames that holds a frame gap of 100 ms or more, while a connection was up. The freeze lies somewhere inside the window. The friend's game had `async_shaders=0` in its `launcher.ini`. The host's log shows the same pattern of builds and gaps, so its builds were not in the background either (inferred).

### Host

| Conn. | Window starts | Window length, s | Longest gap, ms | Time over, s | Builds logged in the window | Line |
|---|---|---|---|---|---|---|
| 1 | 20:13:50.2 | 1.31 | 338 | 0.31 | 16 pipelines and 8 shaders built in 291 ms | H:2574 |
| 1 | 20:15:05.3 | 1.14 | 154 | 0.14 | none (75 new images: the host's Pod loading at the swap) | H:3268 |
| 1 | 20:16:12.5 | 1.35 | 384 | 0.35 | 14 pipelines and 7 shaders built in 361 ms | H:4033 |
| 1 | 20:16:37.8 | 1.52 | 550 | 0.52 | 10 pipelines and 10 shaders built in 521 ms | H:4226 |
| 1 | 20:16:51.3 | 1.54 | 572 | 0.54 | 10 pipelines and 10 shaders built in 547 ms | H:4333 |
| 1 | 20:18:13.0 | 1.26 | 294 | 0.26 | 5 pipelines and 5 shaders built in 260 ms | H:4941 |
| 1 | 20:18:14.3 | 1.29 | 318 | 0.29 | 5 pipelines and 5 shaders built in 288 ms | H:4950 |
| 1 | 20:18:31.6 | 1.43 | 465 | 0.43 | 9 pipelines and 9 shaders built in 432 ms | H:5077 |
| 1 | 20:18:34.1 | 1.45 | 487 | 0.45 | 9 pipelines and 9 shaders built in 457 ms | H:5094 |
| 1 | 20:18:49.5 | 1.54 | 574 | 0.54 | 11 pipelines and 11 shaders built in 539 ms | H:5202 |
| 1 | 20:18:51.0 | 1.61 | 645 | 0.61 | 13 pipelines and 12 shaders built in 618 ms | H:5212 |
| 1 | 20:18:56.6 | 1.58 | 618 | 0.58 | 11 pipelines and 11 shaders built in 591 ms | H:5250 |
| 1 | 20:19:06.2 | 1.61 | 648 | 0.61 | 12 pipelines and 12 shaders built in 615 ms | H:5318 |
| 1 | 20:19:08.8 | 1.31 | 342 | 0.31 | 6 pipelines and 6 shaders built in 319 ms | H:5332 |
| 1 | 20:20:40.0 | 1.11 | 134 | 0.11 | 2 pipelines and 2 shaders built in 107 ms | H:5987 |
| 1 | 20:22:52.2 | 1.22 | 258 | 0.22 | 10 pipelines and 5 shaders built in 234 ms | H:6989 |
| 1 | 20:23:25.4 | 1.09 | 133 | 0.09 | 2 pipelines and 2 shaders built in 103 ms | H:7236 |
| 4 | 20:33:10.3 | 1.16 | 189 | 0.16 | none | H:11733 |
| 6 | 20:36:54.0 | 1.10 | 137 | 0.10 | none | H:13427 |
| 6 | 20:36:55.1 | 1.73 | 412 | 0.73 | 11 pipelines and 10 shaders built in 603 ms | H:13471 |
| 6 | 20:36:58.9 | 1.34 | 356 | 0.34 | 6 pipelines and 6 shaders built in 322 ms | H:13501 |
| 6 | 20:37:54.5 | 1.31 | 341 | 0.31 | 6 pipelines and 6 shaders built in 307 ms | H:13978 |
| 6 | 20:37:58.8 | 1.08 | 110 | 0.08 | 4 pipelines and 3 shaders built in 51 ms | H:14021 |
| 6 | 20:39:36.8 | 1.22 | 139 | 0.22 | 3 pipelines and 3 shaders built in 165 ms | H:14847 |
| 6 | 20:40:42.0 | 1.17 | 132 | 0.17 | 3 pipelines and 3 shaders built in 175 ms | H:15381 |
| 6 | 20:42:13.6 | 1.30 | 287 | 0.30 | 6 pipelines and 6 shaders built in 309 ms | H:16144 |
| 6 | 20:42:43.1 | 1.16 | 142 | 0.16 | 3 pipelines and 3 shaders built in 131 ms | H:16400 |

### Friend (connection 1; none in connection 2)

| Window starts, friend's clock | Host's clock | Window length, s | Longest gap, ms | Time over, s | Builds logged in the window | Line |
|---|---|---|---|---|---|---|
| 19:13:57.0 | 20:13:57.3 | 1.21 | 246 | 0.21 | 8 pipelines and 4 shaders built in 231 ms | F:2682 |
| 19:15:15.6 | 20:15:15.9 | 2.00 | 975 | 1.00 | 16 pipelines and 18 shaders built in 943 ms | F:3377 |
| 19:15:18.6 | 20:15:18.9 | 1.17 | 208 | 0.17 | 3 pipelines and 3 shaders built in 195 ms | F:3395 |
| 19:16:12.6 | 20:16:12.9 | 3.12 | 1,811 | 2.12 | 38 pipelines and 45 shaders built in 2,035 ms | F:3915 |
| 19:16:15.8 | 20:16:16.0 | 3.13 | 1,051 | 2.13 | 48 pipelines and 26 shaders built in 2,219 ms | F:3933 |
| 19:16:37.8 | 20:16:38.1 | 4.58 | 3,456 | 3.58 | 45 pipelines and 49 shaders built in 3,644 ms | F:4090 |
| 19:16:42.4 | 20:16:42.7 | 1.76 | 565 | 0.76 | 10 pipelines and 17 shaders built in 832 ms | F:4106 |
| 19:16:44.2 | 20:16:44.5 | 1.25 | 299 | 0.25 | 3 pipelines and 3 shaders built in 287 ms | F:4114 |
| 19:16:48.4 | 20:16:48.7 | 2.12 | 704 | 1.12 | 12 pipelines and 12 shaders built in 1,208 ms | F:4146 |
| 19:16:50.6 | 20:16:50.8 | 2.65 | 1,099 | 1.65 | 17 pipelines and 17 shaders built in 1,724 ms | F:4153 |
| 19:17:03.2 | 20:17:03.5 | 1.11 | 133 | 0.11 | 6 pipelines and 7 shaders built in 152 ms | F:4242 |
| 19:17:05.3 | 20:17:05.6 | 1.08 | 122 | 0.08 | 1 pipeline and 1 shader built in 109 ms | F:4261 |
| 19:18:12.3 | 20:18:12.6 | 2.21 | 1,246 | 1.21 | 14 pipelines and 15 shaders built in 1,221 ms | F:4777 |
| 19:18:31.6 | 20:18:31.9 | 2.60 | 1,637 | 1.60 | 18 pipelines and 18 shaders built in 1,612 ms | F:4910 |
| 19:18:50.2 | 20:18:50.5 | 2.87 | 1,904 | 1.87 | 24 pipelines and 25 shaders built in 1,877 ms | F:5039 |
| 19:19:06.1 | 20:19:06.4 | 2.57 | 1,608 | 1.57 | 16 pipelines and 15 shaders built in 1,582 ms | F:5143 |
| 19:21:08.7 | 20:21:09.0 | 1.18 | 142 | 0.18 | 2 pipelines and 2 shaders built in 219 ms | F:6035 |
| 19:21:17.9 | 20:21:18.2 | 1.08 | 125 | 0.08 | 4 pipelines and 1 shader built in 115 ms | F:6101 |
| 19:22:49.0 | 20:22:49.3 | 1.13 | 170 | 0.13 | 2 pipelines and 2 shaders built in 159 ms | F:6786 |
| 19:22:52.2 | 20:22:52.4 | 1.74 | 425 | 0.74 | 15 pipelines and 10 shaders built in 790 ms | F:6813 |
| 19:22:56.9 | 20:22:57.2 | 1.44 | 454 | 0.44 | 14 pipelines and 17 shaders built in 472 ms | F:6851 |
| 19:23:26.3 | 20:23:26.6 | 1.21 | 191 | 0.21 | 3 pipelines and 3 shaders built in 251 ms | F:7062 |

**A frozen second costs 30 datagrams.** While the friend was the guest, each window in which his game sent fewer than 145 lines up with freeze time of the same size:

| Friend's windows ending (his clock) | Sent | Short of 150 by | Freeze time over in the same span |
|---|---|---|---|
| 19:16:40 to 19:16:55 | 94, 69, 121, 96 | 220 datagrams = 7.3 s | 7.4 s |
| 19:18:15 | 114 | 36 = 1.2 s | 1.2 s |
| 19:18:35 | 102 | 48 = 1.6 s | 1.6 s |
| 19:18:55 | 99 | 51 = 1.7 s | 1.9 s |
| 19:19:10 | 104 | 46 = 1.5 s | 1.6 s |
| 19:22:56 to 19:23:01 | 128, 138 | 34 = 1.1 s | 1.3 s |

(F:4085 to F:4176, F:4785, F:4924, F:5061, F:5157, F:6834, F:6871.) While the friend's game was frozen in connection 1, the host's game mostly sent more than its median of 207: 278, 389, 246, 266, 227, 305 and 300 per 5 s in seven of the matching windows, and 147 in the eighth (H:4248 to H:4369, H:4965, H:5109, H:5353, H:5211).

### Slow frames on the host

These are not freezes. The host's game took longer than 33.3 ms to make each frame ("work" in the `[video]` line), for many windows in a row, with no builds. The cause is not in the log. The table lists runs of three or more windows averaging 35 ms or more.

| From | To | Length, s | Median frame, ms | Slowest window, ms | Median work, ms | Lines |
|---|---|---|---|---|---|---|
| 20:24:48 | 20:24:56 | 9 | 40.3 | 42.8 | 40.3 | H:7850..7896 |
| 20:26:44 | 20:27:17 | 33 | 43.0 | 44.3 | 43.0 | H:8799..9025 |
| 20:27:29 | 20:28:01 | 32 | 39.1 | 45.6 | 39.1 | H:9123..9331 |
| 20:28:03 | 20:30:06 | 123 | 37.7 | 40.4 | 37.7 | H:9362..10191 |
| 20:30:50 | 20:31:36 | 46 | 38.8 | 43.8 | 38.8 | H:10690..10974 |
| 20:33:54 | 20:34:07 | 12 | 37.6 | 38.5 | 37.6 | H:12035..12096 |
| 20:47:12 | 20:47:16 | 4 | 36.0 | 37.8 | 36.0 | H:18658..18682 |
| 20:47:40 | 20:47:43 | 3 | 36.2 | 37.3 | 36.2 | H:18881..18897 |
| 20:57:28 | 20:57:31 | 3 | 35.2 | 35.4 | 35.2 | H:24071..24084 |
| 20:57:53 | 20:57:58 | 4 | 35.0 | 35.3 | 35.0 | H:24256..24275 |
| 20:58:20 | 20:58:50 | 31 | 36.9 | 41.8 | 36.9 | H:24441..24652 |
| 20:58:55 | 20:59:02 | 8 | 36.9 | 37.8 | 36.9 | H:24688..24732 |
| 20:59:13 | 20:59:16 | 3 | 36.6 | 37.4 | 36.6 | H:24809..24823 |
| 21:01:08 | 21:01:37 | 29 | 41.6 | 43.6 | 41.6 | H:25693..25865 |

So for about 2 min 55 s of connection 3's 4 min 26 s the host's game ran at 22 to 28 frames per second. The friend still sent 150 per 5 s through it. What the simulation did is not visible.

## Stalls

A stall is a stretch in which a game shows frames but its simulation does not step. **Neither log can show one.** The line that counts simulation steps per frame, `[sim]`, is printed only with `LBP3_TRACE_SIM` set (`videoout.cpp:578-607`), and the player-slot and sync lines only with `LBP3_SYNC_PROBE=1` (`core/sync_probe.cpp:199`). None of the three files has a `[sim]`, `[slots]`, `[sync]` or `[p2p-diag]` line.

What can be said is indirect, and all of it is inferred:

- A guest's game sends exactly 30 datagrams per second when it runs. Windows where the guest sent fewer, with no freeze to account for it, are stall or load candidates:
  - 19:26:26 and 19:26:31, friend's clock, connection 2: 82 and 71 sent, no frame gap over 40 ms. 147 datagrams short, about 4.9 s (F:8526, F:8574).
  - 19:16:15 and 19:16:20, connection 1: 29 and 86 sent. 185 short, about 6.2 s, of which 4.25 s were freezes (F:3903, F:3949). The rest falls on a scene change.
  - The joins and the role swap (below).
- The host, as session owner, has no fixed rate, so its log gives no such sign.
- During the 9.4 s delay of connection 2 the host's game got almost nothing from the friend for about 10 s and its frames held 33.3 ms (26 `[video]` windows from 20:25:54 to 20:26:20, none slow). Whether its simulation stepped is not known.

## Delivery delays on the network

Datagrams that arrived late but in order, with none missing. For connections 1 and 2 the transit time comes from both logs: the moment the host's count reached a number, minus the moment the friend's count of sent datagrams reached it. In steady windows that transit is 0.22 s each way (median of 139 and 140 windows).

| Conn. | Windows ending | RTT samples, host, ms | RTT samples, friend, ms | Host received per 5 s | Friend sent per 5 s | Transit, friend → host | Lines |
|---|---|---|---|---|---|---|---|
| 1 | 20:23:52.9 to 20:24:08.0 | 1,217, 3,371, 1,203, 460 | 1,333, 2,634, 3,080, 450 | 134, 93, 215, 174 | 164, 152, 150, 150 | 1.2, 3.2, 1.0 s | H:7436..7553; F:7239..7350 |
| 2 | 20:26:03.2 to 20:26:18.2 | 545, 5,062, 9,800, 455 | 525, 4,772, 10,000, 3,694 | 27, 12, 140, 426 | 151, 150, 150, 150 | 4.4, 9.0, 9.4 s | H:8423..8531; F:8329..8438 |
| 3 | 20:30:58.7, 20:31:03.7 | 1,275, 442 | no log | 138, 163 | no log | not measurable | H:10732, H:10764 |
| 6 | 20:37:19.2 | 1,000 | no log | 130 (level load) | no log | not measurable | H:13671 |
| 6 | 20:41:49.8 to 20:41:59.8 | 2,430, 1,840, 467 | no log | 102, 161, 198 | no log | not measurable | H:15945..16030 |
| 6 | 20:47:55.4, 20:48:00.5 | 1,027, 463 | no log | 133, 169 | no log | not measurable | H:18991, H:19031 |
| 6 | 20:49:00.6, 20:49:05.6 | 1,453, 459 | no log | 127, 175 | no log | not measurable | H:19541, H:19588 |
| 6 | 20:49:35.6, 20:49:40.6 | 2,144, 450 | no log | 98, 203 | no log | not measurable | H:19841, H:19883 |

- **Connection 1.** Just before the delay, both links filled gaps from copies in the same 10 s (host 3 then 5, friend 5 then 4) and the host sent its 12 requests (H:7326, H:7398; F:7129, F:7203). So the path lost datagrams both ways for a few seconds, then delayed the friend's by up to 3.2 s. During the delay the host's game sent 317, 337 and 432 per 5 s.
- **Connection 2.** No loss at all. The friend's PING took the full 10 s cap to come back (F:8402), and the host's 9.8 s (H:8494). Host → friend was unaffected.
- **Connections 3 and 6.** With only the host's log, the direction is inferred: each time the host received fewer than 150 in one window and the missing ones in the next (138 + 163, 133 + 169, 127 + 175, 98 + 203 all come to 301 or 302; 102 + 161 + 198 = 461). So the friend's datagrams came late, not never. Whether host → friend was also late is unknown.
- **Smaller ones.** Connection 6 has nine more RTT samples between 600 and 1,000 ms (20:40:04, 20:40:34, 20:40:44, 20:41:44, 20:42:59, 20:45:15, 20:46:00, 20:47:00, 20:51:50).

## Joins and level changes

Sums of the host's 5 s windows over each stretch. "Host sent" is session owner → guest except in the first row.

| What | When | Marker in the log | Length | Host sent | Host received | Lines |
|---|---|---|---|---|---|---|
| Join: the host into the friend's Pod (conn. 1, phase A) | 20:13:38 to 20:13:57 | ESTABLISHED to "Pod 2/4" | 17.5 to 19.5 s | 306 (15, 32, 76, 183) | 330 (15, 46, 110, 159); then 318 and 256 in the next two windows | H:2441..2622, H:2684, H:2722 |
| Role swap: the friend into the host's session (conn. 1, phase B) | 20:15:03 to 20:15:22 | First status change to "Pod 2/4" | 18 s | 1,634 (69, 864, 260, 441); then 263 | 334 (68, 70, 56, 140); then 149 | H:3281..3541 |
| Join: connection 2 | 20:25:36 to 20:25:53 | ESTABLISHED to both at full rate | 10 to 17 s | 330 (6, 52, 117, 155) | 277 (6, 29, 92, 150) | H:8216..8347 |
| Join: connection 3 | 20:27:55 to 20:28:13 | ESTABLISHED to both at full rate | 13 to 18 s | 342 (12, 21, 143, 166); then 279 | 277 (11, 20, 94, 152) | H:9306..9474 |
| Join: connection 6 | 20:36:21 to 20:36:34 | ESTABLISHED to both at full rate | 8 to 13 s | 292 (11, 109, 172); then 326 | 213 (12, 53, 148) | H:13113..13205 |
| Scene change (conn. 1) | 20:16:07 to 20:16:22 | 55 new images and a save on the host at 20:16:11; a status push from the friend at 20:16:13 | 15 s | 791 (139, 405, 247) | 266 (126, 35, 105) | H:3999, H:4015..4101, H:4026 |
| Level entered (conn. 6) | 20:36:44 to 20:37:19 | `POST …/play/developer/208330` at 20:36:56 | 35 s | 3,003 (274, 359, 219, 667, 471, 509, 504) | 483 (61, 117, 44, 48, 14, 69, 130) | H:13441, H:13361..13671 |
| Level entered (conn. 6) | 20:45:40 to 20:46:05 | `POST …/play/developer/208332` at 20:45:44 | 25 s | 1,455 (120, 327, 263, 298, 447) | 691 (91, 134, 158, 76, 232) | H:17919, H:17930..18110 |
| Scene change, probably back to the Pod (conn. 6) | 20:55:01 to 20:55:16 | `updateUser` at 20:55:07; four status pushes | 15 s | 724 (210, 167, 347) | 299 (106, 53, 140) | H:22811, H:22791..22898 |
| Level entered (conn. 6) | 20:56:26 to 20:56:41 | `POST …/play/user/82` at 20:56:33 | 15 s | 1,551 (592, 335, 624) | 241 (150, 43, 48) | H:23613, H:23584..23682 |

Notes:

- **Lengths are to the nearest 5 s window.** They measure how long the traffic looked unusual, not how long a loading screen was up. Loading screens are not logged.
- **"How much" is in datagrams only.** Bytes are not logged. One datagram in 3,000 has its size printed: the host's 34 samples run 5 to 313 bytes, 27 of them 159 to 190 (H:3319 to H:23930).
- **The role swap sent five times what a join sent** (1,634 against 292 to 342 datagrams). It is the only time the 864-in-5-s burst appears.
- **In connection 2 the host's rate never rose after the join.** It stayed at a median of 157 per 5 s, against 204 to 215 in the other long connections. This agrees with the first reading's "spawn signature".
- **What the friend's game received in the role swap:** 101, 470, 560, 383, 354 per 5 s (F:3237..3452), 1,868 in 25 s.

## Against the first reading

Checked against `rejoin_no_spawn.md` as it stood on 2026-11-15.

**Agrees.**

- Every connection was relayed; RTT about 450 ms (medians 453 to 460 ms here).
- 180 ms from the host to the server (179 to 180 ms in 30 ICMP replies).
- Connection 1 ended by the friend's game, BYE at 20:25:11.937. Connection 2: no BYE, the host's game ended it at 20:26:56.995. Connection 3: ended by the host's game at 20:32:21.994 after the friend went offline.
- Connection 2's link was gap-free: every DATA3 counter stayed still on both sides.
- The friend normally sends 150 per 5 s; his freezes are shader builds; `async_shaders=0`.
- The host's rate as a spawn signature: about 150 in connection 2, 170 to 280 in 1 and 3.
- The 20:36:49 to 20:37:19 example: 274 to 667 sent, 14 to 130 received.

**Differs or adds.**

1. **The table stops at connection 4.** The note knows there were six ("All 6 connections were relayed") and quotes two moments of connection 6, but its table has no row for connection 5 (20:36:07, ended by the host's game after 2 to 4 s) or connection 6 (20:36:21 to 20:57:25, 21 minutes, more than half of all connected time), and it does not say how connection 6 ended.
2. **Connection 1 began with the host as the guest.** For its first 85 s the host was in the friend's Pod; the friend became the guest at 20:15:03. The note describes connection 1 only as the friend joining the host.
3. **How connection 6 ended is new:** the only skipped datagrams of the session (250), a kick, and the host's HTTP requests to the server failing in the same minute.
4. **The 10 s delay began at about 20:25:59, not 20:26:03.** The low counts (27 and 12) are in the windows *ending* 20:26:03.2 and 20:26:08.2. Transit was 4.4 s by the first, 9.0 s by the second, 9.4 s by the third.
5. **More delays than the three listed.** The note has 20:23:52, 20:26:03 and 20:41:49. The host's log also has RTT samples over 1 s at 20:30:58 (connection 3) and at 20:37:19, 20:47:55, 20:49:00, 20:49:35 and 20:57:21 (connection 6). "Friend → host only" is measured for connections 1 and 2; after that it is inferred.
6. **Eleven quiet windows, not nine.** Between 19:15:20 and 19:23:01 the friend's game sent 29 to 121 in eleven windows (114, 29, 86, 94, 69, 121, 96, 114, 102, 99, 104), and fewer than 145 in thirteen.
7. **The host froze too.** 27 windows with gaps of 110 to 648 ms, 24 of them with builds. The note speaks only of the friend's freezes.
8. **Times one hour on.** The note gives the friend's `TerminateConnection` as 20:25:11.435. That is his 19:25:11.435 plus exactly one hour; on the host's clock it is about 20:25:11.70.
9. **Words.** The note calls the network delays "one-way stalls". In the glossary a stall is the simulation not stepping, which these logs cannot show. They are delivery delays here.
10. **"About 10 s"** before the host's game ends a silent connection: about 9.9 s and 9.3 s in connections 2 and 3 (inferred from window counts).

## What the logs cannot show

| Not measurable from these logs | Why | Log line that would show it |
|---|---|---|
| Stalls: whether the simulation stepped | No `[sim]` line without `LBP3_TRACE_SIM`; it prints 60 digits per line, too much for play. | A line every 5 s while online: frames shown, simulation steps, frames with no step, longest run of frames with no step. |
| When the guest spawned, and each player's join state | No `[slots]` line without `LBP3_SYNC_PROBE=1`. | A stamped line whenever a player slot changes state (the slot table at `eboot+0x130d5c0`, per `rejoin_no_spawn.md`). |
| Exact connect times | "connecting to", "the server says", "connected" and the signaling events carry no time. | The stamp on all of them, and at "connected": milliseconds since Activate, HELLOs sent direct and through the relay, and which one was answered. |
| The friend's wait in connections 3 to 6, and anything else on his side | His launcher kept two game logs; the report holds 19:10 to 19:26 only. | The report should carry every game log of the day, or the launcher should keep more than two. |
| The RTT's spread | One sample per 5 s (the last PONG), stale when PONGs stop, capped at 10 s. | Per window: PINGs sent, PONGs received, minimum, median and maximum RTT. |
| Delay in each direction | An RTT is a round trip, and the two PCs' clocks differ. Here it took both logs and steady traffic to get 0.22 s each way. | PONG carrying the answerer's wall clock and how long it held the PING; the link then logs its estimate of the clock offset and of each one-way delay. |
| Loss in each direction | The sender does not log packets sent (new, repeats of the last one, sent again); the receiver does not log packets received or doubles. "Filled from copies" also counts a datagram that merely arrived out of order. | Per window, both sides: DATA3 packets sent by kind, packets received, doubles, arrivals out of order. |
| Requests in each direction | "Sent again of N asked" puts this side's answers next to this side's own requests. | Four separate numbers: requests sent, requests received, datagrams asked for, datagrams sent again. |
| How long delivery waited behind each gap | Only a skip is logged, and only at 2 s. | Per gap: when it opened, how many were missing, how it closed (copy, sent again, repeat of the last, skip) and how long. Per window: the longest wait and the most datagrams held. |
| The longest time the game got nothing | "Received" counts arrival at the link, not delivery to the game. The game's 10 s limit watches delivery. | Per window: datagrams delivered, and the longest gap between two deliveries. A warning line when that passes 2 s and 5 s. |
| Bytes | Counts only; one size in 3,000. | Per window: bytes sent and received, and the largest datagram. |
| Why the game ended a connection | The stack is logged, the reason is not. Connections 4 and 5 ended after 3 to 5 s with nothing else in the log. | At `TerminateConnection`: seconds since ESTABLISHED, seconds since the last delivery, and the player slot's state. |
| What the game netcode did with a datagram | The eboot's own accept and reject decisions and its resends are invisible. The host's rate rising to 400 to 670 per 5 s when the friend goes quiet looks like resends (inferred). | Out of the link's reach; `LBP3_SYNC_PROBE`'s slot lines (state, seconds in it, strikes) are the nearest thing. |
| Whether any direct packet was ever tried or heard | The link logs a direct address only when it changes. | Per connection: the direct addresses tried, PINGs sent to them, and whether anything came back. |
| Each player's leg to the relay during play | The game's ICMP pings stop early (last at 20:13:21 and 19:13:13). | The round trip of the link's own 5 s STUN ping, and a line when a STUN answer is more than 10 s overdue. |
| Where the loss at 20:57 was | One log, one path. | The friend's log for that minute; the STUN line above on both sides. |
| Presence text after the first two minutes | `np_shadnet.cpp`'s `Log()` prints only the first 40 calls of each function (`:97-113`). The host's last presence answer is at 20:15:23 (H:3504). Push events carry no text. | A stamped line whenever a player's status text or session owner changes. |
| Loading screens, the "away" mark, the death countdown | Nothing in the port sees them. | Not known how; the slot lines may be enough to infer them. |

## Not determined

- Whether the friend spawned in connections 3 and 6.
- Why the friend's game ended connection 1, and what stopped his game at 20:32:12.
- Why the host's game ended connections 4 and 5 within seconds.
- What held the friend's datagrams for up to 9.4 s in connection 2. His game, his frames and the other direction were all normal.
- Whether host → friend delivery was ever late or lossy after 20:26:46.
- Whether the end of connection 6 was the host's line or the server. The host's own HTTP failures point at one of those two (inferred); the friend's side is unseen.
- Why the host's frames ran slow from 20:26:44 to 20:31:36.
- The time from pressing Join to the game asking for a connection. It is before anything the link logs.

## Method note

The numbers were taken with throwaway Python scripts run outside any repository (`python -I`, the friend's zip extracted into its own empty folder). They are not kept. Everything above can be redone from the two logs with the rules in [How the numbers were taken](#how-the-numbers-were-taken).
