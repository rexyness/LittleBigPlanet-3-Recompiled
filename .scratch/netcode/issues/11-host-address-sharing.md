# Is the host's PC behind an address its provider shares, and does it have IPv6?

Type: task
Status: resolved
Claimed by: work-through session, 2026-11-16
Blocked by: none
Map: [Online co-op netcode](../map.md)

## Question

[Why did no connection with the friend go direct?](02-why-no-direct-path.md) found the host's mapped ports all inside one block of about 1,000 per public address, which suggests the provider shares that address among customers. If it does, a port opened on the home router can't be reached from outside, and the path ladder has to be decided without that rung for this pair.

Check it on this PC, with no build and without touching the online server:

- **The router's own outside address** against the address in the game log's "the server sees us at" line. Ways to read it without the router's password: a UPnP `GetExternalIPAddress` query to the router, or the first hops of a route trace to a public address (an address in 100.64.0.0/10 or a private one after the home router means sharing by the provider).
- **Whether UPnP, NAT-PMP or PCP answer on the router at all.** A read-only query; map nothing.
- **Whether this PC has a global IPv6 address and a working IPv6 route.**

Already known about this PC, from [Which ways to a direct path work with an online server we can't change?](04-direct-path-options.md): Windows Firewall is on, blocks inbound, shows no prompt and has no rule for the game.

If none of that settles the first point, hand the human a two-line checklist: open the router's status page, read its WAN address.

Done when each of the three is answered yes, no or unknown, with the evidence. The same three questions about the friend's PC go into the checklist of [Does an inbound Windows Firewall rule on both PCs open a direct path to the friend?](20-firewall-rule-test.md).

## Answer

Resolved 2026-11-16 by the work-through session, on this PC, with read-only queries: nothing was mapped, no build was run and the online server wasn't contacted. All three points are **observed**.

**1. Does the provider share the host's public address? Yes.**
- The home router answers UPnP's `GetExternalIPAddress` with a **private address** (in 10.0.0.0/8), and reports NAT on. The online server saw this PC at a public address (`play_log.txt`, "[p2p] the server sees us at", 15 November).
- A route trace agrees: after the home router come five more hops, all private (in 10.0.0.0/8 and 172.16.0.0/12), before any public address.
- So there are two NATs in a row: the home router's, then the provider's. The block of about 1,000 ports per public address that [Why did no connection with the friend go direct?](02-why-no-direct-path.md) saw is the provider's allotment to this customer (inferred).

**2. Do UPnP, NAT-PMP or PCP answer on the router? UPnP yes; NAT-PMP and PCP no.**
- UPnP: the router answers discovery and offers `WANIPConnection:1`. It would very likely grant a mapping, which wasn't tried. That mapping would sit on the router's private outside address, where nobody on the internet can reach it.
- NAT-PMP and PCP: UDP port 5351 on the router is closed (the request is refused at once, three tries each). The provider's NAT can't be asked for a port from here.

**3. Does this PC have a global IPv6 address and a working route? Yes.**
- The PC's wired adapter holds three global addresses in one /64 of the provider's prefix (one from the router's advertisement, one temporary, one from DHCPv6), with a default route through the router.
- A ping to a public IPv6 address answers in 2 ms with no loss, and an HTTPS request over IPv6 succeeds.
- The online server's name has an IPv4 address only, so the server can never tell a player the other's IPv6 address. The two games would have to exchange them themselves over the relay path, as option 5 in [Which ways to a direct path work with an online server we can't change?](04-direct-path-options.md) has it.

**Windows Firewall, checked again:** on in all three profiles, inbound blocked, no prompt, no rule for the game. It filters IPv6 the same way.

**What it means for the path ladder:**
- **For the host, the mapped-port rungs are dead:** UPnP, a port forwarded by hand (`LBP3_P2P_PORT`), NAT-PMP and PCP. None can open a port the friend could reach. They stay useful only to a player whose router holds a public address.
- **The link can tell this case by itself:** when the router's `GetExternalIPAddress` differs from the address the server sees, a mapped port is pointless and can be skipped. That check is one query and maps nothing.
- **A punched hole now has to pass the provider's NAT too.** In the two-instance tests it kept one mapping for a second destination and let nothing in before the host had sent (seen only for traffic that looped back through it). If it treats outside destinations the same way, a pair whose other side also keeps one mapping can still punch through. If the friend's NAT maps per destination, as ranked most likely, IPv4 stays on the relay path for this pair whatever the Windows Firewall rule does, unless the provider's NAT filters by address only.
- **IPv6 is the one rung that fully avoids the provider's NAT** for the host. It needs the friend to have IPv6 as well. That question is already item 4 of the friend's checklist in [Does an inbound Windows Firewall rule on both PCs open a direct path to the friend?](20-firewall-rule-test.md).

**Not determined:**
- whether the router's own IPv6 firewall lets in packets from an address this PC hasn't sent to. It can't be seen from inside. If both sides probe each other, as the link already does over IPv4, it doesn't matter;
- how the provider's NAT treats outside destinations (mapping and filtering). The second outside party in [Which NAT gives the link a mapping per destination: the friend's, the host's or both?](12-which-nat.md) answers the mapping half;
- whether the IPv6 prefix changes over time. It doesn't matter if addresses are exchanged at each connect.

**The router checklist for the human isn't needed:** the UPnP answer settled the first point.
