# Ways to a direct path with an online server we can't change

Research for [issue 04](../../.scratch/netcode/issues/04-direct-path-options.md) of the [online co-op netcode map](../../.scratch/netcode/map.md). Reading only: nothing was built, the game was not run, no router was probed and the online server was not contacted. The one thing run on this PC was `netsh`, to read its firewall settings.

This note lists options. It does not choose one; [issue 08](../../.scratch/netcode/issues/08-path-ladder.md) does. It does not explain the 15 November session either; [issue 02](../../.scratch/netcode/issues/02-why-no-direct-path.md) does.

**How to read it.**

- A sentence with a citation is an **observed** fact from that source.
- A sentence that starts with **Derived:** follows from cited facts by reasoning. It was not tested.
- A sentence that starts with **Estimate:** is my judgement. Every cost and every "who it rescues" size is an estimate.

**Sources and the commits they were read at.**

| Short name | What | Commit |
|---|---|---|
| link | our peer link, `D:\Recomps\lbp3-recompiled\runtime\src\net\p2p_link.*`, `hle\np_shadnet.cpp`, `net\shadnet\*` | `519f220` |
| LBPPC | `D:\Recomps\lbp3-wt\_ref\LBPPC` ([emurphy04/LBPPC](https://github.com/emurphy04/LBPPC)) | `951fb8d` (2026-11-13) |
| shadNet | `D:\Recomps\lbp3-wt\_ref\shadNet` ([shadps4-emu/shadNet](https://github.com/shadps4-emu/shadNet)) | `75edbf0`, the commit LBPPC builds its server from (LBPPC `server/linux/README.md:98-99`) |
| fork | shadPS4 as we carry it, `D:\Recomps\lbp3-recompiled\third_party\shadPS4` (PurpleGitty/shadPS4) | `9423d341` |
| miniupnp | `third_party\shadPS4\externals\miniupnp` ([miniupnp/miniupnp](https://github.com/miniupnp/miniupnp)) | `bf4215a` (tag `miniupnpc_2_3_3`) |
| RPCN | [RipleyTom/rpcn](https://github.com/RipleyTom/rpcn) | `7a3c3c4` (2026-10-20) |
| RPCS3 | [RPCS3/rpcs3](https://github.com/RPCS3/rpcs3), `rpcs3/Emu/NP/` | `1585598` (2026-10-27) |

LBPPC paths given without a folder are under `gpu/bbnet/vendor/shadps4/`: `core/libraries/net/`, `core/libraries/np/`, `core/net/` and `shadnet/`. Fork paths are under `src/` in the same layout.

Newer upstream, checked on 2026-11-15 through GitHub's public API:

- **LBPPC** `main` is `c4f94df`, 27 commits after `951fb8d`. Of the 49 files changed, the only ones near networking are `gpu/bbnet/bbnet_glue.cpp` (where `npbind.dat` is read from) and `run.py` (the update folder). The P2P transport, the signaling handler, UPnP and both server patches are unchanged ([compare](https://github.com/emurphy04/LBPPC/compare/951fb8d915ef8e27a5469ef295ec94196b95a85a...c4f94df1d2aa29aee600ca584b15d3ee8a9c2d9d)).
- **shadNet** `main` is still `b38cf17`. `src/stun_server.cpp`, `src/cmd_matching.cpp`, `src/protocol.h`, `src/server.cpp` and `documentation/signaling.md` are identical at `75edbf0` and `b38cf17` (`git diff --stat`, empty).
- **shadPS4** upstream `main` is `abe42da`. Its `net_upnp.cpp` and `p2p_transport.cpp` are the same as the fork's (compared with line endings stripped). Its `signaling_handler.cpp` differs only in Matching2 details (an establish delay, a member lookup), nothing about paths.

## Headlines

- **The server offers discovery and a relay, nothing for timing.** shadNet at `75edbf0` answers `RequestSignalingInfos` to the asker only. It has no signaling-helper notification. LBPPC's two server patches don't add one. See [(b)](#b-hole-punching).
- **We don't need the server for timing.** The relay path is already a channel between the two links. A HELLO sent through it at once does what RPCN's notification does. It also carries anything we add to HELLO: a mapped port, IPv6 addresses.
- **The link already punches holes, 4 s late and once a second.** After the relayed HELLO both sides send PINGs to each other's direct addresses. Derived: that opens a direct path for every pair whose routers both keep one mapping per socket, which is the common case for home routers.
- **So the timing changes rescue nobody new.** Sending the HELLO through the relay at once, or acting on a server notification, changes how soon a pair goes direct, not which pairs do. New pairs come only from an inbound firewall rule, a mapped port, IPv6, or guessing ports.
- **Windows Firewall is part of the path.** On this PC it blocks unsolicited inbound traffic, shows no prompt, and has no rule for the game. A mapped port on this PC's router alone would not let a player in whose packets come from a port this PC never sent to.
- **LBPPC and shadPS4 map the port with UPnP and tell nobody.** They rely on the server seeing the mapped port. RPCS3's UPnP code keeps the mapped port to itself as well. On our link the mapped address can travel in HELLO instead.
- **Over IPv6 the server gives nothing**, but the link can exchange IPv6 addresses itself through the relay path. It is the only option here for two players who are both behind a carrier-grade NAT that gives a new port per destination.

## What the peer link does today

1. **One socket.** The link has one IPv4 UDP socket, bound to any address (`p2p_link.cpp:887-898`). Its STUN ping to the server leaves from that socket every 5 s (`p2p_link.cpp:757-765`, `p2p_link.h:76-77`). The reply is the address the server sees, kept as `external` (`p2p_link.cpp:690-697`).
2. **The guest asks.** In LBP3 only the guest's game activates a connection; the host learns of it when a HELLO arrives (`np_shadnet.cpp:1086-1088`, `:1360-1383`). `Activate` asks the server where the host is (`p2p_link.cpp:777-782`, `:820-835`; `np_shadnet.cpp:1220-1224`). The answer becomes the first direct candidate and the relay's destination.
3. **HELLO, direct.** The guest sends HELLO to every candidate each 500 ms (`p2p_link.cpp:783-786`). The host knows nothing yet. It hears a HELLO only if its router and its Windows Firewall let an unsolicited packet in.
4. **HELLO, relayed, after 4 s** (`p2p_link.cpp:787`). The host makes the connection if the guest is a friend (`:541-551`), learns the guest's server-seen and LAN addresses from the HELLO (`:553-555`, `:569-570`) and answers ACK the way the HELLO came (`:572-575`).
5. **Both now probe.** While no direct packet is fresh, each side sends a PING to every candidate once a second, besides the one through the relay (`p2p_link.cpp:804-810`: "keep trying the direct addresses (a hole may open)"). A PING is answered to the address it came from (`:653-665`).
6. **First direct packet wins.** Any direct packet sets `direct` to its source (`p2p_link.cpp:385-402`). Traffic goes direct while a direct packet arrived in the last 5 s (`:39`, `:216-218`, `:243-257`).
7. **Same network.** When both players share a public address, the LAN address from HELLO is preferred (`p2p_link.cpp:229-241`). The server does not hand out LAN addresses: its STUN handler ignores the local IP in the ping (shadNet `src/stun_server.cpp:64-98`).

Derived: steps 4 to 6 are UDP hole punching as RFC 5128 describes it. A sends to B's public endpoint and asks B, through the server, to send to A's ([RFC 5128 §3.3.1](https://www.rfc-editor.org/rfc/rfc5128#section-3.3.1)). Our "ask B" is the relayed HELLO.

Derived: in ICE's words ([RFC 8445 §2.1](https://www.rfc-editor.org/rfc/rfc8445#section-2.1), [§7.2.5.3.1](https://www.rfc-editor.org/rfc/rfc8445#section-7.2.5.3.1)) the LAN address is a host candidate, the server-seen address a server-reflexive one, and an address first seen on an arriving packet a peer-reflexive one. The link has all three. It has no candidate from a mapped port and none over IPv6.

### Which pairs that covers

The terms are RFC 4787's ([§4.1](https://www.rfc-editor.org/rfc/rfc4787#section-4.1), [§5](https://www.rfc-editor.org/rfc/rfc4787#section-5)):

- **Mapping.** *Endpoint-independent* (EIM): one public port per socket, whatever the destination. *Address-dependent* or *address- and port-dependent* (here together: EDM, the old "symmetric"): a new public port per destination.
- **Filtering.** *Endpoint-independent* (EIF): anyone may send to the mapped port. *Address-dependent* (ADF): only addresses the socket sent to. *Address- and port-dependent* (APDF): only address-and-port pairs the socket sent to.

RFC 5128 states the rule: hole punching works when both NATs are EIM, and fails when one is not, because the peer can't know the port to send to ([§3.3](https://www.rfc-editor.org/rfc/rfc5128#section-3.3), [§5.1](https://www.rfc-editor.org/rfc/rfc5128#section-5.1)).

Derived, from those definitions and steps 4 to 6. Rows are one player, columns the other:

| | EIM + EIF | EIM + ADF | EIM + APDF | EDM |
|---|---|---|---|---|
| **EIM + EIF** | direct | direct | direct | direct (1) |
| **EIM + ADF** | direct | direct | direct | direct (1) |
| **EIM + APDF** | direct | direct | direct | **relay** |
| **EDM** | direct (1) | direct (1) | **relay** | **relay** |

(1) The EDM player's packets arrive from a port the other PC never sent to. The other player's router lets them in. Their Windows Firewall does not, unless it has an inbound allow rule for the game (see [the firewall](#the-windows-firewall-on-the-players-pc)).

Derived: two more cases follow the same table.

- **Carrier-grade NAT.** Two NATs in a row behave like the stricter of the two. A carrier-grade NAT with EIM can be punched like any other. RFC 6888 requires that of them (REQ-1, by way of RFC 4787 REQ-1) and recommends EIF ([RFC 6888 §3](https://www.rfc-editor.org/rfc/rfc6888#section-3)). So "carrier-grade NAT never works" is too strong for hole punching. It is true for mapped ports.
- **Both players behind the same carrier-grade NAT.** They see each other only at their public endpoints, and the NAT must hairpin ([RFC 5128 §3.3.3](https://www.rfc-editor.org/rfc/rfc5128#section-3.3.3)).

### How common the cells are

- **Home routers with EDM are rare in the one large study found.** "less than 2% of the tests showed CPE NATs with very restrictive symmetric NATs" (Richter et al., IMC 2016, §6.5, [arXiv:1605.05606](https://arxiv.org/abs/1605.05606); Netalyzr sessions up to 2016).
- **Carrier-grade NAT is common and often EDM.** The same paper finds it in 17–18% of eyeball ASes and over 90% of cellular ASes (§1, §5). 11% of non-cellular carrier-grade-NAT ASes have symmetric as their most permissive type; for cellular networks the paper reports "40% symmetric" and "20% full cone" (§6.5).
- **Hole punching in the wild.** 82% of 380 tested NATs supported UDP hole punching in 2005 (Ford, Srisuresh, Kegel, USENIX ATC 2005, §6.2, [bford.info/pub/net/p2pnat](https://bford.info/pub/net/p2pnat/)). In IPFS's network, 70% ± 7.1% of hole-punching attempts succeeded, counted only where the relay reservation and the address discovery before them had succeeded; over 4.4 million attempts from 85,000 networks (Trautwein et al., IMC 2026, abstract, [arXiv:2604.12484](https://arxiv.org/abs/2604.12484)).
- **No good number exists for our players.** They are a handful of friends on home PCs. Neither study samples them.

## (a) A mapped port on the player's router

A mapped port makes the router pass any packet for one public port to the link's socket. Derived: it turns that player's router into EIM + EIF for the link, whatever its normal behaviour. In the table, the whole row and column of that player become "direct".

### The three protocols

| | UPnP IGD v1 / v2 | NAT-PMP | PCP |
|---|---|---|---|
| Defined in | UPnP Forum, [WANIPConnection:2](https://upnp.org/specs/gw/UPnP-gw-WANIPConnection-v2-Service.pdf) (2010) | [RFC 6886](https://www.rfc-editor.org/rfc/rfc6886) (2013; in Apple products since 2005, abstract) | [RFC 6887](https://www.rfc-editor.org/rfc/rfc6887) (2013), NAT-PMP's successor (RFC 6886 §1.1) |
| How the router is found | SSDP multicast search, then HTTP and SOAP (miniupnpc `upnpDiscover`, `include/miniupnpc.h:86-110`; DiCioccio et al., PAM 2012, §2) | The default gateway, UDP 5351 (RFC 6886 §3, §3.3) | The default router unless one is configured, UDP 5351, version 2 (RFC 6887 §8.1; RFC 6886 §1.1) |
| Needs from the router | The IGD service switched on, on the LAN side | A NAT-PMP server on the gateway. "Existing deployed NATs will not support this protocol" (RFC 6886 §4.5) | A PCP server. Designed for home routers, carrier-grade NAT and IPv6 firewalls (RFC 6887 §1, §2.1) |
| Needs from the PC | Multicast out and the reply back in; TCP to the router | The gateway's address; one UDP request and reply | The same |
| Lease | v1: 0 means permanent; some routers accept only that (error 725). v2: 1 to 604800 s, 0 "MUST be interpreted as 604800 seconds", 3600 s recommended (WANIPConnection:2 §1.2, §2.5.16.2, §2.5.16.3; miniupnpc `include/upnpcommands.h:166-195`) | 7200 s recommended; renew at half (RFC 6886 §3.3) | The client asks for a lifetime, the server "MAY grant a lifetime smaller or larger", and the client must renew (RFC 6887 §15) |
| Code we have | miniupnpc 2.3.3, a client for UPnP IGD only (miniupnp `README:10-12`) | none: `externals/` holds no libnatpmp | none |

miniupnp's own router daemon speaks all three ("an implementation of a UPnP IGD + NAT-PMP / PCP gateway", miniupnp `README:11-12`).

### How common

- **UPnP answered in about 35% of 120,000 homes in 2011.** Per data set: 54% of 2,209 homes, 22% of 95,417, 47% of 30,243 (DiCioccio, Teixeira, May, Kreibich, "Probe and Pray", PAM 2012, §3 and Table 1, [PDF](https://www.icir.org/christian/publications/2012-pam-upnp.pdf)). The paper gives three reasons for no answer: the gateway has no UPnP, has it "disabled by default", or the host's firewall blocks the discovery.
- That number is 15 years old and counts answers to discovery, not mappings granted. **No newer number was found.**
- **NAT-PMP and PCP: no number was found.** The one first-party remark is from an implementer: "You can't rely on these protocols being present", and carrier-grade-NAT providers "tend to turn them off in the equipment they put in homes" (Anderson, [How NAT traversal works](https://tailscale.com/blog/how-nat-traversal-works), Tailscale, 2020).

### How each fails

- **Switched off or absent.** See above.
- **Double NAT.** Every one of the three talks to the nearest router only. NAT-PMP says so and leaves nested NAT unsolved (RFC 6886 §4.3.1). miniupnpc detects it: `UPNP_GetValidIGD` returns 2 when the router's own WAN address is private or in the carrier-grade range 100.64.0.0/10 (miniupnpc `include/miniupnpc.h:213-223`, `src/addr_is_reserved.c:36-53`).
- **Carrier-grade NAT.** The home router's mapping is useless behind it. PCP is the intended answer, and a carrier-grade NAT "MUST implement a protocol giving subscribers explicit control over NAT mappings" (RFC 6888 REQ-9). No deployment number was found.
- **The mapped port isn't the port the server sees.** IGD v2 requires that a mapping "MUST also be applied when NAT port triggering occurs for outbound traffic" (WANIPConnection:2 §2.5.16), and miniupnpd adds that rule (miniupnp `miniupnpd/netfilter/iptcrdr.c:1266-1275`, on by default in `miniupnpd/configure:840-842`). The requirement is listed among the changes since v1 (§1.2). PCP notes that on EDM NATs the same external port may serve an outbound and an inbound mapping, and offers two ways to deal with it (RFC 6887 §16.1). So on v1 and other routers it depends on the router. In favour: "in more than 92% of non-CGN sessions the CPE did not alter the source port numbers" (Richter et al., §6.2).
- **The lease runs out, or the router restarts.** A permanent v1 mapping survives until the router restarts or someone deletes it. A v2 mapping asked for with 0 lasts 7 days. NAT-PMP and PCP mappings must be renewed.
- **Mappings pile up** when a program that crashed binds another port on its next run (RFC 6887 §15 says so for PCP).
- **The port is taken** by another PC in the home: error 718 (miniupnpc `include/upnpcommands.h:176`).
- **The PC's own firewall drops what the router lets through.** Next section.

### The Windows Firewall on the player's PC

- **Default.** Windows Firewall will "block all incoming traffic, unless solicited or matching a rule" ([Windows Firewall overview](https://learn.microsoft.com/en-us/windows/security/operating-system-security/network-security/windows-firewall/), Microsoft Learn). An app that listens needs an inbound rule, and "It's common for the app or the app installer itself to add this firewall rule" ([Windows Firewall rules](https://learn.microsoft.com/en-us/windows/security/operating-system-security/network-security/windows-firewall/rules), Microsoft Learn, 2025, "Applications rules").
- **What counts as solicited for UDP.** A flow is "identified by a (Destination IP Address and Destination Port) tuple" and expires after 60 s idle by default ([ALE stateful filtering](https://learn.microsoft.com/en-us/windows/win32/fwp/ale-stateful-filtering), Microsoft Learn). Derived: without a rule, Windows itself is an address- and port-dependent filter on the link's socket.
- **The prompt.** Without a rule, a dialog asks the user the first time. If they say no or cancel, block rules are made; a non-admin user gets block rules whatever they answer; rules are per full path of the program (the rules page, "Applications rules" and "Known issues with automatic rule creation").
- **This PC** (observed 2026-11-15 with `netsh advfirewall show allprofiles` and `netsh advfirewall firewall show rule name=all dir=in`): the firewall is on in all three profiles with `BlockInbound,AllowOutbound`; `InboundUserNotification` is `Disable`, so no prompt appears; no inbound rule names `lbp3` or the launcher. The friend's PC was not looked at.
- **Neither port adds a rule.** Our `runtime/` (launcher included), `tools/` and top-level scripts hold no firewall code, and neither does LBPPC's tree (searched for `firewall`, `advfirewall`, `INetFwPolicy`, `INetFwRule`, `New-NetFirewallRule`, `HNetCfg`). LBPPC counts on Windows' prompt: its game's version resource is commented "What Windows shows for the game: Task Manager, the firewall prompt, Properties" (LBPPC `packaging/windows/LBPPC-Game.rc:5`).

Derived, for the three cases that matter:

- **Hole punching between two EIM routers is unaffected.** Each side sends first to the exact address it then hears from.
- **A mapping on the EDM player's own router needs no rule**, as long as that router also sends from the mapped port. The other player then sends to the mapped port and hears back from it.
- **A mapping on the other player's router needs the rule on that player's PC.** The EDM player's packets come from a port that PC never sent to, and those are the packets Windows drops. This is the case when the EDM is a carrier-grade NAT, where the EDM player can't map anything.

### How LBPPC and shadPS4 use UPnP

The two are the same code: the fork's `src/core/libraries/net/net_upnp.cpp` is the same file as LBPPC's `gpu/bbnet/vendor/shadps4/core/libraries/net/net_upnp.cpp` (compared with line endings stripped).

- **Switch.** LBPPC's `run.py` sets `BB_UPNP=1` whenever a shadNet account is given (`run.py:236-238`: "The router forwards the P2P port to this PC (UPnP), so a joining player can reach the host"). Without `run.py` the vendored default is off (`gpu/bbnet/include/core/emulator_settings.h:49`). shadPS4's own default is on (fork `src/core/emulator_settings.h:241`).
- **When.** As soon as the P2P transport has its UDP port, before any connection (LBPPC `net_kernel.cpp:107-118`; fork `net_kernel.cpp:101-112`). Discovery starts earlier, at sign-in (LBPPC `np_handler/np_handler.cpp:360-362`).
- **Discovery.** `upnpDiscover` with a 2 s wait, then `UPNP_GetValidIGD`; anything but 1 ("valid and connected") counts as no router, which also skips a router behind another NAT (`net_upnp.cpp:76-97`).
- **What.** One UDP mapping, external port = internal port = the transport's bound port, to this PC's LAN address, description "shadPS4", lease "0" (`net_upnp.cpp:122-132`). LBPPC binds any free port (`run.py:235`, `BB_P2P_PORT=0`); shadPS4 defaults to 3658 (fork `emulator_settings.h:242`).
- **Off the caller's thread.** `MapPortAsync` waits up to 10 s for discovery on its own thread (`net_upnp.cpp:144-158`).
- **Failure is not checked.** When the router refuses, the port is stored anyway "as best-guess" (`net_upnp.cpp:137-141`).
- **Removal.** When the transport stops and when the process ends normally (`net_kernel.cpp:119-123`, `net_upnp.cpp:25-35`: "Mappings have no lease time: leave nothing behind on the router"). Derived: after a crash the mapping stays, and the next run binds another port and maps that too.
- **How the mapped address reaches the other player: it doesn't, explicitly.** The handshake sends `mapped_port = 0` (LBPPC `np/signaling_handler.cpp:487-488`, `:523-524`, `:539-540`). The STUN ping leaves from the P2P socket (`:206-227`), so the server sees whatever port the router gives that socket, and `RequestSignalingInfos` hands that out (shadNet `src/cmd_matching.cpp:1187-1247`). Derived: this works only when the router sends from the mapped port, which is likely with equal ports and not certain (see "How each fails").
- **The router's address is preferred** over the server's for "our public address" (`net_kernel.cpp:124-131`).

RPCS3 maps in the same way with small differences: it remembers the router's URL, tries the next 100 external ports when one is refused, and passes no lease ([`upnp_handler.cpp:25-153`](https://github.com/RPCS3/rpcs3/blob/158559877f77113e7ea26eb27614582bd27d704f/rpcs3/Emu/NP/upnp_handler.cpp#L25-L153)). It doesn't pass the external port on either: the table of mapped ports is private, has no getter and is read only to delete mappings (`upnp_handler.h:21-31`, `upnp_handler.cpp:14-18`, `:164-172`), and its public address comes from RPCN (`np_handler.cpp:976`). Derived: when RPCS3 had to take another external port, the mapping helps only if the router also sends from it. Only the files under `rpcs3/Emu/NP/` named here were read.

### What it would take on our link

Estimate, not built:

- **Build:** add miniupnpc (static, from the externals) to the runtime's CMake. [shadnet_design.md](../../.scratch/lbp3-port/best/shadnet_design.md) names "miniupnp with clang-cl" as a build risk; it was not tried here.
- **Code:** a small mapper beside the link (`runtime/src/net/`, about 250 lines; LBPPC's `net_upnp.*` is GPL-2.0-or-later and 257 lines), started from `StartLink` (`np_shadnet.cpp:1185-1250`) on its own thread, never on the link's or the game's.
- **Tell the other player ourselves:** one more address in HELLO and ACK (`p2p_link.h:12-15`, `p2p_link.cpp:264-277`, `:523-578`), added to the candidates. The wire format may break freely (map, "Same version on both sides"). About 30 lines and a `p2ptest` case.
- **Checks worth having:** a timed lease with renewal instead of "0"; compare the router's external address with the server-seen one and don't advertise when they differ.
- **Without any code:** `LBP3_P2P_PORT` already fixes the link's port (`np_shadnet.cpp:1190`). A player can forward that UDP port on their router by hand.

## (b) Hole punching

### What shadNet's protocol offers for timing

- **Only the request and its answer.** `CmdRequestSignalingInfos` looks the target up in `udpExt` and replies to the asker. It sends nothing to the target (shadNet `src/cmd_matching.cpp:1187-1247`).
- **There is no such notification in the protocol.** The server's notification types are 5, 6, 7, 8, 10, 11, 17, 18 and 19 (shadNet `src/protocol.h:93-114`). `SignalingHelper` appears nowhere under `src/` at `75edbf0`.
- **shadNet says so itself:** "shadnet's signaling responsibility is narrow: endpoint discovery only", "no activation-intent registration", and "P2P hole-punching and connection-state tracking happen on the client/emulator side" (shadNet `documentation/signaling.md:9-11`, `:95`).
- **A leftover.** shadNet's `clientsample/` still parses RPCN's `SignalingHelper = 12` (`clientsample/types.h:59`, `clientsample/client.cpp:742-756`). Nothing sends it.
- **Rooms do carry addresses.** A Matching2 room event gives every member the others' addresses (shadNet `src/cmd_matching.cpp:391-402`, `:672`), and shadPS4 then starts a handshake from each side (fork `src/core/libraries/np/signaling_handler.cpp:1529-1601`). LBP3 uses no Matching2 commands (LBPPC `server/linux/shadnet/patches/lbppc-shadnet.patch:35-38`).

### RPCN's notification, for comparison

- When a player asks where another is, RPCN answers and then sends the target a `SignalingHelper` notification with the asker's name and address: "Send a notification to the target to help with connectivity" ([`cmd_misc.rs:71-85`](https://github.com/RipleyTom/rpcn/blob/7a3c3c480d54d7b51a7a231655c778821097ee4e/src/server/client/cmd_misc.rs#L71-L85)). Added in RPCN 1.1.0, 2024-02-04 (`CHANGELOG.md:368`).
- RPCS3's target then sends about ten `signal_info` packets to that address, 200 ms apart, which opens its router to the asker ([`np_notifications.cpp:276-294`](https://github.com/RPCS3/rpcs3/blob/158559877f77113e7ea26eb27614582bd27d704f/rpcs3/Emu/NP/np_notifications.cpp#L276-L294), [`signaling_handler.cpp:774-791`](https://github.com/RPCS3/rpcs3/blob/158559877f77113e7ea26eb27614582bd27d704f/rpcs3/Emu/NP/signaling_handler.cpp#L774-L791), `:481-491`, `signaling_handler.h:86-89`).

### Does a notification reach LBPPC's server build?

- **Not in what LBPPC publishes.** The server is shadNet `75edbf0` plus two patches (LBPPC `server/linux/README.md:98-104`). `lbppc-shadnet.patch` adds the relay and same-game presence (`:1-6`). `lbppc-shadnet-security.patch` has no line about signaling, notifications or UDP.
- **The client knows a notification 100.** LBPPC's client parses "SignalingHelper = 100, SuperMedo shadNet: a player wants to connect to us" and treats it as an activation request from that player, so that "its ack opens our router to the peer" (LBPPC `shadnet/client.h:147`, `client.cpp:1349-1363`, `np_matching2/np_matching2_mm.cpp:885-888`, `np/signaling_handler.cpp:1625-1650`). The name and the SMRL relay framing come from Supermedo's Bloodborne port, whose public tree holds the client only ([Supermedo/bloodborne_pc `6085365`](https://github.com/Supermedo/bloodborne_pc/tree/6085365d14c179612d7fffe91a989806e031aed8), `gpu/bbnet/vendor/shadps4/shadnet/client.h:142`).
- **Our vendored client parses it too, and nothing listens** (`net/shadnet/client.cpp:1433-1448`, `client.h:387`; `shadnet_service.cpp` hooks only `onFriendStatus`, `onClientRejected` and `onServiceUnavailable`, `:276`, `:283`, `:323`).
- **Whether the live server sends it could not be determined.** Contact was off limits. The 15 November host log can't tell: it has 770 `[shadnet]` lines from our service and none from the vendored client's own logger, which is where "SignalingHelper: ... wants to connect" would be written. A log line in a hooked `onSignalingHelper` would settle it in one session.

### What that leaves us, and what it's worth

- **Derived: the relayed HELLO is our notification.** The relay passes any packet between two signed-in players' endpoints (`lbppc-shadnet.patch:98-139`), and both links have pinged the server since the game started them (`np_shadnet.cpp:1323-1328`). Sending the HELLO through the relay at once, not after 4 s (`Config::relayAfterMs`, `p2p_link.h:76`), tells the host at the start. No server change, no dependence on notification 100.
- **Derived: it adds no pair.** The table above already assumes both sides send. It removes up to 4 s from every join where the host's router or firewall filters, and a short burst of probes after the HELLO (today: one a second) would shorten the relayed stretch further.
- **Estimate:** about 30 lines in `p2p_link.cpp` and a `p2ptest` case.

### What never works by punching, and the techniques for it

- **EDM on one side and APDF on the other, or EDM on both** (RFC 5128 §3.3, §5.2: such NATs "are not suitable for P2P applications as techniques such as TCP/UDP hole punching will not work").
- **Port prediction** (RFC 5128 [§3.5](https://www.rfc-editor.org/rfc/rfc5128#section-3.5)): guess the EDM side's next port from the last one. The RFC's own verdict: it fails when ports are not sequential, when another device behind that NAT opens a connection in between, and has "little chance of working" behind two NATs. Among carrier-grade NATs, 22% of non-cellular ASes allocate sequentially, 36% randomly, 41% keep the port (Richter et al., §6.2, Table 6). No number for home routers.
  - Derived: prediction needs two observations of the EDM side's ports from two destinations. The server has one UDP endpoint (shadNet `src/server.cpp:109-114`). The second would be a third-party STUN server, which is a new outside dependency, or the peer itself when it is reachable.
- **Birthday probing** (Anderson, Tailscale, 2020, an implementer's write-up, not a standard): the EDM side opens 256 sockets, the other side probes random ports; 1,024 probes give 98%. With EDM on both sides it takes 170,000 probes for 99.9%, "28 minutes" at 100 a second, and the article calls a router's session table the next limit.
  - Derived: on our link that means 256 extra sockets, a scan-like burst from the other PC, and moving a connection to whichever socket won. The link has one socket today.
- **Estimate: neither is worth its cost here.** They reach a thin slice (EDM against APDF with no mapped port and no IPv6), they are fragile by their own sources' account, and the relay path already carries those pairs.

## (c) IPv6

### What the server allows

- **With the published settings, the server's UDP socket is IPv4-only.** `Host=0.0.0.0` becomes `QHostAddress::AnyIPv4` and the STUN socket binds to that (shadNet `src/server.cpp:93-97`, `:109-114`; LBPPC `server/linux/shadnet/shadnet.cfg:2`). This rests on the settings file LBPPC publishes; the live server's own file was not seen.
- **Whatever the settings, its replies are IPv4-shaped.** The STUN reply is four address bytes and two port bytes (shadNet `src/stun_server.cpp:90-98`). The relay frame carries an IPv4 address and port, and the relay looks players up by IPv4 text (`lbppc-shadnet.patch:65-69`, `:101-121`). `RequestSignalingInfosReply.target_ip` is a string (shadNet `src/shadnet.proto:539-544`), the only field that could hold an IPv6 address.
- **So: no relay over IPv6, and no usable discovery.**

### What our link allows

- **Nothing today.** `Endpoint` is 32 bits and a port (`p2p_link.h:55-62`); the socket is `AF_INET` (`p2p_link.cpp:887`); HELLO's two addresses are 4 + 2 bytes each (`p2p_link.cpp:38`, `:54-58`); the server's host is resolved as IPv4 only (`np_shadnet.cpp:1165-1183`).
- **Derived: the link could do it alone.** A PC knows its own global IPv6 addresses without a STUN server, since IPv6 has no address translation in the usual home setup. The two links can exchange them in HELLO and ACK over the relay path and then probe each other, exactly as in steps 4 to 6.
- **Derived: the game's view need not change.** The game identifies a player by an IPv4 "game address" (`p2p_link.h:35-36`), which the link already keeps apart from the address packets travel on (`gameAddr` against `direct`, `p2p_link.cpp:125-127`).
- **Punching is still needed.** IPv6 home gateways filter statefully; the recommendation is endpoint-independent filtering by default, with address-dependent as the stricter choice ([RFC 6092 REC-17](https://www.rfc-editor.org/rfc/rfc6092#section-3.2.3)). Windows Firewall's flow rule is written for both families (the ALE page names the `V{4|6}` layers). Derived: with no translation there is no EDM case, so both sides sending is enough for any filter, provided each PC sends from the address it told the other.
- **That proviso is real work.** A PC usually has several IPv6 addresses, and the default is to send from a temporary one ([RFC 6724 §5](https://www.rfc-editor.org/rfc/rfc6724#section-5), Rule 7). Derived: a socket bound to "any" may send from an address other than the one the peer was told, or answer from another address than the one a packet came to. The link would have to list every global address as a candidate, or pin its source address. Windows' own choice was not checked.
- **Estimate of the work:** `Endpoint` for both families; a second socket or one dual-stack socket; own addresses from the OS (`iphlpapi` is already linked, `runtime/CMakeLists.txt:107`) and control over the source address; a list of IPv6 candidates in HELLO and ACK; `p2ptest` over `::1`. About 400 to 600 lines in `p2p_link.*`. A real check needs two IPv6 networks.
- miniupnpc can also open an IPv6 pinhole on an IGD v2 router (`UPNP_AddPinhole`, `include/upnpcommands.h:555-580`; `controlURL_6FC`, `include/miniupnpc.h:207-208`). Derived: not needed when both sides punch.

### Who has it

- 46.6% to 51.0% of Google's users reached it over IPv6 in the two weeks to 2026-11-13 ([Google IPv6 statistics](https://www.google.com/intl/en/ipv6/statistics.html), data file `adoption.js`). It varies widely by country and provider.
- Estimate: both players need it, so well under half of pairs. No number exists for pairs of PC players.

### RPCN and RPCS3, for comparison

RPCN binds its UDP socket dual-stack, stores an IPv4 and an IPv6 address per player and hands out the IPv6 one when both have it ([`udp_server.rs:54-128`](https://github.com/RipleyTom/rpcn/blob/7a3c3c480d54d7b51a7a231655c778821097ee4e/src/server/udp_server.rs#L54-L128), `cmd_misc.rs:55-58`; RPCN 1.4.0, 2025-02-11, `CHANGELOG.md:266`). RPCS3 pings RPCN over both families and shows the game a stand-in IPv4 address for an IPv6 peer ([`rpcn_client.cpp:576-583`](https://github.com/RPCS3/rpcs3/blob/158559877f77113e7ea26eb27614582bd27d704f/rpcs3/Emu/NP/rpcn_client.cpp#L576-L583), `ip_address.cpp:26-71`, `signaling_handler.cpp:660-666`).

## (d) What others do that we don't

| | Does, and we don't | We do, and it doesn't |
|---|---|---|
| **LBPPC** `951fb8d` (unchanged to `c4f94df`) | Maps the P2P port with UPnP at start (`net_kernel.cpp:107-118`, `net_upnp.cpp:122-132`). Prefers the router's external address (`net_kernel.cpp:124-131`). Acts on notification 100 if it comes (`np_matching2_mm.cpp:885-888`). | Returns to a direct path after relaying: LBPPC marks a peer's address as relayed and never unmarks it (`core/net/p2p_transport.cpp:183`, `:292-303`, `:349`; `np/signaling_handler.cpp:1315-1322`: "go through the relay on the shadNet server from now on"). Uses the LAN address for two players on one network; LBPPC relays them when the router doesn't hairpin (`lbppc-shadnet.patch:2-3`, `docs/online.md:56-57`). |
| **shadPS4** (fork `9423d341`, upstream `abe42da`) | The same UPnP mapping. For Matching2 games, both sides learn each other from the room and both start a handshake (`signaling_handler.cpp:1529-1601`). | A relay at all (no `SMRL`, no relay timer in either tree). |
| **RPCS3 with RPCN** | The server tells the target (`cmd_misc.rs:71-85`). IPv6 end to end. The LAN address from the server when both share a public address (`cmd_misc.rs:49-53`). UPnP with a port fallback. | A relay: RPCN's UDP server answers the address ping and nothing else (`udp_server.rs:144-147`). |

Derived, about LBPPC: for LBP3 its host never sends first, because only the guest activates and the handshake goes one way until the relay takes over for good. So LBPPC gets a direct path only when the host is reachable unasked (a mapped port, or EIF) or when notification 100 arrives. Its docs agree: "When they can't (both behind one router, or a router without UPnP), the server relays" (LBPPC `docs/online.md:56-57`).

## The ranked list

Ordered by pairs rescued for the work, by my estimate. It is an order to read, not a choice. "Today" means the table in [Which pairs that covers](#which-pairs-that-covers).

| # | Option | Cost to build on our link (estimate) | Pairs it plausibly rescues | Still on the relay path | Confidence |
|---|---|---|---|---|---|
| 1 | **HELLO through the relay at once**, and a short burst of direct probes after it | `p2p_link.cpp` (`Tick`, `relayAfterMs`), `p2ptest`. About 30 lines. | None new. Every pair that can punch goes direct about 4 s sooner and spends less of the join on the relay path. | Everything on it today. | High: read from the code. Not measured. |
| 2 | **An inbound Windows Firewall rule** for the game, added by the launcher | `runtime/launcher/` and the installer; needs one elevation; the rule is per program path. About 50 to 100 lines. | EDM player against a player whose router is EIF or ADF: footnote (1) in the table. Also what 3, 4 and 6 need when the mapping is on the non-EDM player's side. | EDM against APDF; EDM against EDM. | Medium: from Microsoft's documents and this PC's settings; not tested with a second network. No number for how many PCs lack the rule. |
| 3 | **A port forwarded by hand**, with `LBP3_P2P_PORT` | No link code. A field in the launcher and a help text. | Any pair where one player does it and is not behind a second NAT. With 2 when the other player is the EDM one. | Pairs where nobody does it; carrier-grade NAT on the forwarding side. | Medium. Depends on the player, and on the server seeing the forwarded port. |
| 4 | **UPnP IGD mapped port**, advertised in HELLO | miniupnpc into the runtime's CMake; a mapper in `runtime/src/net/` (about 250 lines, LBPPC's is reusable); HELLO and candidates in `p2p_link.*` (about 30 lines); `np_shadnet.cpp` `StartLink`; `p2ptest`. About 300 to 450 lines plus the build work. | The **relay** cells of the table when a router grants a mapping: on the EDM player's side without 2, on the other side with 2. UPnP answered in 35% of homes in 2011 (PAM 2012); no newer number. | Pairs where neither router maps; double NAT or carrier-grade NAT on the mapping side. | Medium for the mechanism (three other projects use it). Low for the size: old number, and home-router EDM was under 2% in 2016. |
| 5 | **IPv6 direct path**, addresses exchanged in HELLO over the relay path | `p2p_link.*` (endpoint type, socket, HELLO, own addresses), `p2ptest`. About 400 to 600 lines. | Any pair where both players have working IPv6, whatever their IPv4 NAT, carrier-grade NAT included. About half of Google's users have IPv6 (Nov 2026); both must. | Pairs where either player has none. | Medium for the mechanism (RPCS3 and RPCN do it with server help; ours would be without). Low for the size: no number for pairs. |
| 6 | **NAT-PMP and PCP** beside UPnP | New code, no library in the externals: gateway lookup, a 12-byte request, renewal (about 150 lines); PCP's MAP on top (about 200 more). | Routers that speak one of them and not UPnP; in principle a carrier-grade NAT with PCP. No number found. | As 4. | Low: no prevalence data at all. |
| 7 | **Port prediction** | Needs a second observation point (a third-party STUN server or the peer), an allocation model and probe bursts. About 200 to 300 lines. | EDM with sequential ports against APDF, on a quiet network. | EDM with random ports; EDM on both sides; two NATs in a row. | Low: RFC 5128 §3.5 itself calls it brittle. |
| 8 | **Birthday probing** | 256 sockets on one side, about 1,000 to 2,000 probes from the other, moving the connection to the winning socket. 400 lines or more, and load on routers. | EDM with random ports against EIM. | EDM on both sides (28 minutes by the source's own sums). | Low. |
| – | **Hook notification 100** | `shadnet_service.cpp`, `np_shadnet.cpp`, a call into the link. About 40 lines. | None new, and nothing over 1 unless the live server sends it. | As 1. | Low: the published server doesn't send it; the live one is unknown. |

## What always falls to the relay path

Derived, with every option above built except 7 and 8:

- **EDM against APDF, or EDM against EDM, when neither player gets a mapped port and they don't both have IPv6.** With carrier-grade NAT this is the likely shape: a mapped port can't reach past it, and 11% of non-cellular carrier-grade-NAT ASes were EDM in 2016.
- **Both players behind the same carrier-grade NAT that doesn't hairpin**, without IPv6.
- **A network that blocks UDP between players** but lets the server through. No source or number; named for completeness.
- **A PC whose firewall holds a block rule for the game.** Block rules win over allow rules (Microsoft Learn, "Rule precedence"). Outbound-first traffic still passes, so this costs only the cases of option 2.

What these pairs get is the relay path's delay: the sum of both players' distance to the server. Whether that calls for a nearer relay is in the map's "Not yet specified".

## Not determined

- **Whether the live server sends notification 100.** See [(b)](#does-a-notification-reach-lbppcs-server-build).
- **Either player's NAT class, and whether their routers offer UPnP, NAT-PMP or PCP.** No router was probed. Issue 02 reads the logs for the first.
- **The friend's Windows Firewall state.** Only this PC was read.
- **Whether a given router sends from the mapped port.** Router-dependent; a check against the server-seen address would show it at run time.
- **Whether miniupnpc builds with clang-cl in our runtime, and whether SSDP replies pass this PC's firewall.** Nothing was built or sent.
- **Which IPv6 address Windows sends from** on a socket bound to "any", and so how much source-address work option 5 needs.
- **Whether RPCS3 hands its mapped port to anything outside the files read.** Its UPnP class offers no way to.
- **Current prevalence** of UPnP being on, of NAT-PMP and PCP, of home-router EDM, and of IPv6 for pairs of PC players. The numbers above are the best found: 2011, 2016, 2005 and 2026, each for a different population.
- **A cheap diagnostic that would fill several of these:** log, per connection, the port a peer's direct packet came from beside the port the server reported for it. Equal means EIM, different means EDM.

## Sources

Code, at the commits in the table at the top.

Specifications:

- RFC 4787, NAT Behavioral Requirements for Unicast UDP: <https://www.rfc-editor.org/rfc/rfc4787>
- RFC 5128, State of Peer-to-Peer Communication across NATs: <https://www.rfc-editor.org/rfc/rfc5128>
- RFC 6092, Simple Security in IPv6 Gateway CPE: <https://www.rfc-editor.org/rfc/rfc6092>
- RFC 6724, Default Address Selection for IPv6: <https://www.rfc-editor.org/rfc/rfc6724>
- RFC 6886, NAT Port Mapping Protocol: <https://www.rfc-editor.org/rfc/rfc6886>
- RFC 6887, Port Control Protocol: <https://www.rfc-editor.org/rfc/rfc6887>
- RFC 6888, Common Requirements for Carrier-Grade NATs: <https://www.rfc-editor.org/rfc/rfc6888>
- RFC 8445, ICE (the general method our candidates and probes are a small case of; §2.1, §7.2.5.3.1): <https://www.rfc-editor.org/rfc/rfc8445>
- UPnP Forum, WANIPConnection:2 Service, 2010-09-10: <https://upnp.org/specs/gw/UPnP-gw-WANIPConnection-v2-Service.pdf>

Vendor documents:

- Microsoft Learn, Windows Firewall overview: <https://learn.microsoft.com/en-us/windows/security/operating-system-security/network-security/windows-firewall/>
- Microsoft Learn, Windows Firewall rules (2025-06-06): <https://learn.microsoft.com/en-us/windows/security/operating-system-security/network-security/windows-firewall/rules>
- Microsoft Learn, ALE stateful filtering: <https://learn.microsoft.com/en-us/windows/win32/fwp/ale-stateful-filtering>
- Google IPv6 statistics: <https://www.google.com/intl/en/ipv6/statistics.html>

Measurements:

- Richter, Wohlfart, Vallina-Rodriguez, Allman, Bush, Feldmann, Kreibich, Weaver, Paxson, "A Multi-perspective Analysis of Carrier-Grade NAT Deployment", IMC 2016: <https://arxiv.org/abs/1605.05606>
- DiCioccio, Teixeira, May, Kreibich, "Probe and Pray: Using UPnP for Home Network Measurements", PAM 2012: <https://www.icir.org/christian/publications/2012-pam-upnp.pdf>
- Ford, Srisuresh, Kegel, "Peer-to-Peer Communication Across Network Address Translators", USENIX ATC 2005: <https://bford.info/pub/net/p2pnat/>
- Trautwein, Ihle, Schubotz, Breitinger, Gipp, "Large-Scale Measurement of NAT Traversal for the Decentralized Web: A Case Study of DCUtR in IPFS", IMC 2026: <https://arxiv.org/abs/2604.12484>

An implementer's write-up, used for the cost of port mapping and birthday probing only:

- Anderson, "How NAT traversal works", Tailscale, 2020-08-21: <https://tailscale.com/blog/how-nat-traversal-works>
