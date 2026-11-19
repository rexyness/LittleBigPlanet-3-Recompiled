# What does the game netcode tolerate, and which of its limits can be reached?

Type: research
Status: resolved
Claimed by: research agent (charting session, 2026-11-15)
Findings: branch `research/netcode-game-netcode-limits` (`e8758e6`, local only), file `docs/research/netcode-game-netcode-limits.md`
Blocked by: none
Map: [Online co-op netcode](../map.md)

## Question

The lockstep simulation is the eboot's, and the port only carries its game datagrams. Whether anything above the peer link can be changed depends on what the game netcode does and where its numbers come from.

- **(a) The server's network settings.** What the game takes from `network_settings.nws`. The `[slots]` probe showed mode 4 with timeouts 60/60/30/60/50 ×4 on LBPPC's server. Give each field's meaning, and say which ones the game really reads.
- **(b) The 10 s and 20 s limits** (`eboot+0x37b3f0`; the constants at `0x1061c4c` and `0xe75844`). What each one guards and what resets it.
- **(c) Loss and order.** Does the game resend, acknowledge or reorder its own datagrams? Which single lost datagram is the fatal one that DATA3 was built for?
- **(d) Waiting.** Does the simulation wait each step for the other player's input (a stall), or run ahead? Does any delay it adds follow the RTT our signaling reports through `sceNpSignalingGetConnectionInfo` (RTT from the keepalives; bandwidth fixed at 10 Mbit/s)?
- **(e) Three and four players.** Who sends to whom.

Sources: the existing notes ([shadnet_design.md](../../lbp3-port/best/shadnet_design.md), "The drop"), the eboot through the runtime repo's tools, Refresh's source for the settings file, and LBPPC's docs and code.

Done when each of (a) to (e) is marked known, inferred or not found, with addresses and sources, and it says what a later pass would need to settle.

Ghidra is allowed (the user, 2026-11-15: "feel free to use ghidra and all of that good stuff"). While games are running on this PC for another session's test, run it at low priority, one process at a time, and work on a copy of the project if it is locked.

## Answer

Resolved 2026-11-15 by the research agent, from the eboot (the existing Ghidra project, read on a copy), the two-instance logs and Refresh's source. Labels: **known** = read at the address, **observed** = seen in a log, **inferred**, **not found**. Addresses and the full tables are in the findings file.

**Three corrections to the earlier notes** ([shadnet_design.md](../../lbp3-port/best/shadnet_design.md) still carries the old wording; it has another lane's uncommitted edits, so it wasn't touched):
1. **The kick is a keepalive test** (known). Each game sends a keepalive once a second on an ordered stream. A peer is removed when the connection is over 10 s old and no keepalive from them has been *delivered* for 10 s. It isn't "nothing accepted for 10 s".
2. **The "20 s test" is 20 ms** (known): a grace after a peer's goodbye.
3. **"Mode 4, timeouts 60/60/30/60/50" are the game's own defaults** and a state byte (known). The server sent none of them (inferred; the live file wasn't fetched). Refresh's default file has 15 keys, none about netcode or timeouts.

**(a) The server's network settings.** Known: the table of keys and fields, every default, and a reader for most fields. Not found: who reads `EnablePrediction`, `EnableKeepAlive` and the packet delay and drop fields.

**(b) The limits.** Known: only a delivered keepalive resets the 10 s. The 10.0 is a plain float in writable data (`eboot+0x1061c4c`) with no settings key. Not found: what a 9 s test in `eboot+0x356540` guards.

**(c) Loss and order.**
- **The game netcode has its own reliability layer** (known). Each connection carries two ordered, acknowledged streams and one unacknowledged one. A lost message is sent again after 2.0 × the game's own RTT, and up to three extra copies go out when it measures loss. Datagrams out of order or with gaps are accepted; a gap stalls an ordered stream until the resend arrives. The per-frame message is on an ordered stream.
- **The fatal datagram: not found.** With the gap-free link off, the session survived all 13 induced single drops. Both fatal cases followed a guest→host loss that nobody induced. The six induced guest→host drops came exactly 45.000 s apart, so they may all have hit the same kind of datagram. The logs hold sizes, not contents.
- **What it means for DATA3** (inferred): it hides loss from a layer that already repairs it, and its value is that it avoids the unexplained fatal case. A gap it skips after 2 s reaches the game as a plain loss, so a skip counts as a possible kick. While it holds datagrams behind a gap it also holds the keepalives and acknowledgements in them.

**(d) Waiting.**
- **Each game takes the per-frame message from one player only** (known), and that player is the host (observed). Guests stall when it is late and catch up at two steps per flip; the host doesn't wait for guests (inferred). **Corrected on 2026-11-16:** the host does wait, for about a second per gap in a guest's input. See [What does the host do with a guest's late input?](17-late-guest-input.md)
- **The delay on a guest's input isn't a setting** (inferred). It is the time the input takes to come back inside the host's per-frame messages, hidden by prediction of up to `MaxPredictedFrames` (20 frames, 667 ms).
- **The RTT the port reports is fetched and never read** (known). **The bandwidth it reports caps the game's send budget** (known); 0 or an error gives 16,384 bytes a second. The game cuts its own budget to 75% at a time, down to 10,240 bytes a second, when the other side sends a congestion notice. Not found: what triggers that notice.

**(e) Three and four players** (from the code, never run). Connections are a full mesh: every game connects to every other player in the room. Simulation data is a star through the host. The 10 s test runs on every connection, so a failed guest-to-guest path should make those two remove each other (inferred).

**What the port can reach:**
- **The settings file, replaced locally** where the port already rewrites replies (`http_online.cpp`): `MinPredictedFrames`, `MaxPredictedFrames`, `FramesBeforeAgressiveCatchup`, `MaxCatchupFrames`, `MaxLagBeforeShowLoading` (23 frames), `MinLagBeforeHideLoading`, and every `TIMEOUT_...` key. No key reaches the kick, the keepalive, the resend or the budget.
- **Values in the game's memory:** the kick limit (10.0) and the resend factor (2.0) in writable data; the keepalive period, the goodbye grace and the ×4 on per-player timeouts in read-only data.
- **Values the port reports:** the bandwidth, and the connection status and events. The RTT has no effect.

**What follows from it:**
- [Which lost datagram ends a session?](13-fatal-datagram.md)
- [Which of the game netcode's own values does the port change?](14-game-netcode-values.md)
- [What makes the game netcode cut its send budget, and does the peer link trigger it?](15-send-budget-cuts.md)
- [Do the game's own packet delay and drop settings work, and what do the other unread settings do?](16-unread-settings.md)
- [What does the host do with a guest's late input?](17-late-guest-input.md)
- [What does a third player do to a session?](18-third-player.md)
