# Why did no connection with the friend go direct?

Type: research
Status: resolved
Claimed by: research agent (charting session, 2026-11-15)
Findings: branch `research/netcode-why-no-direct-path` (`12583eb`, local only), file `docs/research/netcode-why-no-direct-path.md`
Blocked by: none
Map: [Online co-op netcode](../map.md)

## Question

All six connections on 2026-11-15 ran on the relay path at about 450 ms RTT. The peer link sends its HELLOs to the direct addresses first, goes to the relay after 4 s, and keeps trying the direct ones afterwards. So why did none open?

Answer it from both sides' logs and the link's code, not from a guess:

- **Logs:** the host's `D:\Recomps\lbp3-recompiled\analysis\play_log.txt` and the friend's `D:\Recomps\lbp3-recompiled\analysis\online\friend_report_20261115_192711.zip`.
- **Code:** `D:\Recomps\lbp3-recompiled\runtime\src\net\p2p_link.cpp` and `p2p_link.h` on `main` `519f220`. The friend ran an earlier release.

What to establish:

- which addresses each side knew for the other, and when;
- which HELLOs went to which address, and whether any direct packet ever arrived on either side;
- what the server-seen addresses say about each NAT: the host's local port against the port the server saw, and the friend's ports across his restarts (42742, then 42746);
- whether the link's own rules kept it on the relay when a direct path could have opened: the 4 s before the relay, the 5 s a direct packet counts as fresh, the `Auto` path setting, and the fact that only the joining side activates the connection.

Done when the cause is named with its evidence and placed on the host's side, the friend's side or the link's. If the logs can't decide, say which log lines to add so the next session does.

## Answer

Resolved 2026-11-15 by the research agent. The cause is **ranked, not proven**: the session's logs can't name the NAT, because the link asks only one outside party (the online server) where it sees us.

**Observed:**
- **No direct packet arrived.** On the host, 461 of 461 five-second summaries across all six connections say "relayed". On the friend, 153 of 153 in the two connections his log covers. His side of connections 3 to 6 has no log.
- **Both sides had the right addresses for each other**: the server-seen one and the LAN one. Nothing was missing or stale.
- **The link's own rules didn't hold the path on the relay.** Both sides probe every direct address once a second for as long as a connection lasts, and one direct packet switches the path. The 4 s only delays the relay; the 5 s freshness rule and the `Auto` setting played no part.
- **The friend ran v0.1.4, whose link code is byte for byte v0.1.5's.** His build explains nothing.
- **The host's NAT**, in 18 two-instance test pairs on this PC (10 on the same evening and public address): it reuses one mapping for a second destination, and lets nothing in until the host has sent first. Seen only for traffic that loops back through the NAT.
- **The host's mapped ports all sit in one block of about 1,000 per public address** (58 of 58 sockets).
- **The friend's server-seen ports rise from launch to launch** (42736, 42741, 42742, 42746).

**Inferred, most likely first:**
1. The friend's NAT gives the link's socket a different mapping toward the host than toward the server, and the host's side only lets in the exact address and port it has sent to.
2. The same with the sides swapped (possible only if the host's NAT treats outside destinations differently from looped-back ones).
3. Both.
4. Something drops inbound UDP for another reason (a firewall, a provider). Nothing points to it and nothing excludes it.

**Where it sits:**
- **The friend's side:** the main share, by inference.
- **The host's side:** a necessary share. The link accepts a peer on an unexpected port, so it only fails if the host's side filters per port. The port blocks also suggest the provider shares the host's public address; if so, a port opened on the home router wouldn't be reachable from outside.
- **The link:** no share in holding the path on the relay. What it lacks: no port mapping request, no port guessing, a single outside party to ask, no log of what it sends or of a first direct packet.

**A hazard found on the way** (not seen on the 15th): if direct packets arrive one way only, the receiving side sends everything directly and stops probing, while the other side hears nothing from it.

**What follows from it:**
- [Is the host's PC behind an address its provider shares, and does it have IPv6?](11-host-address-sharing.md): a check on this PC that needs no build.
- [Which NAT gives the link a mapping per destination: the friend's, the host's or both?](12-which-nat.md): the log lines that would decide it, and a session with the friend.
- The one-way hazard goes to [How do two players get a direct path, and what carries them when they can't?](08-path-ladder.md).
