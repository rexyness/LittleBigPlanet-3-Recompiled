# How long can a joining guest that hangs keep everyone from stepping?

Research for the [online co-op netcode map](../../.scratch/netcode/map.md), ticket 27. Written 2026-11-16. It builds on [What stops the host's simulation when a guest starts to join?](netcode-join-start-stall.md) (ticket 23), answers its open question 1, and corrects one of its points.

## Summary

- **It depends on how the guest hangs. There are three cases, and only two of them end.**
  - **The guest's datagrams stop** (crash, cable, a closed window on a release without the BYE): the host's game removes the guest 10 s after the last keepalive it read. That is the game's own kick, and it runs inside the wait. The peer link's own limit is 15 s, so it comes second. A BYE ends the wait at once.
  - **The guest's link is alive but its game is frozen** (a shader build): the same 10 s. The keepalive is sent by the game's network update on its main thread, so a frozen game sends none, and the link's own thread keeps the connection up, so the port says nothing. A freeze shorter than 10 s only makes the block that much longer.
  - **The guest's game runs but never reports "loaded"**: nothing ends it. Its keepalives go on, and the join state it sits in, 9, has no timeout in either slot table. With two players the host waits without end. With three, the timers that do run are aimed at the wrong players: each player already in has one on the **host**, 240 s, and the host has one on each of **them**, 360 s (whether a guard on removing one's host stops the first was not read). The hung guest is never removed by a timer. Whether a game can get into this case was not found.
- **No `TIMEOUT_...` key of the settings file bears on the wait.** Ticket 23 guessed "the join timeouts, 30 to 60 s". State 9 has none, on the host or on a guest, and every per-slot timeout is multiplied by 4.
- **The players already in get frames from the wait itself, 29 a second, drawn through the game's loading-screen function, and can do nothing in the game.** What the picture looks like was not found. The main loop is not running: no input is used, no menu opens, no notice comes up. Closing the window works, because the port ends the process from its own thread.
- **The rejoin without a spawn is not this state.** In the one failed rejoin with message logging, both games took the "unblock" 2.5 s after the request, and the host's game took its per-frame messages at 30 a second for the next 47 s. The rejoin stops later, with the simulation running. The two share only state 9, and the fact that state 9 never times out.
- **The port can end the wait by saying DEAD on the host's PC.** The wait loop runs the signaling callbacks and the removal on every pass, so a DEAD from the port removes the guest and the host unblocks everyone. No settings key does it. Any cut below 10 s also removes guests whose freeze would have ended.

## How this was read

- **Binary:** `eboot.bin`, CUSA00473 1.28. Addresses are module offsets, written `eboot+0x...`, or bare `0x...` where the context is clear.
- **Ghidra:** a private copy of `D:\Recomps\lbp3-recompiled\analysis\ghidra\lbp3.gpr` made in this session, opened read-only, headless, at low priority, no analysis run (a game was running on this PC). Decompiled here: `0x34a000`, `0x34a760`, `0x34a650`, `0x6fe00`, `0x2361a0`, `0x236060`, `0x236550`, `0x13ef0`, `0x3604a0`, `0x3788a0`, `0x37b3f0`, `0x37d0e0`, `0x377b20`, `0x3778d0`, `0x3666f0`, `0x34e9f0`, `0x366300`, `0x3abee0`, `0x3ac2a0`, `0x3acef0`, `0x3a0900`, `0x3a1970`, `0x3a1c20`, `0x3a1d00`, `0x3a1af0`, `0x39dc90`, `0x39d250`, `0x39fde0`, `0x3a8070`, `0x3a8180`, `0x3a5d50`, `0x349df0`, `0x3994e0`, `0x5caf0`, `0x5173f0`, `0xaacc0`, `0xaabe0`, `0x34af50`. Floats were read straight from `eboot_etexec.elf`.
- **Port source, read-only:** `D:\Recomps\lbp3-recompiled` (`main`) and `D:\Recomps\lbp3-wt\res` (`fix/coop-online` at `54c538f`).
- **Labels:**
  - **known**: read in this session in the decompiler, the disassembly or the port's source, at the place given.
  - **observed**: seen in a log; file and line or time given.
  - **inferred**: reasoned from known or observed facts; the basis is given.
  - **not found**: looked for and not settled.
- **Logs:** the two-instance runs in `D:\Recomps\lbp3-wt\coop\A\` (host, Kistopawa) and `B\` (guest, Kistopawa2); the host's `D:\Recomps\lbp3-recompiled\analysis\play_log.txt` of 15 November (cited `H:line`); the tables of the session measurements (ticket 01, `docs/research/netcode-session-measurements.md` on branch `research/netcode-session-measurements`) for the friend's log. No game was started and the online server was not contacted. The `m1` files of 16 November were not used.
- **Tickets cited by number.** 01 is "What did the 15 November session with the friend measure?" (branch `research/netcode-session-measurements`); 03 is "What does the game netcode tolerate, and which of its limits can be reached?" (branch `research/netcode-game-netcode-limits`); 16 is [Do the game's own packet delay and drop settings work, and what do the other unread settings do?](netcode-unread-settings.md); 17 is [What does the host do with a guest's late input?](netcode-late-guest-input.md); 19 is "Which log lines does a session need to measure itself?"; 21 is [Does a guest that is still loading make the host wait on every frame?](netcode-loading-guest-wait.md); 23 is [What stops the host's simulation when a guest starts to join?](netcode-join-start-stall.md).
- **No log has a hung join.** Every block in the runs read here ends on the host's 0x67 within about 3 s of the request (ticket 23's tables), and no connection of 15 November ended inside a join's block ((1), case B). The "the game ends connection" stacks in `coop\A`, `coop\B` and `play_log.txt` hold no return address inside the wait loop (`0x34a2cb`) either, though those stack scans don't show the main loop's own call under a kick, so that alone would prove little. Everything below about a wait that is ended from outside is from the code.

## The two slot tables at the time of the wait

Ticket 21 set out the two tables. What matters here is the state each holds while every PC waits:

- **known** On the host thread's table (`eboot+0x1322760`, state at `+0x130`), the joining guest goes to 8 on its REQUEST_GAME_STATE and to 9 when the host game's GAME_STATE_HASH passes through, about 65 ms later (ticket 23, steps 1 and 3). It stays at 9 until its REQUEST_CHANGE_PLAYERS, which it sends after the wait.
- **known** In the joining guest's own table (`[eboot+0x130d5c0]`, state at `+0x2f8`), the host is at 8 from the request and at 9 from the hash.
- **inferred** A PC already in the game keeps its states at 0xb through the wait, on the host's table and in its own. Basis: the handlers only raise a state (ticket 21), and nothing on the path forces one down.

## (1) What ends the wait when the joining guest never reports

**The 10 s keepalive kick when the guest's game has stopped sending, whatever the link does. Nothing when the guest's game still sends. No settings key in either case.**

### The wait runs the whole network update, the kick included

- **known** Each pass of the wait loop `eboot+0x34a000` calls, on the main thread only (`0xe1f0`, a per-thread flag): the platform callbacks `0x6fe00`, which include `sceNpCheckCallback`; the network update `0x34a760` with 4 (`0x34a2c6`); and `0x2361a0`, which presents a frame through `0x236060`.
- **known** `0x34a760` in its running state does what the main loop's network update does. In order: it takes removal requests the host thread queued (`0x3a1af0`) and runs remove-player `0x34e9f0` on them; it calls `0x3604a0`; then the game's slot update `0x3acef0`, the party stages `0x3606f0` and the rest.
- **known** `0x3604a0` is the keepalive and the kick (ticket 03): the signaling update `0x377b20`, then for each connection object `0x3788a0`, which takes that connection's control messages (`0x37d0e0`), sends a keepalive when 1.0 s has passed (`eboot+0xe75810`), and runs the test `0x37b3f0`. A failed test calls remove-player at once (`0x360555`). Only after that loop are the sockets flushed and read (`0x375f00`, `0x378b60`).
- **known** The test fails when the connection is more than 10 s old and no keepalive has been read from it for more than 10 s (the float at `eboot+0x1061c4c`, read here as 10.0).
- **known** `0x3604a0` has four callers and no other: the main loop (`0xae86`), `0x34a760` (`0x34aae7`), `0x3994e0` when its flag is set (`0x39954b`), and `0x35f2d0` (`0x35f33d`). `0x3994e0` is called with the flag set from the presenter `0x236060` (`0x23611a`), which itself returns unless `0xe1f0` is true. `0x35f2d0` is a function of the party manager, reached by a jump at `0x35ec9e` from a stub that Ghidra has in no function; its caller was not resolved.
- **inferred** So no thread of its own sends keepalives: they come from whatever the main thread is doing, when that is the main loop, a wait loop or a loading loop that presents frames. Basis: the callers above. The thread of `0x35f2d0`'s caller is the gap.
- **observed** The network update runs on the thread the port names `main` during a block: `[kernel] timed lock #4600000 from eboot.bin+0x34a921 (2000000 us) on 'main'` (`A\stderr_rm1.txt`, between the `0b 14` of 23:15:10.040 and the `67 12` of 23:15:12.538). `0x34a921` is the return from the lock in `0x34a760` after the removal queue.

### What a removal does to the wait

1. **known** Remove-player `0x34e9f0`, on the main thread, queues the player's ID for the host thread (`0x3a1970`, list at `eboot+0x1324378`), frees the game's slot (`0x3ad7c0`), and calls `0x3666f0`, which sends the goodbye, takes the connection object out of the list and calls `sceNpSignalingTerminateConnection`.
2. **known** It does nothing when called with kind 0 while `0x366300` holds: the party stage row is 0, 1, 3, 4, 0xf or 0x10, the row has not passed stage 0x14, and the player is the one in the game's slot role 0 or 1 (the host). That guards a joining game's host, not a guest on the host's PC.
3. **known** The host update `0x3a0900` starts by taking those IDs (`0x3a1c20`) and calling `0x39dc90`, which finds the slot in either array and overwrites it with an empty one (`0x39d250`).
4. **known** At its end the host update calls `0x3a8180`. It drops every "loaded" entry whose slot is no longer in use, and for a key whose remaining entries are all loaded it sends UNBLOCK_LOADING (0x67) to every slot at 9 or more (ticket 23, step 6).
5. **known** On the host's own game the wait can also end without the message: the loop leaves when fewer than two connection objects with an address are left (`0x34a650`, `0x34a35c`). With two players, removing the guest's connection in step 1 does that.

- **inferred** So when the host's game removes the hung guest, the host's wait ends in the same pass or the next, and every other PC's ends one trip later, on the 0x67. Basis: steps 1 to 5; no log shows it.
- **inferred** A PC that is not the host can't end anyone's wait but its own. In a full mesh (ticket 03) a third PC also has a connection to the hung guest and removes it after the same 10 s, but that only frees its own slot; it still waits for the host's 0x67. Basis: the entries and the 0x67 are the host thread's.

### Case A: the guest's datagrams stop

- **known** The host's game reads no keepalive, and 10 s after the last one the test fails. Keepalives come once a second, so that is 9 to 10 s after the datagrams stop.
- **known** The peer link calls a connection dead after 15 s without any packet (`deadAfterMs = 15000`, `p2p_link.h:76`, the same on `main` and on `fix/coop-online`; the check is at `p2p_link.cpp:821`). The game's kick comes first.
- **observed** Outside a join, that order is in the 15 November log: the friend closed his window at 20:26:46 with no BYE, and the host's game ended the connection at 20:26:56.995 (H:8881, the kick's call site `0x37818b`), before the link's 15 s.
- **known** A BYE from the guest's link reaches the game as DEAD on the next `sceNpCheckCallback`, and the wait loop makes that call on every pass. On `fix/coop-online` closing the window sends the BYE (`np_shadnet.cpp:1257..1265`); on `main` it does not. See (4) for the path from DEAD to the removal.
- **inferred** So a join in which the guest's PC drops off costs every other PC a standstill of about 10 s, counted from the drop, or almost none when the guest's port got to say BYE. Basis: the above.

### Case B: the link is alive, the game is frozen

- **observed** A frozen game sends no game datagrams at all. On 15 November each window in which the friend's game sent fewer than 145 datagrams lines up with freeze time of the same size, 30 datagrams per frozen second (session measurements, "A frozen second costs 30 datagrams", F:4085 to F:6871). The keepalive is a message inside those datagrams (ticket 03).
- **known** The link has its own thread and its own keepalive (`p2p_link.cpp:829`, `:949`), so a frozen game does not make the link go dead, and the port says nothing to the other games.
- **inferred** The host's game therefore removes the frozen guest 9 to 10 s into the freeze, by the same test as in case A. Until then the freeze is simply added to the block. Basis: the two points above and the callers of `0x3604a0`.
- **inferred** A game that thaws after more than 10 s removes every other player itself, before it reads what arrived meanwhile. Basis: `0x3788a0` takes only messages already read, then tests, and `0x3604a0` reads the sockets after the loop. The guard of step 2 above may spare the host on a game that is still joining; that path was not followed. So a freeze of more than 10 s is not survivable on either side, and raising the limit in the host's memory alone would not change that.
- **observed** On 15 November no freeze came near it. The friend's longest frame gap was 3,456 ms, and his first freeze of that session began 11 s after the first join's block (19:13:57.0 on his clock, against the join at 20:13:46 on the host's; session measurements, "Freezes"). No connection that evening ended inside a join's block.
- **observed** The block of that first join shows in the host's log as frames at the wait's own pace: the `[video]` windows after H:2513 average 33.6, 35.3 and 35.5 ms, then one of 43.7 ms with a 338 ms gap. That is about 3 s, the length ticket 23 worked out for 455 ms. The same signature can't be used across the whole log: the host ran slow for other reasons later, and 259 windows fall between 34.0 and 36.5 ms.

### Case C: the guest's game runs but never reports

- **known** State 9 has no timeout on the host thread's table: `0x3abee0`, case 9, clears the strike count and returns "fine". Its other cases, with the settings object live: 1 and 2, 120 s (`+0xcc`, `+0xd0`); 3, none; 4 and 5, 50 s (`+0xf8`); 6 and 7, 120 s (`+0xd4`, `+0xd8`); 8, 30 s (`+0xdc`); 10, 30 s (`+0xe0`); 0xb, 90 s (`+0xe4`), and only for a PC with players in the room.
- **known** State 9 has no timeout in a game's own table either: `0x3ac2a0` groups 9 with 1, 2, 3 and 7. Its timed cases: 4 and 5, 50 s (`+0xf8`); 6, 60 s (`+0xe8`); 8, 60 s (`+0xec`); 10, 30 s (`+0xf0`); 0xb, 60 s (`+0xf4`).
- **known** Both functions multiply the value by 4.0 (`eboot+0xe7a4d8`, `eboot+0xe7a504`, both read here) and report a failure only on the fifth call in a row over the limit (ticket 03).
- **observed** A game sat at state 9 for 118 s with nothing ending it: `B\stderr_rj2.txt:7077` (`[1] Kistopawa: state 9 for 118 s, strikes 0`), a failed rejoin, to the end of the run. That is the guest's table; the host's own table is not printed (ticket 21).
- **known** The joining guest's handler waits for the snapshot's resources in `0x236550`, a loop with no time limit of its own: it ends when every resource is loaded or has failed, or when the game is quitting. Each pass calls `0x6fe00` and `0x2361a0`, whose presenter `0x236060` calls `0x3994e0` with the flag set, and that calls `0x3604a0`.
- **inferred** So the joining guest sends keepalives while it loads the state. Basis: that call is the loop's only way to the sockets, and the snapshot does arrive while the guest is in the loop. The conditions on the way were not read: three tests at the top of `0x236060` (`eboot+0x12ec658`, `eboot+0x13a559c`, `0x670570`) and one in `0x3994e0` (`eboot+0x1315b18` not 0).
- **inferred** So a guest that stays in that loop, or in any other loop that presents frames, looks alive to every test there is, and the host's entry for it is never dropped. Basis: the three points above.
- **known** The timers that do run are those of the players already in. In a waiting game the slot update `0x3acef0` still runs (`0x34aaf6`), and its state for the host is 0xb: 60 s × 4 = 240 s since the last per-frame message it took (`0x3ae640` restarts the timer, `0x349df0` restarts it at the end of a block, and `0x3599d0`, not read, calls the same restart). On a failure it calls remove-player **on the host**. On the host's table an in-game guest at 0xb has 90 s × 4 = 360 s since its last INPUTS (`0x39e815`); on a failure the host thread queues it for removal (`0x3a1d00`, taken by `0x3a1af0`).
- **inferred** With two players nothing ends case C: the host's game has no timer on its own host, and the guest is at 9 on both tables. With three or more, each player already in tries to remove the host after 240 s and, if it does, leaves the wait by the loop's "the host is another PC now" exit; the host removes any of them still there after 360 s. The hung guest outlasts them all. Basis: the timers above; no step and no INPUTS are made during the wait (ticket 23). The removal of a host goes through the guard `0x366300` (step 2 of "What a removal does to the wait"); which stage row a player in the game is in, and so whether the guard lets it through, was not read. What a game does after removing its host from inside the wait was not followed.
- **not found** Whether a game can get into case C. Two candidates were seen and not settled: a snapshot fetch that stalls without failing (whether the resource system gives up on its own was not read; when `0x236550` does report a failure the handler builds an empty world through `0x34caf0`, and whether it then still reports "loaded" was not read), and the label step, where a guest goes through `0x34a000` a second time. The rejoin bug shows that a transfer can be retried every 2 s without end ([rejoin_no_spawn.md](../../.scratch/lbp3-port/coop/rejoin_no_spawn.md)), which is the kind of stall that would do it if it hit the snapshot.

## (2) What the players already in see, and whether they can leave

**A loading picture, drawn by the wait itself, and no way out inside the game. Known from the code for what runs; what the picture looks like was not found.**

- **known** Frames come from the wait's presenter `0x236060`, at most one per 34.3 ms (`eboot+0xe65fa4`) while the busy count `eboot+0x13a55b8` is above 0. It calls the frame function `0x5caf0`, which calls `0x5173f0` on every frame. With the busy count above 0 that function draws the game's loading layer: a full loading screen when `0x5171a0` says so, otherwise a colour over the frame (`0x411740`).
- **observed** In a block the host's frames take the whole 34 ms and nothing is redrawn from the scene: `[video] 8940 frames flipped (avg 35.1 ms … work 33.5 ms, slot wait 1.7 ms` and `[fills] per frame: 0.0 refreshes … 1.0 cleared`, against `work 16.4 ms` and `1.3 refreshes … 3.5 cleared` in the window before (`A\stderr_rm1.txt`, after the `0b 14` of 23:15:10.040).
- **not found** What is on the screen: a frozen picture with a loading mark, or a full loading screen. It needs a look at a live join; a normal join shows it for 0.8 to 3 s.
- **known** Nothing else of the frame runs. The wait is inside a message handler, called from the main loop; the loop's own work (the step, the INPUTS sender, the game's menus) does not run until the handler returns (ticket 23). The wait calls only `0x6fe00`, `0x34a760` and `0x2361a0`.
- **inferred** So pad input does nothing during the wait and the pause menu can't be opened. Basis: the calls above. Whether the pad is read at all meanwhile was not settled: `scePadReadState` is called from `0x64f10` only, which is reached through `0x65940`, a function that `0x638d0` refers to as data (a thread's entry, by the look of it); the presenter calls `0x63c00` and `0x62c70`, which the main loop calls just before it sends INPUTS, and those were not read. Either way nothing that acts on input runs. That no menu is driven from `0x34a760` or the presenter was not checked function by function.
- **known** The two notices found for a stalled session don't come up. The "no step for 3 s" notice of ticket 17 is raised by the main loop after its step loop, which is not running. The notice in `0x3acef0` needs the host's slot to be empty, and after 40.0 s (`eboot+0xe7b634`) of that it leaves the session (`0x3602b0` with 3). Other notices were not looked for.
- **known** The wait ends when the game is quitting: `0x13ef0` is true once a quit was asked for through `0xaabe0` (called from `0x90d600` and from `0xa37`/`0xa66` in `0x890`). **not found:** who asks on a PC.
- **known** Closing the window works at any time. The port's presenter thread, not the game's main thread, runs the exit hook and calls `ExitProcess` (`presenter.cpp:316` on `fix/coop-online`, `:314` on `main`). On `fix/coop-online` the hook stops the link, which sends the BYEs.
- **inferred** What leaving does for the others. If the hung guest's player closes the window, the wait ends for everyone: at once with the BYE, after 10 s without. If the host does, each guest's game hears DEAD or runs into the kick, removes the host and leaves the wait by the "another host" exit. If a third player does, the host drops their entry with their slot and goes on waiting for the hung guest. Basis: (1); none of it is in a log.

## (3) Whether the rejoin without a spawn stops here

**No. That rejoin gets through the wait and stops afterwards, with the simulation running. Observed in one run.**

- **observed** Run rm1, the failed rejoin (allowed at 23:15:07). The host's game takes the request `0a 13` at 23:15:09.973, the hash `0b 14` at 10.040, the unblock `67 12` at 12.538 and the labels `e8` at 12.623 (`A\stderr_rm1.txt:12858`, `:12862`, `:12884`, `:12887`). The guest's game takes them at 10.228, 10.233, 12.751 and 12.828 (`B\stderr_rm1.txt:22036` and the 1st, 38th and 39th `[msg]` lines after it). The times match the working first join of the same run to within 0.1 s (ticket 23).
- **observed** After that the host's game takes per-frame messages (`8f`) again: the first at 23:15:12.691 (`A\stderr_rm1.txt:12924`), then 30 a second to the end of the log at 23:16:00 (2,870 `[msg]` lines of it, two with the same stamp per frame), and its frames are back at 33.3 ms (`:13016`). The guest's game takes them too. Its `[msg]` lines of every type stop at 23:15:39.445 (`B\stderr_rm1.txt:24877`) while its log and its datagram counts run on to 23:15:58 (`:25016`: 150 sent, 154 received); why the probe's lines stop there was not followed.
- **inferred** So the host is stepping. Basis: the main loop tests the byte at `+0x45` before it takes a per-frame message (ticket 23), and it steps on each one it takes (ticket 17). rm1 has no `[sim]` lines.
- **observed** Meanwhile the guest's table has the host at state 9 for 49 s, with its own item flag still 1 (`B\stderr_rm1.txt:25022`, `:25023`).
- **inferred** So the two are different stops. The hung join stops before the 0x67, with the byte at `[eboot+0x130d5e8]+0x45` clear on every PC and nobody stepping. The rejoin stops after the 0x67 and the 0x68, before the guest's REQUEST_CHANGE_PLAYERS, with the byte set, the host taking its frames and only the guest's players missing; the other lane has traced it to a transfer the host never answers. Basis: the lines above. One run; rt2 and rt4 have no `[msg]` lines for the unblock.
- **known** Where they meet: both sit in state 9, in the guest's table and (for the hung join, and by the sequence also for the rejoin) on the host's, and state 9 has no timeout in either (`0x3ac2a0`, `0x3abee0`). That is why a failed rejoin stays as it is until someone leaves, and why case C of (1) has no end. A timeout on state 9 would bound both, and it is the eboot's to have or not.

## (4) Whether the port can end or shorten the wait

**By saying DEAD on the host's PC, yes: the path is live inside the wait. By a settings key, no. Whether it should is a trade against guests who would have come back.**

### A settings key

- **known** No key bounds the wait. The states both PCs are in have no timeout ((1), case C), and the wait loop itself has none (ticket 23).
- **known** The keys that do run during a wait are the 0xb ones: `TIMEOUT_WAIT_FOR_UPDATE` (a game's timer on its host, 60 s × 4) and `TIMEOUT_WAIT_FOR_UPDATE_FROM_CLIENT` (the host's on a guest in the game, 90 s × 4). They act only with three or more players, only in case C, and they remove the players who are fine. Lowering them would not free anyone from a hung guest; it would make a long standstill of any cause end sessions sooner.
- **known** The keys for the states before the wait (`..._LOCAL_CLIENT_TO_SAVE_GAME_STATE`, 30 s × 4, host state 8; `..._HOST_TO_SAVE_GAME_STATE`, 60 s × 4, guest state 8) cover the 65 ms before the hash, not the wait.
- **known** The port does not rewrite the settings file today. `FixUpReply` (`http_online.cpp:251`) answers three other paths; ticket 03 found that it could.

### The kick limit in memory

- **known** The 10.0 at `eboot+0x1061c4c` is in writable data and only read (ticket 03). Lowering it shortens cases A and B directly.
- **inferred** It is the wrong lever. The same float kicks a guest whose link stalls in the middle of play (a delay of 9.4 s came within a second of it on 15 November, ticket 01), and it sets the 9 s threshold for accepting session messages (ticket 16). Basis: those two findings.

### DEAD from the port

- **known** The link's events are handed to the game's signaling callback from `sceNpCheckCallback` (`np_shadnet.cpp:1290..1345` on `fix/coop-online`), and the wait loop calls that on every pass (`0x6fe00`).
- **known** The callback `0x3778d0` puts the connection's entry in state 5 on DEAD, whatever the error code, when the signaling object's mode (`+4`) is 1. The probe reads that field (`sync_probe.cpp:224` on `fix/coop-online`) and prints `signaling mode 1` (`B\stderr_rm1.txt:25023`).
- **known** `0x377b20`, the first thing `0x3604a0` does, takes every entry in state 5 out of the table and calls remove-player for it. It does nothing while the byte at `eboot+0x130d8f0` is 0; what clears that byte was not read. From there the removal is as in (1): the host thread drops the slot and its entry and sends the 0x67.
- **inferred** So a DEAD for the hung guest, said by the port on the **host's** PC, ends the wait for everyone within a frame or two plus one trip. Said on another PC it only removes the guest there. Basis: the chain above, read end to end; never run.
- **inferred** The port has what it needs to decide. It counts the game datagrams each peer sends (the `[p2p]` lines), so "this peer's link answers but its game has sent nothing for N s" is known to it: that is case B, N seconds in. For case C it would have to read the game: the byte at `[eboot+0x130d5e8]+0x45` clear for longer than any join takes, and the host thread's entries (the map at `+0x2d8` of the object at host `+0xa10`, "loaded" byte at `+0x40` of an entry) naming who has not reported. The sync probe already reads game memory this way. Basis: (1) and ticket 23.
- **inferred** The cost. In cases A and B the game already acts at 10 s, so the port can only win the difference, and every second it wins is a second in which a frozen guest might have thawed and stayed. The longest freeze of 15 November was 3.5 s, and background shader builds, which the joining-and-leaving lane turns on by default online, take away the known cause. In case C the port would be the only thing that ever acts, and there the cost is nil: a guest that has not loaded the state in, say, 60 s is not coming. Basis: (1); no measurement of how long a real join's wait can honestly take with a large level and a cut send budget (ticket 23's open point), which is what a limit for case C has to clear.
- **known** None of this touches the simulation or "keep our tick": it is an event the game already handles, raised at a time the port chooses.

### What already helps

- **known** The BYE on closing (`fix/coop-online`) turns the commonest case A, a joining player who gives up and closes the window, from a 10 s standstill for everyone into none.
- **inferred** A direct path shortens every wait that does end normally (ticket 23), and so narrows the window in which a freeze or a drop can fall inside one.

## What a later pass needs to settle

Not found, listed in the sections above: any log of a wait ended from outside; the caller of `0x35f2d0` (the one caller of the kick's function whose thread is not known); whether a game can run and never report (a snapshot fetch that stalls without failing, the failure path through `0x34caf0`, the second pass through the wait for labels); what a game does after it removes its host from inside the wait; what the screen shows during the wait; who sets the game's quit flag on a PC; what clears the byte at `eboot+0x130d8f0`; the third restart of the 0xb timer (`0x3599d0`).

Corrections to earlier findings:

- Ticket 23, (4): "a slot goes by the 10 s kick or the join timeouts (30 to 60 s in the `[slots]` lines)". Only the kick applies. State 9, where the joining guest is for the whole wait, has no timeout on either table, and the timeouts that exist are four times the values the `[slots]` lines print.
- Ticket 23, summary and (4): "how long that can take was not found". 9 to 10 s when the guest's game has stopped sending; without end when it has not.
- [rejoin_no_spawn.md](../../.scratch/lbp3-port/coop/rejoin_no_spawn.md), "State 9 has no timeout": confirmed for the game's table, and the same holds for the host thread's table, which that note did not have.

Questions this raises for the map:

1. **Does a frozen joining guest end the wait at 10 s as the code says, and what is on the screen meanwhile?** A two-instance run with `LBP3_TRACE_SIM=1` and `LBP3_SYNC_PROBE=2`: suspend the guest's process right after it takes `0b 14` (case A and B together, since a suspended process stops its link too), and once more with only the game's main thread suspended (case B alone). Expected on the host: `[sim]` zeros for 9 to 10 s, then "the game ends connection" with a stack through `0x360555` and `0x34aaec`, then steps. It needs the test accounts' sign-in.
2. **Can a game run and never report "loaded"?** Static first: the resource system's own give-up for a fetch from a peer, what `0x34af50` does after `0x34caf0`, and the label pass. If it can, the port's watchdog of (4) is the only thing that would ever end it, and its limit needs the longest honest wait, which is ticket 23's question on the size of the game state at a cut send budget.
3. **Should the port say DEAD for a peer whose link answers but whose game has been silent for some seconds, and at how many?** It only moves the 10 s forward, for joins and for play alike. It belongs with the netcode bar: how long a standstill the players should sit through for the chance that a frozen guest comes back.
4. **Should the session log lines of ticket 19 name who the host is waiting for?** The byte at `+0x45` gives the standstill; the "loaded" bytes of the host thread's entries give the PC that causes it. Together they would have answered this ticket from a log.
5. **A game that thaws after more than 10 s removes everyone before it reads its sockets.** Inferred from the order of calls in `0x3604a0`. If it holds, no setting on the other PCs can save a player from a freeze of that length, and the kick limit would have to be raised on every PC to matter at all.
