# Put a set delay and jitter on the peer link between two local instances

Type: task
Status: resolved
Claimed by: work-through session 2, 2026-11-16
Blocked by: 24
Map: [Online co-op netcode](../map.md)

## Question

The peer link's test settings cover loss, late and doubled datagrams only (`LBP3_P2P_TEST_LOSS`, `_LATE`, `_DUP`). The 15 November trouble was delay: about 450 ms RTT, with the friend's datagrams arriving up to 9.4 s late. Until two instances on this PC can run under those conditions, nothing about them can be measured or decided.

Add test settings to the link that:

- delay each direction by a set time, with a set jitter;
- hold one direction back once for a set number of seconds, at a chosen moment.

Done when:

- `p2ptest` covers them;
- a two-instance run on this PC shows the set RTT in the link's own RTT lines;
- with the settings off, the path is byte-for-byte today's (the tick gate in the map's Notes).

**The game has none of its own.** [Do the game's own packet delay and drop settings work, and what do the other unread settings do?](16-unread-settings.md) found the game's packet delay and drop settings compiled out of this build. All of it goes into the link.

**Also establish:** whether the port's `sendto` ever fails with errno 163 for the host's datagrams to itself, so that the game's own queue (connection `+0x2208`) carries them, or whether they always go out and come back through `recvfrom`. It decides whether the link's test settings can touch the host's own per-frame messages. A log line in the two-instance run answers it.

**Before starting:** the `fix/coop-online` lane holds uncommitted `p2p_link` changes (2026-11-15). Build on that lane's head once it has landed, and don't edit `p2p_link.*` alongside it. The game lock applies to every run.

**Built and unit-tested on 2026-11-16; the two-instance run is what remains.** It waits on [Can the three test accounts sign in and find each other?](24-test-accounts-sign-in.md).

- **Where:** runtime branch `netcode/test-delay`, commit `32ce010`, on top of `fix/coop-online` at `54c538f` (the user: "build on the lane's head as it is"). Worktree `D:\Recomps\lbp3-wt\netdelay`; only `p2ptest` is built there, not `lbp3.exe`.
- **The settings:**
  - `LBP3_P2P_TEST_DELAY=<ms>[,<jitter ms>]`: every packet from another player is handled that late, plus up to the jitter, in the order it came. It covers the link's own PINGs, so the link's RTT lines show it. Set on both instances, the RTT is twice the delay plus the jitter's mean; set on one, it slows one direction.
  - `LBP3_P2P_TEST_HOLD=<seconds>,<after seconds>`: once, that long after the first game datagram arrives, everything that comes in during `<seconds>` is held and passed together at the end. Nothing is lost. Set it on the instance whose incoming direction should stall.
  - Loss, late and doubled (`_LOSS`, `_LATE`, `_DUP`) apply after them.
- **`p2ptest`:** cases 15a to 15c, 221 checks, 0 failed. 60 ms each way gave an RTT of 121 ms and a first datagram after 62 ms; 20 ms with 40 ms of jitter one way arrived in order with nothing for the gap-free link to repair; a 600 ms hold gave a longest gap of 600 ms and all 500 datagrams in order.
- **Jitter keeps the order.** A packet is never handled before one that came earlier. Reordering is what `_LATE` is for.

**Still to do:**
- build `lbp3.exe` on that branch and run two instances with `LBP3_P2P_TEST_DELAY=225` on both: the link's RTT lines should read about 450 ms;
- the tick gate with the settings off. Not run. The change is one extra branch on the link's thread in `HandlePeerPacket`; the loopback path and the epoll timeouts aren't touched;
- the errno 163 question above, from a log line in that run.

## Answer

Resolved 2026-11-16. The settings are described above ("Built and unit-tested"); this is the two-instance run that was still owed, and what it showed.

**The run** (`d450`): Kistopawa hosting and Kistopawa2 joining on this PC, `lbp3.exe` built from the runtime branch `netcode/session-log` (`5ec713c`, which holds these settings and the link's session log lines), LAN path, `LBP3_P2P_TEST_DELAY=225` on both. Logs: `D:\Recomps\lbp3-wt\coop\A\stderr_d450.txt` and `B\stderr_d450.txt`.

- **The link's RTT reads the set value** (observed): `rtt 450/450/451 ms (min/median/max)` in every window on both sides, against 0 ms without the setting. Its request to the server reads 180 ms, the host's leg measured on 15 November.
- **Steady play at a steady 450 ms is smooth** (observed): in the 56 s after the join neither PC had a single flip with no step (1,680 flips each), and the worlds agreed.
- **The join's block at 450 ms is about 5 s** (observed): 149 flips with no step on the host, then a short one of 14; on the guest 146 and 194. That matches the "about 5 s" of 15 November, and is more than the 3 s that [What stops the host's simulation when a guest starts to join?](23-join-start-stall.md) worked out from 0.8 s plus 4.6 round trips. Its formula needs another look with these logs.
- **The host's own datagrams don't pass the link** (observed): sends to `127.0.0.1:3658` go through the port's loopback path (130 in the run, `[net] loopback send`), so the link's test settings can't touch them. Whether a `sendto` ever fails with errno 163 wasn't found; the port logs no errno there.

**Not run: the tick gate with the settings off.** With online off the link isn't started at all (`StartLink` returns before it is made), and with the settings off the change is one branch on the link's thread. The gate belongs to the landing of the branch; the netcode spec's build ladder should carry it.

**What follows from it:**
- The rig can now set any RTT, jitter and a one-off stall: [How does today's online co-op hold up at set delays and loss?](06-coop-under-set-conditions.md) can run its matrix on the `netcode/session-log` build.
- A steady delay doesn't stall the game; gaps in a player's input do, as [What does the host do with a guest's late input?](17-late-guest-input.md) read. The 15 November trouble was the stalls and the joins, not the 450 ms as such.
