# How should the peer link deliver game datagrams?

Type: grilling
Status: open
Blocked by: 03, 06, 13, 15, 32
Map: [Online co-op netcode](../map.md)

## Question

DATA3 hands the game every datagram in order with none missing. A gap holds back everything behind it until it is filled: from the copies in the next two datagrams, then by asking again every 30 ms, and it is given up after 2 s. On the relay path one ask-and-answer takes 450 ms or more.

[What does the game netcode tolerate, and which of its limits can be reached?](03-game-netcode-limits.md) changed what this question is about:

- the game netcode has its own reliability layer: it accepts datagrams out of order and with gaps, and sends a lost message again after twice its own RTT;
- so DATA3 hides loss from a layer that already repairs it, and its one known value is that it avoids a fatal case nobody has explained;
- while DATA3 holds datagrams behind a gap it also holds the keepalives and acknowledgements in them, and the kick comes when no keepalive is delivered for 10 s;
- a gap DATA3 skips reaches the game as a plain loss. On 15 November the last connection ended after 250 skips.

Decide:

- whether the link should stop holding datagrams back and let the game repair loss itself, protecting only what [Which lost datagram ends a session?](13-fatal-datagram.md) shows is fatal;
- if it keeps some protection, which: copies, forward error correction, asking again on a timer that follows the RTT;
- what the link does about bursts, given what [What makes the game netcode cut its send budget, and does the peer link trigger it?](15-send-budget-cuts.md) finds;
- whether DATA3's hold behind a lost datagram makes a longer gap in a guest's input than the game's own resend (twice its RTT) would. The host stalls every player for such a gap ([What does the host do with a guest's late input?](17-late-guest-input.md));
- whether the port's 256-datagram socket queue can drop what the link delivered during a freeze in a load, and what the link does about it;
- a rule for datagram size.

Decide from the measured matrix in [How does today's online co-op hold up at set delays and loss?](06-coop-under-set-conditions.md). A library swap belongs in the answer only if a measured need calls for one. The wire format may change freely, since both players run the same version.

**From** [What stops the host's simulation when a guest starts to join?](23-join-start-stall.md)**:** every join is an exchange of about five round trips during which no PC steps. Datagrams held back behind a gap lengthen it for everyone. Decide delivery with that phase in view.
**Marked inferred:** that datagrams held back behind a gap lengthen the join exchange is a reading of the research, which only inferred that the link should not hold datagrams in that phase.

**Seen in the code while adding the log lines (2026-11-16), not yet seen in a run:** the 2 s after which the link gives up on a gap are counted from the first gap of a run of gaps, not from the one being waited behind. `gapSince` is cleared only when nothing is held back (`Deliver3` in `p2p_link.cpp`). Under steady loss on a slow path, where a new gap opens before the last one is filled, the link would skip datagrams after 2 s although every single gap was being filled in time. The new gap lines ("given up") will show whether it happens.

**From** [Which lost datagram ends a session?](13-fatal-datagram.md)**:** the loss the game couldn't survive, which DATA3 was built around, was a fault in the port's clock on paths faster than a frame, not something in the game. With its RTT kept from going wrong the game repairs a lost datagram itself: 50 drops in six minutes on the LAN path and 33 in four at 60 ms, gap-free delivery off, no kick and no stall longer than 4 flips. That is steady play in the Pod only: loss during a join or a level load, and with the send budget at the floor, is untested and goes to [How does today's online co-op hold up at set delays and loss?](06-coop-under-set-conditions.md). So "does the link have to hold datagrams back at all" is now an open choice, to make once [How does the port keep the game's RTT right on a path faster than a frame?](32-rtt-on-fast-paths.md) says how that fault is stopped.

**From** [How does today's online co-op hold up at set delays and loss?](06-coop-under-set-conditions.md)**:** single losses need no help from the link (the game repairs them, and the copies fill them at no cost); bursts are what hurts. At 450 ms gap-free delivery covers a burst of exactly two (its two copies: clean with it, broken without) and from three in a row up did not help (a hold of one round trip, the game's RTT pushed up by a quarter to two thirds, each missing datagram sent again 13 to 14 times; its copies cover exactly two datagrams of a burst).

**From** [What is the netcode bar?](07-netcode-bar.md)**:** delivery has three targets to meet at a 450 ms RTT, each against the playable line (no stall of 15 flips or more, under 10% of flips with no step, no catch-up loading screen over play): steady play with 5% single loss, with nothing held behind a gap (today passes); two lost in a row of every 101 (today passes, on the copies); four lost in a row of every 101 (today fails; provisional on this ticket, so if no way of delivering reaches it, put the best burst length that passed into the bar). 15 November's failure was silence with almost no loss, so the bar ranks the burst target below the ones on freezes and silence.

**From** [How does the port keep the game's RTT right on a path faster than a frame?](32-rtt-on-fast-paths.md)**:** the fault gets its own fix outside the link: an RTT sample under one frame counts as one frame (run once: 567 drops in six minutes at no added delay with gap-free delivery off, no kick, no budget cut, and after the join 3.5% and 2.4% of flips with no step, never two in a row), then one clock for the game's connection code. So delivery can be decided as if the game repairs single losses on a fast path too; hiding loss from the game is no longer needed for that. The join in that run was slow (a block of 4.3 s on the guest): loss during a join without gap-free delivery is still one run's worth of knowledge. [Does the one-frame floor hold through the bar's cell and between 0 and 30 ms of delay?](38-floor-between-0-and-30ms.md) checks it at the bar's length.
