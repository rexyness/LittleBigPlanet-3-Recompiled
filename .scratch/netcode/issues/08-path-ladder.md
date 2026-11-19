# How do two players get a direct path, and what carries them when they can't?

Type: grilling
Status: open
Blocked by: 02, 04, 11, 20
Map: [Online co-op netcode](../map.md)

## Question

Decide:

- the order of paths the peer link tries: the LAN, a mapped port, a punched hole, IPv6, the relay path;
- which of the ranked options in [Which ways to a direct path work with an online server we can't change?](04-direct-path-options.md) the link and the launcher take on;
- how long each gets before the next is tried;
- when a running connection moves from one path to another;
- what the link does when a direct path opens one way only (today the side that hears direct packets sends everything directly and stops probing, while the other side hears nothing from it);
- whether the link compares the router's own outside address (UPnP `GetExternalIPAddress`) with the address the server sees, and skips the mapped-port rungs when they differ. The host's PC is such a case: its provider shares the public address;
- what happens to pairs who can never go direct.

It draws on [Why did no connection with the friend go direct?](02-why-no-direct-path.md), [Which ways to a direct path work with an online server we can't change?](04-direct-path-options.md), [Is the host's PC behind an address its provider shares, and does it have IPv6?](11-host-address-sharing.md) and [Does an inbound Windows Firewall rule on both PCs open a direct path to the friend?](20-firewall-rule-test.md).

Two facts to start from: changes to timing alone rescue no new pairs, though sending the HELLO through the relay at once would take about 4 s off every join; and the relayed RTT is the sum of the two players' legs to the server.

[Which NAT gives the link a mapping per destination: the friend's, the host's or both?](12-which-nat.md) needs a session with the friend and may not be answered when this is taken. If it isn't, decide the ladder so that it holds for every cause in the ranking.

If the answer needs a nearer relay or one of our own, stop there. That costs money and hosting, so it is the user's decision; the map's "Not yet specified" holds it until then.

**From** [What stops the host's simulation when a guest starts to join?](23-join-start-stall.md)**:** the block with no step at every join costs about five times the RTT, so the relay path's RTT is paid five times over there. It adds weight to a nearer relay for pairs that can't go direct.

**From** [What does a third player do to a session?](18-third-player.md)**:** the ladder has to hold for every pair in a session. Two guests who aren't friends get no address for each other from the server at all, so for that pair there is no rung today, not even the relay, and the third player's join never finishes. A rung through the host (the host's link hands out the addresses, or forwards) is the candidate; [Should join protection let two guests of one host connect?](26-join-protection-guests.md) decides the rule.

**From** [What is the netcode bar?](07-netcode-bar.md)**:** the ladder has four targets to meet: two PCs on one LAN always go direct; a pair some rung can serve is direct within 10 s of ESTABLISHED; a pair that stays on the relay path has each rung tried, and what it failed on, named in the log; and asking to ESTABLISHED takes 2 s on any path (today 4 to 6 s on the relay path). The last three are provisional on this ticket: if one can't be reached, put the best number found into the bar. The bar counts a relay path at 450 ms as passing when no rung can serve a pair, so it doesn't by itself call for a nearer relay.
