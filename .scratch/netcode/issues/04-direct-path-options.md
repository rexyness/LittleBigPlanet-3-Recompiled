# Which ways to a direct path work with an online server we can't change?

Type: research
Status: resolved
Claimed by: research agent (charting session, 2026-11-15)
Findings: branch `research/netcode-direct-path-options` (`554e04c`, local only), file `docs/research/netcode-direct-path-options.md`
Blocked by: none
Map: [Online co-op netcode](../map.md)

## Question

The online server is LBPPC's shadNet with their relay patch. It tells each player where it sees the other (`RequestSignalingInfos`) and it relays; we can't change it. Which ways to a direct path exist within that?

- **(a) A mapped port on the player's router:** UPnP IGD, NAT-PMP and PCP. What each needs, how common it is, how it fails, and how LBPPC (`BB_UPNP=1`) and shadPS4 use it. miniupnpc is already in shadPS4's externals and our link doesn't use it.
- **(b) Hole punching.** What shadNet's protocol offers for timing both sides: does it carry RPCN's signaling-helper notification, or only the request and its answer? What our HELLO exchange already does. Which NAT types that covers, and which never work (symmetric, carrier-grade).
- **(c) IPv6.** Whether the server and the link's addressing allow a direct path over it.
- **(d) What others do that we don't:** LBPPC at `951fb8d` and anything newer upstream, shadPS4's own P2P transport, and RPCS3 with RPCN.

Done when there is a ranked list: each option, what it costs to build on our link, which pairs of players it plausibly rescues (with the source for any number), and what still falls to the relay path.

## Answer

Resolved 2026-11-15 by the research agent. It lists options and chooses none. Every cost and every "who it rescues" is the agent's estimate. Sources and line references are in the findings file.

**What the server offers:** discovery and a relay, nothing for timing. shadNet answers `RequestSignalingInfos` to the asker only and tells the other player nothing, and LBPPC's two server patches don't change that. Our vendored client parses a notification 100 that nothing listens to; whether the live server sends it wasn't determined.

**What the link already does:** it punches holes. After the relayed HELLO both sides probe each other's direct addresses once a second, which opens a direct path for every pair whose routers both keep one mapping per socket. So changes to timing rescue nobody new; they only get a pair direct sooner.

**Windows Firewall is part of the path.** Without an inbound rule for the game, Windows itself only lets in the exact address and port the socket has sent to. On this PC (observed) it is on, blocks inbound, shows no prompt and has no rule for the game. Neither our launcher nor LBPPC's adds one.

**The options, ranked by pairs rescued for the work:**

| # | Option | Cost (estimate) | Who it rescues |
|---|---|---|---|
| 1 | HELLO through the relay at once, then a burst of direct probes | about 30 lines in the link | Nobody new. Every pair that can punch goes direct about 4 s sooner |
| 2 | An inbound Windows Firewall rule for the game, added by the launcher | 50 to 100 lines, one elevation | A player whose NAT maps per destination, against one whose router filters loosely. Also needed when the mapped port is on the other player's side |
| 3 | A port forwarded by hand with `LBP3_P2P_PORT` (exists) | a launcher field and help text | Any pair where one player does it and has no second NAT |
| 4 | A UPnP mapped port, advertised in HELLO | 300 to 450 lines, plus building miniupnpc | The relay cases, when a router grants the mapping |
| 5 | An IPv6 direct path, addresses exchanged in HELLO over the relay path | 400 to 600 lines in the link | Any pair where both have IPv6. The only way when both sit behind a provider's NAT that maps per destination |
| 6 | NAT-PMP and PCP | about 350 lines, no library at hand | Routers without UPnP. No prevalence data |
| 7, 8 | Port prediction; birthday probing | 200 to 400+ lines, a third-party server or 256 sockets | A thin slice. Fragile by their own sources |

**What others do that we don't:**
- **LBPPC and shadPS4** map the P2P port with UPnP at start and tell nobody, relying on the server seeing that port. LBPPC upstream to `c4f94df` changed nothing here.
- **RPCS3 with RPCN** has a server notification to the target, IPv6 signaling end to end, and UPnP with a port fallback.
- **We do and they don't:** we return to a direct path after relaying (LBPPC stays relayed), and we use the LAN address on one network.

**Always on the relay path,** even with options 1 to 6 built:
- a NAT that maps per destination against one that filters per port, or two that map per destination, when no mapped port is possible and not both players have IPv6. That is the likely shape behind a provider's shared address;
- both players behind the same provider NAT that doesn't loop traffic back;
- networks that block UDP between players.

**Not determined:** either player's NAT class or router features (nothing was probed); whether a router sends from its mapped port; whether miniupnpc builds with clang-cl here. The prevalence numbers are all old or from other populations: UPnP answering in 35% of homes (2011), home routers that map per destination under 2% (2016), about half of Google's users on IPv6 (November 2026).

**What follows from it:** put together with [Why did no connection with the friend go direct?](02-why-no-direct-path.md), the firewall finding gives a test that needs no build: [Does an inbound Windows Firewall rule on both PCs open a direct path to the friend?](20-firewall-rule-test.md)
