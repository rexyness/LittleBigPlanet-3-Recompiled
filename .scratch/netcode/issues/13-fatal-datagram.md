# Which lost datagram ends a session?

Type: task
Status: resolved
Claimed by: work-through session 3, 2026-11-17
Blocked by: 24, 25
Map: [Online co-op netcode](../map.md)

## Question

[What does the game netcode tolerate, and which of its limits can be reached?](03-game-netcode-limits.md) found that the game repairs loss itself, and that with the gap-free link off a session survived 13 induced single drops. Yet two sessions ended after one guest-to-host loss that nobody induced (run D3, datagram 11605; run T0, datagram 9226): no keepalive from the guest was delivered on the host for 10 s, and the host kicked them. DATA3 was built around that case without knowing what it is. Until it is known, nobody can say whether the link has to hold datagrams back at all.

Do, with two instances on this PC and gap-free delivery off:

- **Log what the datagrams carry.** Put the content of game datagrams in the diagnostics ring: at least the five-byte trailer and each message header. The layout is in the findings file of that ticket, section (c).
- **Drop single guest-to-host datagrams at a period that isn't a multiple of 30.** The guest sends a steady 30 a second, so the earlier drops, exactly 45 s apart, may all have hit the same kind of datagram. That direction at varied points is the untested case.
- **Probe both games once a second, per connection:** the last keepalive, the unacknowledged count, messages held out of order, the last delivered numbers, the send budget and the game's own RTT. The offsets are in the same findings file, "What a later pass needs to settle", item 1.

Done when the fatal case is reproduced and named: which message was lost, and whether the sender stopped resending, the receiver refused the resend, or the send budget starved it. Or when drops across every kind of datagram have passed without one, with the count and the time run.

**A candidate from** [What makes the game netcode cut its send budget, and does the peer link trigger it?](15-send-budget-cuts.md)**:** resends get budget / 30 - 50 bytes per flush. At the 10,240 floor that is 291 bytes, so a flush already holding that much new data sends no resend. Check whether the budget was at the floor in the fatal case and whether the flushes stayed that full for 10 s. The second cut (the game's own, on rising timed-out resends) is at `eboot+0x37d63b`.

**Before starting:** the `fix/coop-online` lane holds uncommitted changes to `p2p_link.*` and `sync_probe.cpp` (2026-11-15). Build on that lane's head once it has landed. The game lock applies to every run.

**Sign-in:** every step here is a two-instance run, so it waits on [Can the three test accounts sign in and find each other?](24-test-accounts-sign-in.md). Build on `fix/coop-online` at `54c538f` ([Has the joining-and-leaving lane landed?](25-joining-lane-landed.md)).

## Answer

Resolved 2026-11-17. The fatal case is reproduced, named and switched off in a test. The test's way of switching it off has a cost of its own (below), so it is not the fix. It is not a kind of datagram: it is a fault in the port's clock that only shows on a path faster than one frame, which is every two-instance run on this PC.

**Which message.** Any message on an ordered stream. The kind of datagram doesn't matter (observed: with the fault masked, 50 drops passed, covering all 30 points in the guest's second and datagrams of 100 to 104, 192 to 195, 246 and 12 bytes; without it, the first real drop was fatal in three runs out of three).

**Which of the three causes.** The sender stopped resending. The receiver never refused anything and the send budget was never cut.

**The chain, link by link:**

1. **The port's frame clock can hand the game a time that is earlier than one it already handed out** (known, `runtime/src/hle/kernel_time.cpp`, `MonotonicNs`). On the main thread and the game host thread, a read within 8 ms of the last flip returns the flip's time, worked out from two clock readings taken one after the other, so it wavers by one 100 ns tick. The guard against going backwards is per thread. Every other thread gets real time, which is up to 8 ms ahead of those two.
2. **So an acknowledgement that comes back within the same frame can be read "before" its message was first sent** (observed, run fd3, the `[rtt]` lines). On the guest: 322 samples of -100 ns, one of -200 and one of -300 in 75 s, each for a message whose first send and last send carry the same time stamp. On the host: one sample of -6.67 ms on its own loopback connection. That the two readings were on different threads is inferred from the per-thread guard; which threads wasn't traced.
3. **The game takes that difference as unsigned** (known, in assembly). At an acknowledgement it computes now minus the message's first-send time (`eboot+0x37f0f1`, message `+0x88`) and turns the nanoseconds into seconds with `eboot+0x1b2a0`, which treats a negative number as a huge positive one: -100 ns becomes 18,446,743,552 s. The sample goes into the current half-second bucket.
4. **Its RTT is the mean of ten such buckets**, so one bad sample makes the RTT billions of seconds for the next 5 s (known, `eboot+0x37d450..0x37d4d7`; observed, the `[conn]` lines: the guest's `+0xf0` reads values such as 1,543,503,872 µs, the low 32 bits of the real figure). In fd2 the guest's RTT was wrong in 72 of 88 seconds.
5. **A message is sent again only after 2 × RTT** (known, `eboot+0x37db87..0x37dc0b`: `+0xf0` ÷ 1,000,000 × the 2.0 at `eboot+0x1061c50`). It reads the stored low 32 bits, so the wait is whatever they happen to be: up to 2.4 hours, and tens of minutes for the values seen. A low word that happened to be small would resend at once, which fits a fault that kills some losses and not others.
6. **One lost datagram then ends the session** (observed, fd1, fd2, fd3). On the host the messages held out of order (`+0x270`) rise by 30 a second from the drop on, the last delivered number stands still, and the keepalive age climbs to 10 s: the kick, 9.4 to 9.7 s after the drop. On the guest the unacknowledged count stays at 1 or 2: it holds the lost message and doesn't send it. The host's own budget was 32,768 and the guest's 16,384, neither ever cut. The candidate from the send-budget research (resends starved at the floor) is ruled out for this case.

**The check that it is the cause** (observed, run fd4). With `LBP3_RTT_CLAMP=1`, which counts a negative sample as 0 (800 of them on the guest in six minutes), the same build took 50 guest-to-host drops in six minutes, one every 217 datagrams: no kick, the keepalive never older than 1.6 s, at most 2 messages held, no run of two flips without a step on either PC.

**What the clamp costs** (observed, fd4, the guest's `[conn]` lines). A sample of 0 is as untrue as the huge one, in the other direction. The guest's RTT read 2.7 to 5.6 ms (median 5.6) where the host's read 33 ms, so its resend wait was about 11 ms and it sent messages again that were only on their way. 71 s after the connection came up the game cut its own send budget (the cut on rising timed-out resends, `eboot+0x37d63b`), and it sat at the 10,240 floor for the rest of the run, cut eight more times. In fd5, with real samples and the same drops, the budget stayed at 16,384 with no cut. So the clamp proves the cause and keeps a session alive, and it would slow every resource transfer on a fast path. The fix has to give the game a true sample.

**It needs a path faster than a frame** (observed, run fd5). With 30 ms of delay each way (RTT 60 ms) and no clamp: no negative sample on either side, the guest's RTT read 54 ms at the end, and 33 drops in four minutes passed, again at all 30 points in the second (at most 6 messages held, longest run of flips without a step 4). Delays between 0 and 30 ms weren't run; the 8 ms is the frame clock's dead zone in the code.

**What that says about the earlier runs:**

- D3 and T0, the two fatal losses this ticket started from, were both two-instance runs on this PC. They are this fault (inferred: same symptoms, same path; their logs have no RTT figure to prove it). T0 surviving 13 induced drops first fits a fault that comes and goes with the samples of the last 5 s.
- The 15 November session ran on the relay path at about 455 ms. No acknowledgement comes back within a frame there, so its kicks are not this (inferred from fd5). They stay with the delays [What did the 15 November session with the friend measure?](01-session-measurements.md) measured.
- Two PCs on one home network, or any direct path under about 8 ms, can hit it (inferred from the code; not run on two PCs). Released builds have gap-free delivery on, which hides loss from the game, so they would be exposed only when the link gives up on a gap.
- "LBP3 can't survive some lost datagrams", the reason DATA3 was built (`p2p_link.h`), isn't true of the game in steady play in the Pod, which is all these runs covered. With a true RTT it repairs a loss in about two round trips. Loss during a join or a level load, and loss with the send budget at the floor, are untested.

**The runs.** Kistopawa hosting, Kistopawa2 joining, LAN path (the link's RTT 0 ms), gap-free delivery off, both moving every 4 s. Logs: `D:\Recomps\lbp3-wt\coop\A\stderr_fd<n>.txt` and `B\stderr_fd<n>.txt`.

| Run | Setting | Guest-to-host drops | Outcome |
| --- | --- | --- | --- |
| fd1 | a drop every 217 datagrams | 2 | kicked 9.5 s after the first |
| fd2 | every 2011 | 1 | kicked 9.7 s after it, 75 s into the session |
| fd3 | every 2011, RTT samples logged | 1 | kicked 9.4 s after it |
| fd4 | every 217, negative samples counted as 0 | 50 | held six minutes, no stall |
| fd5 | every 217, 30 ms delay each way, no clamp | 33 | held four minutes |

In every run the link also dropped the very first datagram of the connection (the counter starts at 0). That one was always survived and isn't counted above.

**What was built** (runtime branch `netcode/fatal-datagram`, commit `8535ebf`, on top of `netcode/session-log`; worktree `D:\Recomps\lbp3-wt\netdelay`, which now has that branch checked out). Test settings only; no fix is in it.

- `LBP3_P2P_TEST_DUMP=1`: a `[p2p-dg]` line with the bytes of every game datagram sent, received or dropped by the loss test.
- `LBP3_CONN_PROBE=1` (with the sync probe): a `[conn]` line per connection object once a second (keepalive age, unacknowledged, held out of order, last delivered numbers, send budget, cap, growth step, bytes sent, RTT), and an `[rtt]` line for every RTT sample above 5 s or below 0.
- `LBP3_RTT_CLAMP=1`: the test fix above.
- [tools/rig3](../tools/rig3/): `launch2.sh` (two instances, with settings for one instance only) and `fd_run.sh` (the whole run).

**Not done, and why:**

- **The datagram's content wasn't decoded.** The bytes on the wire aren't readable as the layout in the limits research: all but a datagram's last bytes look random, so the game packs or scrambles them after that layout. With the cause found in the sender's RTT, the content no longer decides anything. The dump stays for whoever needs it.
- **No drops during a join, a level load or with a cut budget.** The plan had them as a second arm in case steady play passed. It didn't pass, and the cause found doesn't depend on them. They are still untested, and go to [How does today's online co-op hold up at set delays and loss?](06-coop-under-set-conditions.md).
- **Host-to-guest drops weren't run again.** T0 survived seven of them, and the host's RTT never went wrong in these runs (the one negative sample on the host was on its connection to itself).
- **The tick gate wasn't run.** With the settings off, the branch adds nothing on the game's threads; the RTT hook is installed only with `LBP3_CONN_PROBE=1`. `p2ptest` on the branch: 246 checks, 0 failed.

**What follows from it:**

- The fix is its own decision: [How does the port keep the game's RTT right on a path faster than a frame?](32-rtt-on-fast-paths.md).
- [How should the peer link deliver game datagrams?](09-datagram-delivery.md) no longer has an unexplained fatal loss to design around.
- Every two-instance run on this PC at no added delay carries this fault. [How does today's online co-op hold up at set delays and loss?](06-coop-under-set-conditions.md) should run with 30 ms of delay each way as its fastest point until the fix is in: at no delay the fault is there, and the clamp bends the RTT and the budget the other way.
- The `[conn]` line is a start on the game's half of [Which log lines does a session need to measure itself?](19-session-log-lines.md): the same fields, once a second where that ticket wants every 5 s.
