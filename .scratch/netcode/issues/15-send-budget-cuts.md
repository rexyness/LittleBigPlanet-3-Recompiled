# What makes the game netcode cut its send budget, and does the peer link trigger it?

Type: research
Status: resolved
Claimed by: research agent (work-through session, 2026-11-16; the first agent stopped on a usage limit on 2026-11-15 with nothing written)
Findings: branch `wayfinder/netcode-host-address` (`2baec25`, local only), file `docs/research/netcode-send-budget-cuts.md`. The agent couldn't write to the `research/netcode-send-budget-cuts` worktree, so that branch stays empty
Blocked by: none
Map: [Online co-op netcode](../map.md)

## Question

[What does the game netcode tolerate, and which of its limits can be reached?](03-game-netcode-limits.md) found that each connection has a send budget in bytes a second. A congestion notice from the other side (control code 2) cuts it to 75%, at most every 0.25 s, down to 10,240 bytes a second. A game sends that notice when a delay-trend test on the time stamps in the datagram trailers fires (`eboot+0x37ec30`, `0x380ce0`). The decompiler output for that test was unreliable, so it wasn't read.

Establish:

- what the test measures, and its thresholds (read it in assembly);
- how the budget recovers: at what rate and under which conditions;
- whether the peer link's own behaviour can fire it: datagrams released in a burst after a gap (DATA3), the relay path's jitter, the repeat of the last datagram after 100 ms idle, doubled datagrams;
- what a cut budget does to a join or a level change: how long a load's worth of data takes at 10,240 bytes a second;
- whether the 15 November logs fit a cut budget during the loads that dragged (the link logs datagram counts per five seconds, and a few sizes).

Done when each point is marked known, observed, inferred or not found, with addresses and log lines. Ghidra is allowed; see the map's Notes.

## Answer

Resolved 2026-11-16 by the research agent, from the assembly (`llvm-objdump`; Ghidra wasn't run). "Model" below means the agent's own simulation of the test, not a run of the game. Addresses and the basis of each mark are in the findings file.

**What the test measures** (known). The time stamps in the trailers count in ticks of 1/6 s. The receiver keeps the travel times of the last 64 datagrams and takes 8 medians of 8. It fires when at least 5 of the 7 steps between them rise and the net rise is more than half the sum of the steps (`eboot+0x37ec30`, `0x380ce0`). A notice goes out at most every 0.1 s. That is a different limit from the one on cuts: the sender cuts at most every 0.25 s, as the question above has it.

**What fires it** (inferred, model). Datagrams arriving steadily later for a second or two: a climb of about 0.3 s per second for 2 s always does, at 30 datagrams a second. A steady delay, however long, never does. Nor does jitter.

**A second cut exists** (known). A game cuts its own budget to 75%, with no notice from anyone, when its count of timed-out resends rises through four half-second periods in a row (`0x37d63b..0x37d857`).

**Recovery** (known).
- The budget starts at 16,384 bytes a second, not at the bandwidth the port reports.
- After a cut it grows every 5 s, by x1.0, 1.1, 1.5, then 2.0, and only while the game is sending at 75% of the budget or more. Every new notice restarts the 5 s.
- So steady play never grows it, and a guest cut to the 10,240 floor stays there (inferred). The port's 10 Mbit/s is out of reach except during a bulk send.

**Does the peer link fire it?** Rarely (known for the link's code, inferred from the model for the rest).
- Doubled datagrams and the repeat after 100 ms idle never reach the game (`p2p_link.cpp:601`).
- A burst released after a gap, or after the receiver froze, fired the test in 0 to 15% of model runs, usually with one notice. A burst gives falling delays, not rising ones.
- The relay path's steady delay never does.
- The delay spells measured on 15 November: not found. The model gives anything from no notice to 42, depending on how arrivals fell inside each 5 s window, which the logs don't hold.

**What a cut does to a join or a level change** (known and inferred). The budget limits less than "how fast the game sends":
- first sends on the ordered streams aren't limited;
- resends are: budget / 30 - 50 bytes per flush, which is 291 bytes at the floor, about one resend;
- resource transfer between players is: the budget minus 4,096 for two players. At the floor that is 6,144 bytes a second, so 1 MB takes 163 s against about 19 s uncut.
- The size of a load: not found. The upper bound for a level change is 0.94 MB, from datagram counts.

**Do the 15 November logs fit a cut budget?** Not found. The counts fit several stories and neither need nor exclude a cut. The values that would settle it are on the connection at `+0x118`, `+0x12c`, `+0x120` and `+0x124`.

**Also not found:** the writer of the timer at manager `+0xd8` that halves the allowance, and whether resource data flowed at all on 15 November.

**What follows from it:**
- At the floor, a flush that already holds 291 bytes or more of new messages sends no resend. Whether real traffic keeps flushes that full for 10 s is a candidate for the unexplained kicks: it goes to [Which lost datagram ends a session?](13-fatal-datagram.md), which already probes the budget.
- The budget, its growth step and its last-cut times during a join and a level change through a delayed link go to [How does today's online co-op hold up at set delays and loss?](06-coop-under-set-conditions.md) and to the log lines in [Which log lines does a session need to measure itself?](19-session-log-lines.md).
- Whether the port's 256-datagram socket queue (`net_offline.cpp:107`) drops datagrams the link has delivered, during a 2 s freeze in a load at 130 datagrams a second, goes to the same log-lines ticket as a counter, and to [How should the peer link deliver game datagrams?](09-datagram-delivery.md).
- For bursts, [How should the peer link deliver game datagrams?](09-datagram-delivery.md) can take it that a released burst seldom cuts the budget, and that a smoothly growing delay does.
