# Why did no connection with the friend go direct?

Research for [issue 02](../../.scratch/netcode/issues/02-why-no-direct-path.md) of the [online co-op netcode map](../../.scratch/netcode/map.md). Reading only: nothing was built or run, and nothing was sent to the online server or to any address in the logs. The only web use was fetching the RFCs.

**Sources and method.**

- **Host log:** `D:\Recomps\lbp3-recompiled\analysis\play_log.txt` (15 November, 20:10 to 21:01). Cited as "host log N" (line N).
- **Friend's report:** `analysis\online\friend_report_20261115_192711.zip`. It holds his 19:10 to 19:26 launch (`play_launcher_log.1.txt`, "friend log.1 N") and a 7 s launch (`play_launcher_log.txt`, "friend log N"). His two later launches are not in it.
- **Clocks.** His game ended connection 1 at 19:25:11.435 (friend log.1 7819) and the host got the BYE at 20:25:11.937 (host log 8007). So the host's clock is one hour and about 0.3 s ahead of his. Times below are host time. A time with `~` is on a line without a stamp and comes from the frame counters around it (within about a second).
- **Code:** `runtime/src/net/p2p_link.cpp` and `p2p_link.h` in `D:\Recomps\lbp3-recompiled` at `519f220` (v0.1.5), cited as `p2p_link.cpp:N`; `runtime/src/hle/np_shadnet.cpp` at the same commit.
- **Server:** LBPPC's patch `server/linux/shadnet/patches/lbppc-shadnet.patch` (clone `D:\Recomps\lbp3-wt\_ref\LBPPC` at `951fb8d`) and shadNet's source (clone `D:\Recomps\lbp3-wt\_ref\shadNet` at `b38cf17`). Which commit the live server runs isn't known.
- **The host's own NAT:** 57 logs of two test instances on the host's PC (`D:\Recomps\lbp3-wt\coop\A`, `B`, `C`: `stderr_*.txt`; 13 November, and 15 November from 21:08, after the session) and five single-instance logs in `lbp3-recompiled\analysis`.
- **Standards:** RFC 4787, RFC 5128, RFC 5780, RFC 6598 and RFC 7422, for the terms only.
- **Privacy.** Public addresses are masked to two octets. The host's two public addresses are called P1 (13 November to the early hours of the 15th) and P2 (the evening of the 15th); both are `H.H.x.x`. The friend's is `F.F.x.x`. The online server's UDP service is `179.236.x.x:31314`.
- **Not done:** no WHOIS or location lookups of any address, no traceroute, no test against the online server.

Each section keeps **Observed** (a log or code line says it) apart from **Inferred** (a conclusion drawn from those lines).

## Headlines

- **No direct packet arrived.** Observed on the host in all six connections (461 of 461 5-second summaries say "relayed") and on the friend in the two connections his log covers (153 of 153). For his side of connections 3 to 6 there is no log; there it is inferred.
- **The link's rules did not hold the connections on the relay.** By the code, both links kept sending a probe a second to every direct address they knew, for as long as each connection lasted, and one direct packet would have switched the path. The link's own test covers that switch, and an older build is seen doing it between two instances on the host's PC.
- **Both sides knew the right addresses.** Each had the other's address as the server sees it and the other's LAN address. Nothing was missing or stale.
- **Inferred cause: a NAT that gives the link's socket a different mapping toward the other player than toward the server, together with a filter on the other side that only lets in the exact address and port it has sent to.** The logs cannot name that NAT with certainty, because the link has a single reflector (the online server). The evidence ranks the friend's side first (section 6). A plain drop of inbound UDP somewhere can't be excluded either.
- **The host's NAT reuses its mapping for a second destination and lets nothing in until the host has sent first.** Both are seen in 18 two-instance test pairs on the host's PC, 10 of them on the same evening and the same public address as the session, but only for a destination that loops back through the NAT. The host's mapped ports also sit in one block of about 1,000 ports per public address. Inferred from that: the provider shares the address among customers.
- **The friend's build (v0.1.4) has the same link code as the host's (v0.1.5).** Byte for byte.
- **One log line would decide it next time:** the link's mapped address as seen by a second reflector, printed beside "the server sees us at" (section 7).

## 1. What each side knew, and what was sent

### Where the addresses come from

- **"The server sees us at".** The link pings the server's UDP service every 5 s (`p2p_link.cpp:757-765`). The server answers with the source address and port of that datagram and keeps it per player (shadNet `src/stun_server.cpp`, `HandleStunPing`). The link logs the answer only when it changes (`p2p_link.cpp:693`).
- **"The server says".** When the game activates a connection, the link asks the server where the peer is (`p2p_link.cpp:777-782`, `np_shadnet.cpp:1220-1224`, `shadnet_service.cpp` `ResolvePeer`). The server answers with the address it kept from that player's pings (shadNet `src/cmd_matching.cpp:1187-1247`). That address becomes the first direct address and the relay's destination (`p2p_link.cpp:831-832`).
- **HELLO and ACK** carry the sender's address as the server sees it, and its LAN address with the link's local port (`p2p_link.cpp:264-277`). The receiver adds both to the addresses it tries (`p2p_link.cpp:569-570`).
- **The LAN address in the server ping is not used by the server.** `HandleStunPing` reads only the online ID. So a LAN address travels only in HELLO and ACK.

### Observed: each side's own addresses

| Side | Launch | Local UDP port | LAN address | The server saw | Source |
|---|---|---|---|---|---|
| Host | 20:10 | 56048 | 192.168.100.91 | `H.H.x.x:9358` (P2), one line for the whole session | host log 532, 556 |
| Friend | A, 20:10 (his 19:10) | 65524 | 192.168.0.160 | `F.F.x.x:42736`, one line for the whole launch | friend log.1 422, 485 |
| Friend | B, 20:26:49, 7 s | 64597 | 192.168.0.160 | `F.F.x.x:42741` | friend log 424, 485; `launcher_log.txt` 14-16 |
| Friend | C, online ~20:27:25 | not in any log | not in any log | `F.F.x.x:42742` | host log 9081, 9283 |
| Friend | D, online ~20:32:36 | not in any log | not in any log | `F.F.x.x:42746` | host log 11437, 11667 |

One "the server sees us at" line per launch means the mapping toward the server never changed during that launch.

### Timeline per connection

"Activated first" is the side whose game called `sceNpSignalingActivateConnection` before any packet from the other. The other side's game also activates, but only in answer to PEER_ACTIVATED (event 10), which the link raises when the first HELLO arrives (`p2p_link.cpp:479`, `np_shadnet.cpp:1278-1288`).

**The link logs no send.** Every HELLO and probe cell below is inferred from the code and the times: a HELLO every 500 ms to each direct address, the relay added after 4 s (`p2p_link.cpp:783-788`), then a PING every second through the relay and to each direct address (`p2p_link.cpp:804-810`).

| # | Host time | Activated first | Address it started with (observed) | Direct HELLOs before the relay (inferred) | Came up (observed) | Direct probes afterwards (inferred) | Direct packets handled (observed) |
|---|---|---|---|---|---|---|---|
| 1 | ~20:13:34 to 20:25:11.9 | Host (host log 2358-2359) | Server says the friend is at `F.F.x.x:42736` (host log 2361) | Host to `F.F.x.x:42736`, 8 to 10. The friend sent nothing: his link didn't know of the connection yet. | Relayed, 4 to 6 s after activation (host log 2392; friend log.1 2379-2385) | For 693 s, one a second each: host to `F.F.x.x:42736` and `192.168.0.160:65524`; friend to `H.H.x.x:9358` and `192.168.100.91:56048` | None. Host 138 summaries, friend 139, all "relayed" |
| 2 | ~20:25:33 to 20:26:57.0 | Friend (friend log.1 8065-8066) | Server says the host is at `H.H.x.x:9358` (friend log.1 8067) | Friend to `H.H.x.x:9358`, 8 or 9. The host sent nothing until the relayed HELLO. | Relayed, 3 to 5 s after activation (friend log.1 8095; host log 8194) | The same four addresses, about 70 s (his game closed at 20:26:46 without a BYE; the host's link went on until its game ended the connection, host log 8881) | None. Host 16, friend 14, all "relayed" |
| 3 | ~20:27:56 to 20:32:22.0 | Friend (host log 9283-9286: event 10 first) | His side isn't logged | By the code, friend to `H.H.x.x:9358` | Relayed; he reports `F.F.x.x:42742` (host log 9283) | Host to `F.F.x.x:42742` and his LAN address (its port isn't in a log); friend to the host's two addresses; about 265 s | Host: none, 53 summaries. Friend: no log |
| 4 | ~20:33:07 to 20:33:10.8 | Friend (host log 11667-11670) | Not logged | As 3 | Relayed; `F.F.x.x:42746` (host log 11667) | About 4 s | Host: none, 1 summary |
| 5 | ~20:36:07 to 20:36:10.5 | Friend (host log 12947-12950) | Not logged | As 3 | Relayed; `F.F.x.x:42746` (host log 12947) | About 3 s | Host: none, 1 summary |
| 6 | ~20:36:21 to 20:57:25.2 | Friend (host log 13085-13088) | Not logged | As 3 | Relayed; `F.F.x.x:42746` (host log 13085) | About 1,264 s | Host: none, 252 summaries |

- **The addresses were right.** The address the host held for the friend (`F.F.x.x:42736`) is the one the friend's own log reports from the server (friend log.1 485). The address the friend held for the host (`H.H.x.x:9358`, friend log.1 2379 and 8067) is the one in the host's log (host log 556). Relayed packets also arrived marked with those same sender addresses (host log 11726; friend log.1 7836).
- **RTT on the relay:** the median is between 445 and 465 ms in every connection, on both sides (the 5-second summaries).

## 2. Did any direct packet ever arrive?

**Observed: no, on the host in all six connections and on the friend in the two his log covers.**

- **At connection time.** All six "connected" lines on the host say "relayed" (host log 2392, 8194, 9283, 11667, 12947, 13085), and both on the friend (friend log.1 2379, 8095). The line says "direct" when a direct packet from that player has been handled by then (`p2p_link.cpp:480-481`).
- **During the connections.** The path word in each 5-second summary is "relayed" unless a direct packet from that player was handled in the 5 s before it (`p2p_link.cpp:216-226`, `:719-721`). Host: 461 summaries name the friend, 461 say "relayed", 0 "direct". Friend: 153 of 153 say "relayed". Within a connection the summaries are 5.000 to 5.021 s apart on both sides, so at most 21 ms per interval is unobserved.
- **Game datagram lines.** Every "game datagram N to/from" line says "relayed": 39 on the host, 12 on the friend.
- **No connection died of silence.** Neither log has a "nothing heard for 15 s" line (`p2p_link.cpp:796-803`).

**What this evidence does not cover.**

- **"Its packets now come from" is no evidence either way.** The link prints it only when the direct source changes from an earlier one (`p2p_link.cpp:395`). A first direct packet prints nothing. There are no such lines in either log, and that alone would prove nothing.
- **Packets that were dropped before the socket** (by a NAT or a firewall) leave no trace in any log.
- **The friend's side of connections 3 to 6** has no log. Inferred: nothing direct reached him either. Had his link handled a direct packet, it would have sent everything to that address for the next 5 s and nothing through the relay (`p2p_link.cpp:243-249`). The host handled nothing direct, so the host would have lost about 5 s of his traffic each time. The host's log has only two windows with nothing received while he was named, 20:26:53 and 20:32:18 (host log 8858, 11285), and both come after his game had closed (host log 8813, 11238). A shorter loss split across two windows can't be ruled out from the host's log.

## 3. What the server-seen addresses say about each NAT

Terms are RFC 4787's. A NAT has an **endpoint-independent mapping** when it reuses one external port for everything a socket sends, whatever the destination (§4.1; REQ-1 asks for it). Otherwise the mapping depends on the destination's address, or on its address and port. **Filtering** says who may send in through a mapping: anyone, only addresses the socket has sent to, or only address-and-port pairs it has sent to (§5). **Port preservation** means the external port equals the local one (§4.2.1).

Hole punching, which is what the link's probes do, "relies on the properties of EIM-NATs" (RFC 5128 §3.3): each side sends to the mapping the server saw for the other, and that only works if the other's NAT uses that same mapping toward this side.

**The online server is a single reflector.** It has one UDP socket (shadNet `src/stun_server.cpp`, `Start`). Telling a NAT's mapping behaviour takes answers from two addresses (RFC 5780 §4.3). So the session's logs cannot classify either NAT directly. What follows is the indirect evidence.

### The host's NAT

**Observed, session log.** Local port 56048, external port 9358 (host log 532, 556). No port preservation. One external port toward the server for 50 minutes.

**Observed, 58 sockets in all the logs on the host's PC** (13 to 15 November, the session's among them).

- **Ports come from one block per public address.** On P1, 37 sockets got ports from 31281 to 32250. On P2, 21 sockets got ports from 9119 to 10054 (the session's 9358 among them). No socket kept its local port. Within the block the ports are scattered, not in sequence: two sockets opened together got 31918 and 32062 (`coop/A/stderr_run2.txt`, `coop/B/stderr_run2.txt`).
- **The mapping is reused for a second destination.** In the two-instance tests, instance B sends to instance A's public address, and the packet loops back through the NAT (hairpinning, RFC 4787 §6). A then logs where B's direct packets come from. In all 18 pairs that logged a public source (16 of them in both directions), the source is the other instance's public address with exactly the port the server saw for it. Example: `coop/A/stderr_rj1.txt` (15 November, 21:08, P2): the server saw B at `H.H.x.x:9179`, and A logs "its packets now come from `H.H.x.x:9179` (its public address)". The label "(its public address)" is printed only when the source equals the address the peer reports from the server (`p2p_link.cpp:398`). The pairs: `d1`, `d3`, `t0`, `t1`, `t2`, `r10`, `m1`, `stderr` (13 November, P1; the last two in one direction only) and `rj1` to `rj5`, `rf1`, `rw1` to `rw3`, `rxB` (15 November, 21:08 to 22:48, P2). No pair shows a public source on another port.
- **Nothing gets in until the host's socket has sent first.** In every pair, B activates and sends HELLOs to A's public address for 4 s, yet A's first HELLO is the relayed one: A logs "connected (relayed" (for example `coop/A/stderr_rj1.txt` 2271, B activated ~4 s earlier at `coop/B/stderr_rj1.txt` 1533). Once A has sent probes to B's public address, B's direct packets arrive at once.
- **The opening stays for at least a minute.** In `rj1` the first connection ended at 21:09:04 and B activated again ~61 s later. This time A logged "connected (direct" (`coop/A/stderr_rj1.txt` 3188): B's direct HELLO passed. The same after 25 s in `rw3` (`coop/A/stderr_rw3.txt` 4517).

**Inferred.**

- **The host's NAT has an endpoint-independent mapping and a filter of the "send first" kind.** The hairpin tests show it for a destination on the NAT's own public address. They don't show it for a destination out on the internet, and they can't tell an address filter from an address-and-port filter (the test peer always had one address and port).
- **The host is behind address sharing by the provider.** 58 of 58 ports fall inside one window of under 1,000 ports per public address, and each window fits one slot of 1,008 ports on a grid from port 1024: 31264 to 32271, and 9088 to 10095. RFC 7422 §2.3 describes carrier-grade NATs that give each customer one contiguous range of (65536 − 1024) ÷ ratio ports; a ratio of 64 would give 1,008. The fit to that grid is this note's, not something a log states. The public address also changed between 00:51 and 20:10 on the 15th. Not confirmed: the router's own WAN address wasn't read. If it differs from P2 (for instance an address in 100.64.0.0/10, RFC 6598 §7), the sharing NAT is the provider's, and a port opened on the home router would not be reachable from outside.

### The friend's NAT

**Observed.**

- **No port preservation:** 65524 became 42736, and 64597 became 42741.
- **Ports rise from launch to launch:** 42736, 42741, 42742, 42746 over 22 minutes. Each launch is a new local socket.
- **The count between launches.** The ports and the differences are from the logs. The third column is from the code (sections 1 and 4), since sends aren't logged.

| Launch | Server saw | Where its socket sent (from the code) | Next launch's port is higher by |
|---|---|---|---|
| A (16.5 min, connections 1 and 2) | 42736 | The server; the host's public address; the host's LAN address | 5 |
| B (7 s, no connection) | 42741 | The server only | 1 |
| C (about 5 min, connection 3) | 42742 | The server; the host's public address; the host's LAN address | 4 |
| D (connections 4 to 6) | 42746 | The same three | no later launch |

**Inferred. These ports come from different sockets, so they cannot show what one socket gets for a second destination.** Two readings fit, and neither closes:

- **A mapping per destination, handed out in sequence.** It predicts 1 after launch B (seen). It predicts 3 after launches A and C: the server, the host's public address, the host's LAN address (which is not on his network, so it leaves through his router too). Seen: 5 and 4. Launch A fits if the two peer mappings were made again for connection 2 after 21 s of silence. Launch C leaves one port unexplained.
- **One mapping per socket, and a counter shared with other traffic.** Then the other 4, 0 and 3 ports went to other programs or devices behind the same NAT. Nothing contradicts that, and nothing supports it.

Either way the allocator counts upward, which RFC 5128 §3.5 calls the common case ("most NATs assign mapping ports in sequence").

**Not known:** what kind of line the friend is on. His report doesn't say, and address lookups were out of bounds.

## 4. Did the link's own rules keep it on the relay?

No rule did. Each one, with what it did on 15 November:

- **The 4 s before the relay (`relayAfterMs`, `p2p_link.h:76`).** It delays the relay, not the direct path. Direct HELLOs go out from the first tick and continue until an ACK comes (`p2p_link.cpp:783-787`). Cost here: every connection took 3 to 6 s to come up, and the direct HELLOs in that time got no answer. In connections 2 to 6 they could not have: the host's NAT lets nothing in before the host has sent (section 3), and the side that didn't activate sends nothing until the relayed HELLO arrives.
- **Only one side activates first.** A link creates a connection either when its game activates it (`p2p_link.cpp:960-968`) or when a HELLO for it arrives (`p2p_link.cpp:541-551`). So the second side starts probing only after the relay has delivered the first HELLO, about 4 s in. From then on both sides probe. This makes the relay the way the two sides learn to start, which is how hole punching is meant to work (RFC 5128 §3.3: both learn the other's address through the server, then both send). It did not prevent it.
- **Probes after the relay works.** Every second, while no direct packet has come in the last 5 s, the link sends a PING through the relay and one to every direct address it holds (`p2p_link.cpp:804-810`): the server's answer, the two addresses from HELLO or ACK, and any address a direct packet has come from (`p2p_link.cpp:390`). There is no limit and no back-off. In connection 1 that is about 690 probes per address in each direction.
- **Could a probe switch the path? Yes.** Any direct packet from the player sets the direct address at once: HELLO and ACK are matched by name (`p2p_link.cpp:540`), PING and PONG by name (`:657`), game datagrams by a tag of the name (`:584-585`, `:636-637`), not by the address they come from. A direct PING is answered directly (`:663-664`), so the other side then hears a direct PONG. That is read from the code at `519f220`. The link's test covers it for a LAN address: in section 6 of `runtime/tests/p2ptest.cpp` (`:412-441`) the server-seen addresses reach nobody, the connection comes up through the relay, and the test then waits until neither side is relayed (`:430`). No test does it for a public address behind a NAT. Seen once in a log, on a 13 November build older than v0.1.0: `coop/A/stderr_d1.txt` comes up relayed, logs "its packets now come from … (its public address)", and then has 89 summaries saying "direct".
- **A source on an unexpected port is accepted too.** Because packets are matched by name, a peer whose NAT uses another port toward us is still recognised, and its address is added to the ones we send to (`p2p_link.cpp:390`, `:399`). So a NAT with a mapping per destination on one side does not by itself stop the direct path. It stops it only if the other side drops packets from a port it hasn't sent to.
- **The 5 s a direct packet counts as fresh (`kDirectFresh`, `p2p_link.cpp:39`).** It played no part: there never was a direct packet. A hazard for later, not seen here: if direct packets arrive one way only, the receiving side sends everything directly and nothing through the relay (`p2p_link.cpp:243-249`) and stops probing (`:809`), while the other side hears nothing from it.
- **The `Auto` path setting.** Its "LAN first" rule applies only when the peer reports the same public address as ours (`p2p_link.cpp:235-239`). Here they differ (`H.H.x.x` and `F.F.x.x`), so the rule was idle. `LBP3_P2P_PATH` wasn't set on either side: neither log has the line `np_shadnet.cpp:1198` would print.
- **The LAN address of a player on another network** still gets a probe a second (`p2p_link.cpp:810`). It reaches nobody. On a NAT with a mapping per destination it costs one more mapping.

**What the link lacks** (absent features, not rules that blocked):

- It only sends to addresses it was told or has heard from. It never tries neighbouring ports (RFC 5128 §3.5 describes that trick and how fragile it is).
- It asks the router for nothing: `runtime/src` has no port mapping code (a search for UPnP and NAT-PMP finds none).
- It has one reflector, so it cannot tell what kind of NAT it is behind, and it logs no sends.

## 5. The friend's build

**Observed.**

- His `launcher.ini` has `settings_version=2` and `my_unlocks=0`, and no `bloom_1440`. Only v0.1.4's launcher writes that set: `settings_version` exists from v0.1.4 (`runtime/launcher/launcher_settings.cpp:81` at `v0.1.4-lbp3port`), `bloom_1440` was last written by v0.1.3 (`:86` there), and `my_unlocks` is gone in v0.1.5 (`git diff v0.1.4-lbp3port v0.1.5-lbp3port`).
- His game log has "[p2p] exact maths: on" (friend log.1 421), a line that exists from v0.1.3. It has no "[natives] IsOnline__" line. v0.1.5 hooks that native unless `LBP3_MY_UNLOCKS=off` (`runtime/src/hle/my_unlocks.h:14-15` at `v0.1.5-lbp3port`), and the host's log has the line (host log 90).
- `p2p_link.cpp`, `p2p_link.h` and `np_shadnet.cpp` are the same blobs in v0.1.3, v0.1.4 and v0.1.5 (`64f3419`, `e652a7a`, `ced95e3`), and nothing under `runtime/src/net` differs between v0.1.3 and v0.1.5 (`git diff --stat`).

**Inferred:** he ran v0.1.4 (the launcher starts the `lbp3.exe` next to it: `build=this`). **His link behaves exactly as the host's.** The older build explains nothing here.

## 6. The cause

**Observed core.** Nothing direct came through in either direction (section 2). By the code, for 693 s in connection 1, and again in the others, each socket sent a packet a second to the address the server had seen for the other, so each side's NAT had been "sent to first". Between two sockets behind the host's NAT, direct packets arrive in the same second the connection comes up (`coop/A/stderr_rj1.txt` 2271 and its "now come from" line at 21:08:31.755; that run had the relay forced, so its traffic stayed on the relay).

**Inferred.** At least one NAT did not use, toward the other player, the mapping the server saw. And the other side dropped what came from the unexpected port (section 4: the link itself would have accepted it). Ranked:

1. **The friend's NAT gives a mapping per destination, and the host's side filters per address and port.** Most likely.
   - The host's NAT reuses its mapping in every one of 18 hairpin tests, 10 of them on the session's evening and public address. So the host's packets very probably reached the friend's NAT from `H.H.x.x:9358`, the address the friend was sending to. A NAT that reuses its mapping would have let them in.
   - Connection 2 began 21 to 25 s after the host's last probe to `F.F.x.x:42736`. On the host's NAT an opening outlived 25 s and 61 s in the tests. Yet the friend's direct HELLOs still didn't arrive. (Weak point: in the tests the earlier traffic had flowed both ways; here it never had, and a NAT may forget such an opening sooner.)
   - The friend's port count fits a mapping per destination better than not (section 3), without closing.
2. **The host's NAT gives a mapping per destination toward outside addresses, and the friend's side filters per address and port.** Possible only if the host's NAT treats a looped-back destination differently from an outside one.
3. **Both.**
4. **Something drops inbound UDP that is not a mapping matter** (a firewall on a PC or router, or a provider). Nothing in the logs points to it and nothing excludes it.

**Shares.**

- **The friend's side: the main share** under candidate 1, by inference. Not proven.
- **The host's side: a necessary share under candidate 1.** If the host's side let in packets from any port of an address it had sent to, the link would have picked up the friend's new source address and gone direct. The host's address sharing (inferred) also limits the remedies: opening a port on the home router may not help.
- **The link: no share in holding the path on the relay.** Its share is what it lacks: no way across a NAT with a mapping per destination, a single reflector, and no log of what it sends or of a first direct packet. That last gap is why this ticket ends with a ranking.

## 7. What the logs cannot decide, and the lines to add

**The deciding line.** Ask a second reflector for the link socket's mapped address and print it beside the server's:

```
[p2p] nat: the server sees us at H.H.x.x:9358; <second reflector> sees us at H.H.x.x:9358 (one mapping: endpoint-independent)
[p2p] nat: the server sees us at F.F.x.x:42742; <second reflector> sees us at F.F.x.x:42743 (a mapping per destination)
```

- Print it once after the first server reply, and again at each "connected".
- It has to be sent from the link's own socket, next to the server ping (`p2p_link.cpp:757-765`), and its answer read where the server's is (`:689-697`).
- With that line in both players' logs, candidates 1, 2 and 3 are told apart at start-up, before any connection.
- The online server can't be the second reflector: it has one UDP address and port. Any server that answers an RFC 5389 Binding Request would do. Which one is a choice for the path-ladder ticket, since it shows the player's address to that service.

**Lines that show what the link tried and what arrived.** The values are made up to show the format.

```
[p2p] 20:27:55.9 <peer>: HELLO from F.F.x.x:42742 (relayed): it reports public F.F.x.x:42742, LAN 192.168.0.160:61234
[p2p] 20:27:55.9 <peer>: connected (relayed) after 4.6 s: 9 HELLOs to H.H.x.x:9358, 1 through the relay; no direct packet yet
[p2p] 20:28:03.4 last 5 s: ...; <peer> (relayed, rtt 448 ms; direct: 5 probes to F.F.x.x:42742, 5 to 192.168.0.160:61234, heard 0)
[p2p] 20:28:04.1 <peer>: first direct packet, from F.F.x.x:42743 (it reports F.F.x.x:42742)
[p2p] 20:31:10.0 <peer>: no direct packet for 5 s: back to the relay
```

- HELLO or ACK received: in the handler at `p2p_link.cpp:552-571`, the first time and when a value changes.
- Counts of HELLOs: kept per peer at `p2p_link.cpp:786-787`, printed at `:480`.
- Probes sent and direct packets heard, per address: counted at `p2p_link.cpp:810` and in `Heard` (`:389-400`), printed in the summary (`:719-721`).
- First direct packet: in `Heard`, where today only a change of source prints (`:395`).

Without the second reflector these lines still can't name the NAT. They only make "nothing direct arrived" visible on both sides at a glance, and they would catch a one-way opening.

**Checks that need no build.**

- Each player reads the WAN address on their router's status page and compares it with the address in "the server sees us at". If they differ, the provider shares the address.
- The friend sends a report right after the session. His launcher keeps two launches, so the report from 19:27 missed connections 3 to 6.
- The launcher's report could carry the Windows Firewall inbound rule for `lbp3.exe` and the default gateway.

## 8. Not determined

- **Which NAT maps per destination.** Ranked in section 6, not proven. The session's logs hold one reflector's view only.
- **Whether the host is behind a carrier-grade NAT.** The port blocks say address sharing; the router's WAN address wasn't read.
- **The filter kind on either side** (per address, or per address and port), and whether a firewall on either PC drops packets from an address the socket hasn't sent to.
- **The friend's line type and provider.** No lookups were made.
- **The friend's side of connections 3 to 6.** No log.
- **Every send.** No HELLO or probe is logged; the table's send cells are from the code.
- **How long the host's NAT keeps an opening that was never answered.** The tests only show openings that had carried traffic both ways.
- **The live server's exact code.** The STUN and relay behaviour is from the clones.

## Sources

- Host log and friend's report: paths above. Line numbers are 1-based lines of those files.
- `D:\Recomps\lbp3-recompiled` at `519f220`: `runtime/src/net/p2p_link.cpp`, `runtime/src/net/p2p_link.h`, `runtime/src/hle/np_shadnet.cpp`, `runtime/src/net/shadnet_service.cpp`, `runtime/tests/p2ptest.cpp`; tags `v0.1.3-lbp3port`, `v0.1.4-lbp3port`, `v0.1.5-lbp3port` for `runtime/launcher/launcher_settings.cpp`.
- LBPPC at `951fb8d`: `server/linux/shadnet/patches/lbppc-shadnet.patch` (the relay: only between endpoints that have pinged, lines 98-132).
- shadNet at `b38cf17`: `src/stun_server.cpp` (`Start`, `HandleStunPing`), `src/cmd_matching.cpp:1187-1247` (`CmdRequestSignalingInfos`).
- Two-instance logs on the host's PC: `D:\Recomps\lbp3-wt\coop\A\stderr_*.txt`, `coop\B\stderr_*.txt`, `coop\C\stderr*.txt`; single-instance logs `D:\Recomps\lbp3-recompiled\analysis\play_best_log*.txt`, `play_log.1.txt`, `play_log.2.txt`.
- [RFC 4787](https://www.rfc-editor.org/rfc/rfc4787.txt), NAT Behavioral Requirements for Unicast UDP: §4.1 mapping, §4.2.1 port preservation, §5 filtering, §6 hairpinning.
- [RFC 5128](https://www.rfc-editor.org/rfc/rfc5128.txt), State of Peer-to-Peer Communication across NATs: §3.3 UDP hole punching, §3.5 port number prediction.
- [RFC 5780](https://www.rfc-editor.org/rfc/rfc5780.txt), NAT Behavior Discovery Using STUN: §4.3 mapping behaviour needs a second address.
- [RFC 7422](https://www.rfc-editor.org/rfc/rfc7422.txt), Deterministic Address Mapping to Reduce Logging in CGN Deployments: §2.3.
- [RFC 6598](https://www.rfc-editor.org/rfc/rfc6598.txt), Shared Address Space: §7.
- Earlier reading of the same logs: `.scratch/lbp3-port/coop/rejoin_no_spawn.md`; the link's design: `.scratch/lbp3-port/best/shadnet_design.md`, "(c) Peer connections".
