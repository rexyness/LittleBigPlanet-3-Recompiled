# Online co-op: the friend rejoins but doesn't spawn (2026-11-15)

**Report:** the user's friend (on another network) joined the user (host) once without problems. On the second join he loaded into the user's Pod, but his character never appeared.

**Logs:**
- **Friend:** `analysis\online\friend_report_20261115_192711.zip`. It holds the session from his 19:10 to 19:26, and his clock is offset from this PC's (line the logs up on a shared event).
- **Host:** `analysis\play_log.txt`, from the 20:10 start; the game was still running when this was read.
- **Builds:** the host runs `lbp3-recompiled\build\lbp3.exe` (v0.1.5 menu). The friend runs an earlier release, since `IsOnline__` isn't hooked there. Both have exact maths on, and every connection was relayed with an RTT of about 450 ms.

## The connections (host times)

| # | Time | Who connected first | Outcome |
|---|---|---|---|
| 1 | 20:13 to 20:25:11 | host | Worked. At 20:25:11.435 the **friend's game** called TerminateConnection; the host got a BYE ("left") and DEAD with TERMINATED_BY_PEER. |
| 2 | 20:25:36 to 20:26:46 | friend | Pod loaded about 10 s later, but **no spawn**. The friend closed the window at 20:26:46; the host saw no BYE, and its game ended the connection at 20:26:56. |
| 3 | about 20:27:55 to 20:32:21 | friend (restarted game, new port 42742) | Traffic looked normal for 4.5 min. Whether he spawned is not yet known. The host's game ended it after the friend went offline. |
| 4 | 20:33:08 to 20:33:10 | friend (new port 42746) | The **host's game** called TerminateConnection 2 s after ESTABLISHED. |

The link itself was healthy in connection 2: both directions ran gap-free and in order, with nothing skipped.

## What we know

1. **Host send rate as a spawn signature (inferred).**
   - In connections 1 and 3 the host sends about 170–280 game datagrams per 5 s once the guest is in.
   - In connection 2 it stayed at about 150, apart from one 194 at 20:26:03.
2. **A 10 s one-way stall during connection 2's spawn window.**
   - From 20:26:03 to 20:26:13 the host received 27 and then 12 datagrams per 5 s, while the friend sent 150 each time.
   - RTT reached 5–10 s on both sides, and then 426 arrived in a burst, none lost.
   - The host was not stalled: its frames held 33.3 ms throughout. The friend → relay → host path was.
   - It began just as the host's rate started rising (194 at 20:26:03), so a join step in the game may have timed out.
3. **The terminating side is never told its own connection is dead.**
   - Our `sceNpSignalingTerminateConnection` (`np_shadnet.cpp` ~1384) erases the connection and tells the link, but queues no event.
   - LBPPC's shadPS4 (`signaling_handler.cpp` TerminateSig1 → CloseConnectionAndDispatchDead) stages DEAD with `TERMINATED_BY_MYSELF` = `0x80552718` to the game's own callback.
   - Before connection 2, the friend's game had terminated connection 1 itself, and before connection 4 the host's game had terminated connection 3 itself.
   - **Against it:** the host's game also terminated connection 2 itself, yet connection 3 then ran normally. Unproven.
4. **Closing the window sends no BYE (confirmed in code).**
   - `presenter.cpp:310-314` runs the exit hook, which only saves the pipeline cache, and then calls `ExitProcess(0)`.
   - `Link::Stop`, which sends the BYEs, never runs, so the other player waits for a timeout (about 10 s here).
   - The pause menu's Quit Game goes the same way (an SDL quit event).
   - This doesn't cause the no-spawn, but it is a real bug.
5. **LBPPC has a similar known issue** in 0.1.3: "Returning to your pod with a friend can occasionally leave you both stuck … both players restarting the game gets you going again."

## Second report: constant loading screens, and the guest shown as away with a death countdown

**The friend's game goes quiet.**
- It normally sends 150 game datagrams per 5 s (30 Hz).
- In 9 of the 5-second windows from 19:15:20 to 19:23:01 (friend's clock) it sent only 29–121.
- Each of those lines up with a freeze of 0.4–3.5 s on his PC, all of them caused by shader builds: 1.1–4.3 s of building per window, for content he was seeing for the first time on a new shader cache.
- During a freeze his game sends nothing. On the host his character stops, looks away, and LBP starts the death countdown.
- **Fix available now:** background shader builds. His `launcher.ini` has `async_shaders=0`. LBPPC turns them on by default since 0.1.3.

**Other quiet windows without a freeze** are joins and level loads (19:13:40, 19:15:05–15, 19:25:41). Some of these come while the host sends 3–4× its usual amount, for example 20:36:49–20:37:19 on the host: 274–667 sent, 14–130 received. That is most likely level or resource data going to the guest.

**Network:**
- All 6 connections were relayed and never direct. RTT is about 450 ms, against 180 ms from the host to the server.
- One-way stalls of 1–10 s happened in the friend → host direction only: 20:23:52, 20:26:03, 20:41:49 (host clock). The host → friend direction stayed fine.
- That points at the friend's upload or the relay.
- Direct connections (UPnP; the link already accepts a direct address it first hears from) would shorten every load.

**Proposed:**
1. Background shader builds on by default when online.
2. UPnP, so connections can go direct.

## Open questions for the user

- Did the friend spawn on the third join (20:27–20:32)?
- What did each of you see at 20:33, the fourth join?
- At 20:25:11, did he leave on purpose (back to his own Pod), or was he dropped?
- A new report from the friend covering joins 3 and 4 (his launcher keeps only two sessions).

## Next steps

1. **Reproduce locally** with two instances (Kistopawa and Kistopawa2), under the game lock once the user's game is closed:
   - join, have the guest go back to their own Pod, then rejoin;
   - then the same with a 10 s one-way delay injected into the link.
   - Whichever breaks the spawn decides between finding 2 and finding 3.
2. **The DEAD-to-self change, if the repro points to it:**
   - queue DEAD/`0x80552718` for delivery from `sceNpCheckCallback`, not inline;
   - capture the callback, argument, context and connection id before erasing;
   - send it only once per connection.
3. **BYE on exit:** stop the link (send BYEs) in the exit path before `ExitProcess`.

## Reproduced on this PC (2026-11-15 evening)

**Setup:** two instances, `D:\Recomps\lbp3-wt\coop\A` (Kistopawa, host) and `coop\B` (Kistopawa2, guest).
- Exe: the `res` worktree's build, branch `fix/coop-online`.
- Both use the relay path, `LBP3_SYNC_PROBE=1` and remote input.
- Harness: `seq.sh` / `seqn.sh` in the job's tmp `coop\`.
- Sequence: the guest uses Pod computer → friend → Join, and the host allows; spawn checked by moving the guest; then the guest's pause → **Leave to my Pod**, about 25 s, and Join again.

**Result:** every first join spawns. Comparable rejoins, one per fresh pair or the first rejoin of a pair:
- **Failed:** rj2 (no changes), rj3 (DEAD only), rj4 (DEAD + linger), rf1-j2 (both + room probe).
- **Spawned:** rj5 (both + room probe), rw1 (both + probe + watch).

When it fails, the guest loads into the host's Pod but never appears, and its slot for the host stops at join state 9.

Later rejoins in the same pair after a stuck one (rf1 j3–j5) never even connected: the guest's game kept deleting and recreating its signaling context. That is a knock-on failure, not counted.

**Not yet decided:**
- DEAD with `TERMINATED_BY_MYSELF` (0x80552718) to the game that ended a connection (as LBPPC does);
- keeping a closing peer's datagrams flowing for 10 s after a leave (`Config::lingerMs`).

Without the linger change the count is 0/2; with both changes it is 2/4. Both are on the branch (uncommitted), with test switches `LBP3_P2P_TEST_NO_SELF_DEAD=1` and `LBP3_P2P_LINGER_MS=0` for an on/off comparison on the same exe.

## Where it stops (Ghidra and live probes)

**The slot table and its state 9 → 11 step:**
- Slot table: `[eboot+0x130d5c0]`, 8 × 0x300 bytes. `SetState(slot, s, force)` is `eboot+0x3ac240`; states only move forward unless forced.
- State 9 has no timeout.
- The per-frame updater `0x3acef0` moves a state-9 slot to 11 when our own player's entry has an item with flag 0:
  - the room is `[slots+0x1808]+0x30`, 4 players × 0xb0, keyed by online ID;
  - each player's item list is at `+0x50`, count at `+0x58`, 16-byte items with the flag at byte `+8`.
- The probe (`[slots]   room:` lines) shows the guest's item:
  - `{id 0x1_00000000, flag 1}` on the first join, then flag 0 about 4 s later, then state 11;
  - `{id 0x2_00000000, flag 1}` on the rejoin, staying 1 on both sides forever.

**What clears the flag:**
- A hardware write watch (`LBP3_WATCH_JOINFLAG=1`) shows the flag is copied in with a whole room record (`0xbb2f10`, called from `0x375550`).
- Only `[sp+0]` (`0x3757c9`) is a sure caller; the deeper frames are a stack scan. The host's own set-to-1 goes through `0x375550` too, so the clear may be local rather than from a guest packet.
- The receive loop reads its P2P socket (`recvfrom`, `MSG_DONTWAIT`) and gives each datagram to the first connection object (net object `[eboot+0x130d5b8+0x80]`, list `+0x50`, count `+0x58`) whose IP (`+0x34`) and port (`+0x38`) match the sender.
- Each connection object has its own packet sequence (`0x3795b0`) and reliable-message state (`0x37ede0`).

**The game's signaling table** (net object `+0x8`, count `+0x10`, 0x34-byte entries: online ID, state `+0x24`, connection `+0x28`):
- Its callback (`0x3778d0`): DEAD → 5, ESTABLISHED → 2, PEER_DEACTIVATED → 4, MUTUAL → 2 (from 4/5), PEER_ACTIVATED → add or reset the entry by online ID (`0x377eb0`).
- Remove-player (`0x3666f0`) ends a live entry (state 1–3) with TerminateConnection and parks it in state 4.

**Next:** with the connection list in the probe, see whether the rejoin's datagrams reach a fresh connection object or a stale one.

## Session messages: what's missing on a failed rejoin (run rm1, 2026-11-15 23:13–23:16)

**How it was logged:** `LBP3_MSG_PROBE=1`, with `LBP3_SYNC_PROBE=1`, logs every message the main loop takes off a player's queue: `0x3add00` → `0x379ab0`, `[msg]` lines with time, slot, channel, length and the first bytes. The host only takes messages under its own slot, its loopback: what it sends itself. The guest takes them under both slots.

**Working join** (allowed 23:13:34):
1. Host: `cb Kistopawa2` and `11 02 Kistopawa2` (23:13:40).
2. Channel 6: three rounds of `12 04 Kistopawa2` (host to all) and `14 08 Kistopawa2` (host to guest), about 1 s apart.
3. At 23:13:44, in one burst: `16 07 Kistopawa` (350 bytes, the host's player record), `0d 12 Kistopawa` (170 bytes, against 84 before), `e5 … Kistopawa2` (145) and `b8 01 7d 01`.
4. The guest spawns. Its item flag clears on both sides at the same moment, and the guest moves the host's slot to state 11.

**Failed rejoin** (allowed 23:15:07):
- Steps 1 and 2 happen, but the `12 04` / `14 08` rounds repeat every second forever.
- The burst of step 3 never comes.

**Reading:** the host repeats a join step for Kistopawa2 because it never gets the answer it waits for from the guest's game.
- Our link dropped nothing.
- Restarting only the guest between the leave and the rejoin made the rejoin work (rxB, 1 of 1). It matches the user's session, where the friend restarted and joined again.
- So the stale state is most likely in the guest's process.
- A host-only restart (rxA) gave no usable sample: the join request wasn't accepted in time.

**Next:**
- Log what the guest sends in answer to `14 08`. Find the game's send path, or the handler for that type through the table at `slots+0x1840`, and see which guest state it checks. That state is probably left over from the first join.
- For players meanwhile: whoever left restarts the game before joining again.

## The stuck step is a transfer the host never answers (runs rt2, rt4, 2026-11-16)

**The messages:** `12` announces a transfer (from the guest, relayed by the host), `14` is the receiver's answer, `13` is "done". The transfer manager is at `eboot+0x12cc4e8`: outgoing records at `+0x0` (0x30 bytes each, id at +0, state at +0x20), incoming objects at `+0x10` (online ID, id at +0x24, kind at +0x38, stream flags at +0x40).

**Working join:** the guest announces one transfer (id 6, kind 0), then a second (id 8), and both complete within 4 s.

**Failed rejoin:** the guest announces a transfer, gets no usable answer, and re-announces with a new id every 2 s forever (10, 12, 14, …).
- The host creates an incoming object for each announce (kind 0, flags 0x55) and drops it 2 s later.
- The host's answer path is `eboot+0x228a10`. It sends `14` only after its checks pass: `0x229500` (dependencies loaded), `0x229740` (allowed, by kind) and `0x229c60`.
- Which check holds on the rejoin is not yet known.

**Still unexplained:** restarting only the guest made the rejoin work (rxB, 1 of 1), so what the guest announces on a rejoin may differ from a first join.

**Next:** log `0x228a10`'s path per object on the host (which check returns 0), and compare the announce's contents between the first join and the rejoin.
