# Which log lines does a session need to measure itself?

Type: task
Status: resolved
Claimed by: work-through session 4, 2026-11-17
Blocked by: 24, 25
Map: [Online co-op netcode](../map.md)

## Question

[What did the 15 November session with the friend measure?](01-session-measurements.md) could not measure stalls, the moment a guest spawns, the wait behind a gap, the longest time the game got nothing, the delay in each direction, loss in each direction, bytes, or why the game ended a connection. The netcode bar can only hold targets a session can measure, in the two-instance rig and in real play.

Add the lines. Cheap ones are always on while online: a line every five seconds or on a change, nothing per frame.

- **The game's side:**
  - per window: frames shown, simulation steps, frames with no step, and the longest run of them;
  - a stamped line whenever a player slot changes state (that is the spawn);
  - on the host, per window and per guest: the longest gap between two of the guest's INPUTS as they reach the game, and how often the host waited for a guest. [What does the host do with a guest's late input?](17-late-guest-input.md) found that these gaps, not the RTT, stall every player;
  - a stamped line when the catch-up loading screen shows and hides;
  - a stamped line when the byte at `[eboot+0x130d5e8]+0x45` goes to 0 and back: the block at every join in which no PC steps, which step counts can't tell from a wait for input ([What stops the host's simulation when a guest starts to join?](23-join-start-stall.md));
  - the host thread's own slot states (`eboot+0x1322760`, state at `+0x130`). The sync probe prints another table, where a guest stays at "state 1" ([Does a guest that is still loading make the host wait on every frame?](21-loading-guest-wait.md));
  - per window and per connection: the game's send budget, its growth step and the time since its last cut (connection `+0x118`, `+0x12c`, `+0x120`, `+0x124`, from [What makes the game netcode cut its send budget, and does the peer link trigger it?](15-send-budget-cuts.md));
  - datagrams dropped at the port's 256-datagram socket queue (`net_offline.cpp:107`) after the link delivered them;
  - at `TerminateConnection`: seconds since ESTABLISHED, seconds since the last delivery, and the slot's state.
- **The link:**
  - datagrams delivered to the game and the longest gap between two deliveries, with a warning past 2 s and past 5 s;
  - per gap: when it opened, how it closed and how long it took;
  - per window: RTT minimum, median and maximum, and PINGs unanswered;
  - packets in each direction by kind, and bytes;
  - a stamp and a breakdown on the connect lines;
  - the round trip of the link's own ping to the server.
- **Both directions apart:** PONG carries the answerer's clock and how long it held the PING, so each side can estimate the one-way delays.
- **The path:** direct addresses tried, probes sent and direct packets heard per address, the first direct packet's source, and the socket's mapped address as a second outside party sees it. [Which NAT gives the link a mapping per destination: the friend's, the host's or both?](12-which-nat.md) needs these and says what to decide about the second party.
- **The report:** the launcher keeps only two game logs, so the 15 November report missed four of the six connections. Keep the day's, or have the report carry them.

The findings file of the first ticket has the full table ("What the logs cannot show") with the place in the code for each line.

Done when two instances on this PC produce every line, `p2ptest` covers the link's new counters, and the tick gate holds with online off.

**Before starting:** the `fix/coop-online` lane holds uncommitted changes to `p2p_link.*`, `np_shadnet.cpp` and `sync_probe.cpp` (2026-11-15). Build on that lane's head once it has landed.

**Where to build:** on `fix/coop-online` at `54c538f`, on its own branch ([Has the joining-and-leaving lane landed?](25-joining-lane-landed.md)). The link counters and `p2ptest` need no sign-in; the two-instance check at the end does ([Can the three test accounts sign in and find each other?](24-test-accounts-sign-in.md)).

**From** [How long can a joining guest that hangs keep everyone from stepping?](27-hung-join-blocks-all.md)**:** during the block at a join, name who the host is waiting for (the "loaded" bytes of the host thread's entries).

**From** [Can a game take part in a join and never report the game state loaded?](28-never-reports-loaded.md)**:** during the block at a join, the resource bytes the host has sent to the joining guest (the sending places at `eboot+0x1315b18 + 0x8f8 + n*0x618`, bytes sent at `+0xf4`). A watchdog would go by the same number.

**The link's half is built and unit-tested (2026-11-16); the game's half and the two-instance check remain.** The rest waits on [Can the three test accounts sign in and find each other?](24-test-accounts-sign-in.md): the game's lines read the eboot's memory and can't be checked without a session.

- **Where:** runtime branch `netcode/session-log`, commit `5ec713c`, on top of `netcode/test-delay` (`32ce010`), which is on `fix/coop-online` at `54c538f`. It is stacked on the delay branch because both change `p2p_link.cpp`. Worktree `D:\Recomps\lbp3-wt\netdelay`; only `p2ptest` is built there.
- **Done, always on while a connection is up, nothing per datagram:**
  - a `[p2p-win]` line per connection every 5 s: datagrams given to the game and the longest wait between two; gaps opened and how each closed (filled without asking, filled after asking again, given up) with the longest; RTT minimum, median and maximum; PINGs sent and answered; packets by kind and bytes in each direction; direct and relayed packets heard; the round trip of the link's own request to the server;
  - a stamped line when the game has had nothing from a player for 2 s and for 5 s, and one when datagrams come again, with how long it was;
  - a stamped line for each gap that held datagrams back 250 ms or more, or was given up;
  - a `[p2p-path]` line while a connection isn't direct: what each side says its addresses are, and per address tried the probes sent and packets heard; a line for the first direct packet's source;
  - the connect line says when, and how long it took.
- **Both directions apart, as far as two clocks allow.** PONG now carries the answerer's clock (the wire format changed; same version on both sides). The two PCs' clocks aren't the same, so the delay of each direction can't be known, only how far above its own lowest of the last minute each one is. The line prints that as "extra delay out" and "back". In the test a 600 ms hold of one direction shows as 250 ms or more on that direction and nothing on the other.
- **`p2ptest`:** cases 15a to 15d check the counts through `PeerInfo`; 246 checks, 0 failed, four runs.

**Still to do:**
- **The game's side**, all of it: steps and frames with no step per window, slot changes, the host's wait for a guest's INPUTS, the catch-up loading screen, the `+0x45` byte, the host thread's slot states and "loaded" bytes, the send budget, resource bytes to a joining guest, and the `TerminateConnection` line. The lane's sync probe has some of these behind `LBP3_SYNC_PROBE` and `LBP3_TRACE_SIM`; the work is to make cheap always-on lines of them.
- **Datagrams dropped at the port's socket queue** as a count in a window line (today it is a line per drop).
- **The socket's mapped address as a second outside party sees it:** waits on what [Which NAT gives the link a mapping per destination: the friend's, the host's or both?](12-which-nat.md) decides about that party.
- **The report:** the launcher keeps only two game logs.
- **The two-instance check and the tick gate.** The tick gate wasn't run; the changes are on the link's thread and in its packet handlers only.

**From** [Can the host's snapshot go away while a guest is still fetching it?](30-snapshot-replaced-mid-join.md)**:** the lane's `xfer` part reads another manager. Add the resource transfer manager's 32 sending places and 8 download places (`eboot+0x1315b18`), which carry the game state at a join.

**From** [Which lost datagram ends a session?](13-fatal-datagram.md)**:** a first piece of the game's side exists as a test setting: `LBP3_CONN_PROBE=1` prints a `[conn]` line per connection object once a second (keepalive age, unacknowledged, held out of order, send budget, cap, growth step, bytes sent, RTT) from a thread that only reads, and logs RTT samples that are negative or above 5 s. Runtime branch `netcode/fatal-datagram` (`8535ebf`), on top of `netcode/session-log`; build the rest on top of it. Add to the always-on lines: a count of negative RTT samples per window, and the game's RTT next to the link's.

## Answer

Resolved 2026-11-17. The game's half of the session log is built, two instances on this PC wrote every kind of line in three runs and again with the probes off, and the tick gate holds with online off. Two items of the question were moved to other tickets, and two things the lines are meant to show never came up in a run: a datagram dropped at the socket queue, and resource bytes on the move inside a join block (all four are listed below).

**The tick gate** (observed, 19:17 to 19:26, online off, the final build `d1f9d403` put in the clone for the run and the clone's own exe put back after; [tools/tick_gate.sh](../tools/tick_gate.sh)): Introduction steps ≠ 1 at 0.26% and 0.46% of 1,530 flips, the Thruster recipe 3 of 3, and no `[ses]` line in either log. The gate's Bunkum and sky checks weren't run; the constraint names these two.

**What a session now writes** (always on while online; `LBP3_SESSION_LOG=0` turns the game's half off; about one line a second in all):

| Line | When | What it carries |
| --- | --- | --- |
| `[ses] … players:` | a player slot changes state | each player's slot and state in the game's own table, and on the host the host thread's slots (the table that gates INPUTS). A guest is in when its own PC shows the host at 11 and the host thread's slot for it is 11. |
| `[ses] … join block:` | the "simulation running" byte goes to 0, every 2 s while it is 0, and when it is back | how long, who has loaded the game state and who is awaited, resources on the move |
| `[ses] … game state exchange:` | the host thread's list of who has loaded changes | loaded and awaited, by the host thread's slot number |
| `[ses] … resources:` | every 2 s while the resource transfer manager holds something outside a block, and when it empties | each sending place (hash, asker, bytes sent of length) and download place (bytes received of size) |
| `[ses] … catch-up loading screen:` | its flag changes | shown, or hidden after how long |
| `[ses] … the game ends its connection to` | `TerminateConnection` | seconds the connection was up, seconds since the game last got a datagram, how the link was sending, the player's slot state (the last one seen, as the game clears the slot first) |
| `[ses-win]` | every 5 s | flips, steps, flips with no step and the longest run of them, flips with two or more steps, step counter jumps, whether the simulation is stopped, whether the loading screen is up, datagrams dropped at the socket queue. On the host also: frames made for everyone, the longest time between two, waits over 50 ms, catch-up frames, and per PC the INPUTS messages that reached the game with the longest gap between two. |
| `[ses-conn]` | every 5 s, per connection | send budget, cap, growth step, cuts in the window and the time since the last one, the game's RTT beside the link's, the game's RTT samples and how many were negative, unacknowledged, held out of order, keepalive age |
| `[p2p-win]`, `[p2p-path]`, the 2 s and 5 s warnings | as before | the link's half (built on 16 November, above) |

**How it is built** (runtime branch `netcode/session-log-game`, on top of `netcode/fatal-datagram`; worktree `D:\Recomps\lbp3-wt\netdelay`, which now has that branch checked out with a full build):

- A thread of the port's own reads the game's memory ten times a second and prints. It writes nothing to the game.
- The per-flip counts are taken on the game's frame thread, one read of the game's step counter per flip, only while online.
- Three calls in the game are redirected, only while online: the RTT sample at `eboot+0x37f100` (counted, passed on unchanged; `LBP3_RTT_CLAMP` is still a test setting of its own), the call at `eboot+0x39e822` that runs for every INPUTS message on the host thread, and the call at `eboot+0x39f09e` that sends each frame the host built. With online off none of this is installed.
- The code is in `runtime/src/core/sync_probe.cpp` (`SessionLogThread`, `ApplySessionLog`), with `core/session_log.h` for the three callers (`videoout.cpp`, `net_offline.cpp`, `np_shadnet.cpp`).

**The check: every line from two instances on this PC.** [tools/rig3/sl_run.sh](../tools/rig3/sl_run.sh) joins the guest, leaves them 40 s in the Pod, starts the Introduction, leaves 70 s in the level, kills the guest's game and gives the host 16 s. Kistopawa hosting, Kistopawa2 joining, LAN path. Logs: `D:\Recomps\lbp3-wt\coop\A\stderr_sl<n>.txt` and `B\stderr_sl<n>.txt`.

| Run | Delay each way | Join block, host / guest | The game's RTT, host / guest | The link's RTT | Negative samples on the guest |
| --- | --- | --- | --- | --- | --- |
| sl2 | none | 1.0 s / 1.4 s | 33 ms / wrong in 20 of 25 windows | 0.1 ms | up to 18 per window (median 10) |
| sl3 | 30 ms | 1.1 s / 1.7 s | 67 ms / 64 ms | 61 ms | 0 |
| sl4 | 225 ms | 4.2 s / 5.7 s | 467 ms / 444 ms | 451 ms | 0 |
| sl6 | 30 ms, probes off | 1.7 s / 2.2 s | 67 ms / 54 ms | 61 ms | 0 |

**sl6 is the run as a player has it**: `LBP3_SYNC_PROBE=0` and no step trace (`SL_PLAIN=1 sl_run.sh`), where sl1 to sl4 had both on. Both logs have every kind of line below except the loading screen's, and no `[slots]`, `[sync]` or `[sim]` line. The link's `LBP3_P2P_DIAG=1` was still on, as in every rig run. sl5 was the same run with the join not made: the script had waited 150 s for a probe line that never comes, and the guest's first button press was swallowed after that long without input. `launch2.sh` now also takes the session log's own line as "the guest is in its Pod".

sl1 was a first pass on an earlier build and found three faults in the lines, fixed before sl2. sl2 ran before the exchange and resource lines printed on a change, and its level start didn't happen (the host had walked away from the Pod computer); sl3 and sl4 have both.

Which line was seen where (observed):

- `players`, `join block`, `[ses-win]`, `[ses-conn]`: every run, both PCs.
- `game state exchange`: sl3 and sl4, on the host.
- `resources`: sl3 and sl4, one transfer each (below).
- `catch-up loading screen`: sl1, sl2 and sl3 on the host, right after the guest was removed; not in sl4 or sl6.
- `the game ends its connection`: every run, on the host, 9.1 to 9.7 s after the last datagram. It says "the link sends relayed" each time, on the LAN path too: after 9 s of silence the link has stopped trusting the direct address. Read the path from the `[p2p-win]` lines before the silence, not from this line.
- **Not seen with a number above 0:** datagrams dropped at the socket queue. Nothing in these runs fills the queue, so that count is untested.
- **Not seen at all: resource bytes on the move inside a join block**, which is what the item taken from [Can a game take part in a join and never report the game state loaded?](28-never-reports-loaded.md) asked for. Inside a block the places are printed only with the block's own lines (at its start, every 2 s, at its end), so a transfer shorter than that is missed by design, and a Pod's game state is small. The read itself works (a 1,260-byte transfer after the block showed on both PCs with matching numbers). A join into a real level is the test; it is in [How does today's online co-op hold up at set delays and loss?](06-coop-under-set-conditions.md).

**What the lines showed on the way** (observed; none of it was looked into further):

- **The game's RTT reads right once the path is slower than a frame**, a little above or below the link's, and wrong at no delay on the guest, with negative samples in most windows. That is [Which lost datagram ends a session?](13-fatal-datagram.md) again, now visible in every log.
- **The join block is longer on the guest than on the host** by 0.4 to 1.5 s, and grows with the RTT as [What stops the host's simulation when a guest starts to join?](23-join-start-stall.md) worked out (4.2 s on the host at 450 ms).
- **In the exchange the host's own game reports loaded within 0.1 to 0.2 s; the rest of the block is the wait for the guest.**
- **The host thread's list keeps entries that nobody reports loaded.** When the block ends, the list holds a new pair of entries (host and guest), both awaited, and they stay. A level start adds another pair. A leaving player's entries go; the host's own stay. The simulation runs meanwhile, so an awaited entry alone doesn't mean a block: the block is the "simulation running" byte. What these entries are is its own ticket: [What are the entries the host thread keeps awaiting after a join?](34-awaited-entries-after-join.md).
- **A level start is not a join block.** The "simulation running" byte wasn't seen clear. Both PCs had about a second with no step (24 to 26 flips in a row on the host, 27 to 48 on the guest), and on the host the gap in INPUTS was 1.1 to 1.8 s.
- **The only resource seen on the move was 1,260 bytes from the guest to the host**, right after the block. No sending or download place was in use at any of the moments sampled inside a block, also not in the 4 s block of sl4. So the Pod's game state either travels between two samples or not through those places.
- **The host's send budget toward the guest doubles as play goes on** (32,768, then 65,536, 131,072 and 262,144 in sl4) and the guest's toward the host stays at 16,384. The only cuts seen came when the guest's game was killed.
- **After the guest is killed the host stalls in rounds until its kick**: waits of about a second, then catch-up frames (up to 79 flips with no step and 67 catch-up frames in a 5 s window), which is the wait rule of [What does the host do with a guest's late input?](17-late-guest-input.md) at work.
- **Alone in its Pod, a host's own INPUTS come up to 67 ms apart**, with about six waits and six catch-up frames per 5 s and no flip without a step. With a guest in, there were none.

**Left out, and where it went:**

- **The socket's mapped address as a second outside party sees it**: to [Which NAT gives the link a mapping per destination: the friend's, the host's or both?](12-which-nat.md), which decides the party first.
- **The report** (the launcher keeps two logs): its own ticket, [Keep the day's game logs for a friend's report](33-report-keeps-days-logs.md).
- **The frame time each player reports and the one the host hands out**: asked for by [Does the frame time the host hands out explain its slow frames?](22-host-slow-frames.md), which adds it to the host's window line.
- **`p2ptest` covers the link's counters only** (250 checks, 0 failed; four of them are new, for the connection's age and the time since the last delivery that the connection's-end line prints). The game's lines read the eboot's memory and have no unit test; the two-instance runs are their check.
- **The game's lines print while a player is alone and online too** (their own slot counts as a session): one `[ses-win]` and one `[ses-conn]` line per 5 s.
