# How does the port keep the game's RTT right on a path faster than a frame?

Type: grilling
Status: resolved
Claimed by: work-through session 6, 2026-11-18 (branch `wayfinder/netcode-rtt-fast-paths`)
Blocked by: none
Map: [Online co-op netcode](../map.md)

## Question

[Which lost datagram ends a session?](13-fatal-datagram.md) found that the port's frame clock can show the game an acknowledgement "before" its message was sent, when the acknowledgement comes back within the same frame. The game reads the negative difference as billions of seconds, its RTT is wrong for the next 5 s, and it resends nothing meanwhile: one lost datagram then ends the session. It happens on any path under about 8 ms: two instances on this PC, two PCs on one home network.

Decide where the port stops it:

- **At the sample.** Replace a negative RTT sample at the call at `eboot+0x37f100`. It touches nothing the simulation reads. It would be a patch that is always on while online, where today it is a probe's hook. Counting it as 0 was tested (`LBP3_RTT_CLAMP=1`): 50 drops in six minutes with no kick, but the game's RTT then read about 5 ms on a path where the other side read 33 ms, it resent after 11 ms, and its own rule on timed-out resends cut the send budget to the 10,240 floor after 71 s and held it there. So 0 is the wrong value: the sample has to be the true round trip.
- **In the clock.** Make the frame clock's time the same on every thread that reads it, or never earlier than a time already handed out to another thread (`MonotonicNs` in `runtime/src/hle/kernel_time.cpp`). It removes the cause for every reader, not only this one, but the frame clock is what keeps our tick, and the tick gate has to hold.
- **Both, or neither.** Neither would mean leaving it to gap-free delivery to hide loss from the game.

Also settle:

- whether other code in the game subtracts two clock readings taken on different threads (the timeouts of the join states, the keepalive test), and what a difference of -100 ns or -8 ms does there;
- where a true sample comes from when the frame clock says no time has passed: real time taken at the send and at the acknowledgement, the link's own RTT for that player, or a floor such as one frame. The many samples that come out as exactly 0 on a fast path (same frame, no wavering) pull the mean down the same way, so the choice has to cover those too, not only the negative ones;
- which threads take the two readings. The guard in `MonotonicNs` is per thread, so they are two; `LBP3_CLOCK_THREADS=1` names them.

Done when the choice is made, with what the build ladder of the netcode spec has to carry for it (the patch or the clock change, and the tick gate if the clock is touched).

**From** [What is the netcode bar?](07-netcode-bar.md)**:** the bar has a steady cell at no added delay with 5% single loss, ten minutes, run twice: the playable line on both games, no kick, no cut of the send budget, and the game's RTT never read wrong. Today's build fails it; whatever this ticket decides has to pass it, and `LBP3_RTT_CLAMP=1` as it stands doesn't (it cuts the send budget to the floor).

## Answer

Resolved 2026-11-18. Settled by the agent with its recommendation, under the map's standing preference.

**The choice: both, as two steps of the build ladder.** First a floor at the sample, which is the fix that was run. Then one clock for the game's connection code, which removes the cause for its other readers.

**Step 1. At the sample: an RTT sample under one frame counts as one frame** (1/30 s). It sits where the test hook sits (the call at `eboot+0x37f100`), is on whenever online is on, and touches nothing the simulation reads and nothing in the clock, so it needs no tick gate.

- **It was run** (observed, run rtf1). Two instances at no added delay, gap-free delivery off, one incoming datagram in 19 dropped on both games, six minutes, with `LBP3_RTT_FLOOR_MS=33.4`: 567 drops on the host's side, no kick, the send budget never cut on either game (the guest's stayed at 16,384, the host's at 32,768), the game's RTT 33 to 35 ms on both, the budget's cut field zero in every line. After the join both games stepped as in a run with no loss: the host had no step on 3.5% of its flips and the guest on 2.4%, with no two such flips in a row on either, over 5 min 40 s. The join itself was slow under this loss: the guest went 128 flips (4.3 s) with no step in its block, and its oldest keepalive (3.6 s) and most messages held (8) were both in the first 12 s of the connection. Loss during a join with gap-free delivery off was untested before and this is one run of it. Without the floor the first drop was fatal in three runs of three, and with negative samples counted as 0 the budget went to its floor after 71 s ([Which lost datagram ends a session?](13-fatal-datagram.md)).
- **Why a frame and not the true round trip.** The question above says the sample has to be the true round trip. It doesn't, and on a fast path the true one would likely do harm. In rtf1 the guest's raw samples were 645 below 0, 9,729 exactly 0 and 1,626 above 0, of which 1,209 between 30 and 40 ms (observed). So about one acknowledgement in seven comes back a frame later than the rest: each game reads its sockets at set points of its own frame. The mean of the true round trips would be a few milliseconds, the game resends after twice its RTT with no floor of its own (known, [findings](../../../docs/research/netcode-rtt-on-fast-paths.md)), and every acknowledgement that takes a frame would then count as a timed-out resend: the budget cut that the test with 0 showed (inferred; real-time samples weren't run). A frame is the shortest time in which the other game can be relied on to answer, and it is what the host already read on the same path with no fix (33 ms, budget never cut).
- **It covers the zeros and the negatives alike**, whichever threads took the two readings, and leaves every sample of a frame or more as it is. At an RTT of 60 ms and up the unfloored figures gave no trouble in any run.

**Step 2. In the clock, for the connection code only: every reading the game's connection code takes comes from one clock that never goes back between threads.** The port picks the connection code out by the caller's address (the game's clock wrapper `eboot+0x1b1b0` keeps a frame pointer): callers in `eboot+0x374000..0x382000`, plus `0x36055e`, `0x3664d3` and `0x36697a`. Those callers get the frame clock on every thread, with one guard shared by all threads in place of the guard per thread. Every other reader gets exactly what it gets today.

- **Why the frame clock for them and not real time.** The connection code paces its own flushes with these readings on the two simulation threads. On the frame clock that pacing stays as it is today; on real time it would waver against the flips, which is the fault the frame clock was made to end (inferred; real time for these callers wasn't run).
- **Why it is wanted on top of step 1.** Every difference of two readings in the connection code goes through the same unsigned conversion, not only the RTT sample (known). A negative one makes the resend timer fire at once, and that resend counts towards the budget cut; it costs one strike of the five in the slot and join timeouts; and it could kick at once if a keepalive stamp were ever taken on a real-time thread right after a connection is set up, which isn't settled. None of these was seen to hurt in rtf1, so step 2 is the smaller gain and comes second. Unlike the RTT fault, these don't need a fast path: two threads reading within the same 8 ms can meet on any path.
- **It carries the tick gate**, because it edits the clock's file, and it is on only while online is on, so the loopback path with online off stays byte for byte.

**"Neither" is ruled out.** Leaving it to gap-free delivery would keep the link hiding loss for a reason that is the port's own fault, and [How should the peer link deliver game datagrams?](09-datagram-delivery.md) should decide delivery without that weight.

**The other things this ticket had to settle:**

- **Which threads take the two readings** (known from assembly, [findings](../../../docs/research/netcode-rtt-on-fast-paths.md)). Both simulation threads do both jobs on a host. Whether a guest runs the host thread wasn't settled from the assembly; the guest's 645 samples below 0 say that two threads on the frame clock take its readings too (inferred). 'main' flushes stream 0 each frame and reads the socket in the network update; 'game host thread' flushes stream 1 and reads the socket for two classes of message; all of it under the connection manager's lock. The first-send stamp is written in the datagram builder (`eboot+0x37e07a`) and the acknowledgement's reading is at `eboot+0x37f0f1`, each a fresh read of the clock. So -100 ns is a message stamped on one of the two threads and acknowledged on the other inside the same 8 ms (inferred). The host's one sample of -6.67 ms needs a stamp from a real-time thread; the room-update handler `eboot+0x375550` is the candidate, and its thread isn't settled. `LBP3_CLOCK_THREADS=1` wasn't run: it names threads per clock function, not per call site, and the assembly answered more.
- **Other code that subtracts two readings.** The list is in the findings, with all 52 callers of the conversion (23 in netcode). Harmless: the 9 s session test and the budget timers (float seconds, signed compares). Wrong by one step: flush pacing (one early flush), the RTT's bucket rotation (one bucket closed early), the slot and join timeouts (one strike of five). Worse: the resend timer, and the keepalive test in the one unsettled case above.
- **Where a sample comes from when the frame clock says no time has passed.** From nowhere: it is floored. Real time at both ends and the link's own RTT were the alternatives. The link's RTT was 0 ms on this path and leaves out the other game's frame, which is the part that matters.

**What the build ladder carries:**

1. Step 1, the floor. Pass: the bar's steady cell at no added delay with 5% single loss, twice ([What is the netcode bar?](07-netcode-bar.md), target 8), and no `(WRONG)` in any `[ses-conn]` line. "The game's RTT never read wrong" there means: never above 5 s. An RTT of one frame on a path faster than a frame is right by this decision.
2. Step 2, one clock for the connection code. Pass: the tick gate with online off; no negative sample counted in `[ses-conn]` over a ten-minute run at no added delay; the 60 ms steady cell no worse on flips with no step than before it.
3. Neither step touches the peer link's files, so they don't collide with the joining-and-leaving lane.

**What was built** (runtime branch `netcode/rtt-floor`, on top of `netcode/session-log-game`; the worktree `D:\Recomps\lbp3-wt\netdelay` now has it checked out). Test settings only; the fix is the ladder's.

- `LBP3_RTT_FLOOR_MS=<n>`: a sample below n ms counts as n ms.
- `LBP3_RTT_HIST=1`: every 500 samples, how the raw samples spread.
- [tools/rig3/rf_run.sh](../tools/rig3/rf_run.sh): the run. Logs: `D:\Recomps\lbp3-wt\coop\A\stderr_rtf1.txt` and `B\stderr_rtf1.txt`.

**Not done, and why:**

- **One run of six minutes, in the Pod, with gap-free delivery off.** The bar's own cell (ten minutes, twice, a level start, gap-free delivery as shipped) wasn't run, nor any delay between 0 and 30 ms each way, where a direct path between two homes would sit. Another lane held the game lock for most of this session. Both are [Does the one-frame floor hold through the bar's cell and between 0 and 30 ms of delay?](38-floor-between-0-and-30ms.md).
- **Step 2 wasn't tried.** Its case rests on the assembly, not on a run.
- **Real-time samples weren't tried**, so "the true round trip would cut the budget" is inferred from the spread of the raw samples and from the run with 0.

Findings: `docs/research/netcode-rtt-on-fast-paths.md` (on this ticket's branch, `wayfinder/netcode-rtt-fast-paths`).
