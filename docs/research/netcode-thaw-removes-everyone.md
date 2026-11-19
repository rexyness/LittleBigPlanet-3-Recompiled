# Does a game that thaws after more than 10 s remove every player before it reads what arrived?

Research for the [online co-op netcode map](../../.scratch/netcode/map.md). Written 2026-11-16. It checks an inference made in [How long can a joining guest that hangs keep everyone from stepping?](netcode-hung-join-blocks-all.md) (case B and question 5 there) and corrects it.

## Summary

- **No. The game reads its socket before it runs the 10 s test, on every pass.** The earlier inference read the call order one level too high. Inside the per-connection update, the function that takes a keepalive calls the socket read itself before it takes anything. Checked in assembly: `0x3789cd` (take) comes before `0x378a32` (test), and the take reaches the `recvfrom` loop at `0x379b1b`. The order is inside `eboot+0x3604a0`, so it is the same for all four of its callers.
- **The test measures the game's own clock at the moment the game takes a keepalive out of its ordered stream.** The port stamps nothing. The clock is `sceKernelClockGettime(4)`, which the port answers from the performance counter; it runs through a freeze.
- **So a thawing game most likely passes its own test.** That part is inferred: the read is certain, but the stamp needs one keepalive from each player to come out of the stream on that pass. The bridge keeps the first 256 datagrams of a freeze and drops later ones, which leaves the oldest keepalives in order.
- **It does not save the session, because the other PCs end it.** Each of them runs the same test on the frozen PC and removes it 9 to 10 s into the freeze. Their links then say BYE, the frozen PC's link takes it on its own thread, and the thawed game hears DEAD and removes them in its first network update. A session with a freeze of more than 10 s is lost from the other side whatever the thawing side does.
- **Sparing the thawing side can't matter by itself.** There is nothing to spare there. A freeze of more than 10 s can only be survived if every *other* PC holds its kick back, which means the kick limit (10.0 at `eboot+0x1061c4c`) raised on every PC, or each port keeping the frozen player's keepalive time fresh in its own game's memory while that player's link is alive. Neither touches the loopback path or the epoll timeouts. Whether the simulation recovers from such a freeze was not found.
- **No log holds such a freeze.** 33,315 `[video]` lines in 66 logs; no frame gap of 5 s or more.

## How this was read

- **Binary:** `eboot.bin`, CUSA00473 1.28. Addresses are module offsets, `eboot+0x...` or bare `0x...`.
- **Ghidra:** a private copy of `D:\Recomps\lbp3-recompiled\analysis\ghidra\lbp3.gpr` made in this session, read-only, headless, low priority, no analysis run. Decompiled: `0x3604a0`, `0x3788a0`, `0x37b3f0`, `0x37d0e0`, `0x379ab0`, `0x379bf0`, `0x379d10`, `0x378b60`, `0x3795b0`, `0x37ede0`, `0x37f270`, `0x37f190`, `0x375f00`, `0x378fe0`, `0x377b20`, `0x36ac00`, `0x37a430`, `0x3994e0`, `0x37cff0`, `0x1b1b0`, `0x1b2a0`, `0x1b300`, `0x1b3f0`. Disassembled: all of `0x3604a0` and `0x37b3f0`; `0x3788a0` to `0x378a71`; `0x379ab0` to `0x379b86`; `0x37d243` to `0x37d313`; the four call sites of `0x3604a0`; the four call sites of `0x36ac00`; the call sites of `0x39f0e0`. Floats and the class table were read from `eboot_etexec.elf`.
- **Port source, read-only:** `D:\Recomps\lbp3-wt\res` (`fix/coop-online` at `54c538f`); `D:\Recomps\lbp3-recompiled` (`main` at `29dad5a`) for the BYE in `Link::Terminate` and the queue limit.
- **Logs:** the host's `D:\Recomps\lbp3-recompiled\analysis\play_log.txt` of 15 November (`H:line`); the friend's report `analysis\online\friend_report_20261115_192711.zip`, unpacked to a temp directory (`F:line` is `play_launcher_log.1.txt`; his clock is offset from the host's); the two-instance logs `D:\Recomps\lbp3-wt\coop\A\stderr*.txt` and `B\stderr*.txt`. The `*_m1.txt` files were not used. No game was started and the online server was not contacted.
- **Labels:**
  - **known**: read in this session in the disassembly, the decompiler or the port's source, at the place given. Four things are taken as known from the earlier findings and were not read again: what remove-player `0x34e9f0` and `0x3666f0` do (goodbye, flush, `sceNpSignalingTerminateConnection`), that the signaling callback `0x3778d0` puts a connection in state 5 on DEAD, that `0x3602b0` is the leave-session call, and that `0x3a0900` is the game host thread's update.
  - **observed**: seen in a log; file and line given.
  - **inferred**: reasoned from known or observed facts; the basis is given.
  - **not found**: looked for and not settled.

## (1) The order of the read and the test

**The read comes first, inside the same per-connection call. Known, in assembly.**

### The chain

1. **known** `0x3604a0` calls the signaling update `0x377b20` (`0x360507`), then loops over the connection objects and calls `0x3788a0` on each (`0x360535`). When that sets its flag, remove-player `0x34e9f0` is called at once (`0x360550`). After the loop come the two flushes `0x375f00` (`0x36059a`, `0x3605ce`) and one more socket read `0x378b60` (`0x3605dc`).
2. **known** `0x3788a0` calls `0x37d0e0` at `0x3789cd`, then sends a keepalive when 1.0 s has passed (`0x378a17`, float at `eboot+0xe75810`), then calls the test `0x37b3f0` at `0x378a32`. It gets that far only for a connection whose player ID is valid, whose address is set and whose port is not 0xffff (`0x3788be..0x3788f2`). Any other connection is not tested.
3. **known** `0x37d0e0` calls `0x379ab0` with class 0 at `0x37d178` (`0x37d16a  XOR ESI,ESI` sets the class), once and not in a loop, and `0x3789cd` is its only caller: one control message per connection per pass.
4. **known** `0x379ab0` finds the connection (`0x379bf0`, `0x379afb`), then at `0x379b08..0x379b1b`:
   ```
   00379b08  LEA EAX,[R14 + -0x5]     ; R14 = class
   00379b0c  CMP EAX,0x2
   00379b0f  JC  0x00379b20           ; classes 5 and 6 skip the read
   00379b1b  CALL 0x00378b60          ; every other class reads the socket first
   00379b49  CALL 0x00379d10          ; then one message of the class is taken
   ```
   Class 0 does not skip.
5. **known** `0x378b60` is the game's only `recvfrom` (one call site, `0x378c98`). It reads the P2P socket until nothing is left, with a 1,600-byte buffer and flag 0x80, and hands each datagram to the connection it came from (`0x3795b0`, at `0x378d4a`). So one call empties the socket for **every** connection, not only the one being updated.
6. **known** `0x3795b0` puts each message of a datagram into the connection's list at `+0x268` and calls `0x37ede0`; for the two ordered streams that calls `0x37f270`, which moves every message whose number is the next one into the stream's buffer (`+0x40` or `+0x58`). A message behind a missing one stays in the list.
7. **known** `0x379d10` takes the first message of the asked class out of the stream's buffer. For class 0 the table at `eboot+0xe1d344` gives 3, which means "the first ordered stream, then the second".

- **known** So on the pass after a thaw, the first connection that is tested empties the socket before its own test, and every later connection finds its messages already in its streams.
- **known** `0x379bf0` succeeds for any connection that got through step 2: it asks for the same valid ID and set address.

### Every caller of `0x3604a0`

- **known** There are four callers and no other: the main loop (`0xae86`), `0x34a760` (`0x34aae7`), `0x3994e0` with its flag set (`0x39954b`) and `0x35f2d0` (`0x35f33d`). Each passes the manager and two player IDs; none passes anything that changes the order, which is fixed inside `0x3604a0` and `0x3788a0`.
- **inferred** In the main loop the platform callbacks `0x6fe00` run at the top of the frame (`0x9e29`), before both `0x34a760` (`0xa0dd`) and `0x3604a0` (`0xae86`). Basis: the order of the three call sites by address inside `0x9d70`; the loop's branches were not followed.

### The other places the test runs

- **known** `0x37b3f0` has two more callers.
  - The flush `0x378fe0` (`0x37905d`). A connection that fails gets nothing sent on that flush. Nothing is removed there. The main loop also flushes later in the frame (`0x375f00` from `0xc725`), so a flush can run after a thaw and before the next read; it costs one skipped send.
  - `0x36ac00` (`0x36ac88`), a status query: 3 for "no such connection or the test fails", 2 for "no address or no keepalive ever", 0 for "fine". It is called from the party stages `0x3606f0` (`0x3638aa`, `0x36399b`, `0x364cb3`) and from `0x369ec0` (`0x36a2ae`), which `0x3606f0` calls.
- **known** `0x3606f0` has two callers, `0x34a760` (`0x34ab14`) and `0x35f2d0` (`0x35f35c`). In both it runs a few calls after `0x3604a0` in the same function, so after the read.
- **known** What a 3 does at those sites: at `0x3638aa` the game calls `0x3602b0` (the leave-session call of the earlier findings) and `0x3aea50`; at `0x364cb3` it stores the time at `+0x258` of the stage row; at `0x36a2ae` it sets the connection up again (`0x366450`); at `0x36399b` it only skips.
- **not found** Which party stage and which player the `0x3638aa` site tests. It can only see a stale time if the freeze falls between `0x3604a0` and `0x3606f0` in the same update (`0x3acef0`, `0x421c0`, `0x339010` lie between), not in the frame's drawing.

## (2) What the test measures against

**The game's own clock, read when the game takes the keepalive out of the stream. Nothing of the port's. Known.**

- **known** The test `0x37b3f0`, in order:
  1. A connection to oneself passes (`0x37b405`).
  2. If a goodbye was taken (`+0x168` not 0) more than 20.0 ms ago, fail (`0x37b412..0x37b43b`, float 20.0 at `eboot+0xe75844`; the converter here is `0x1b300`, not `0x1b2a0`, and the earlier findings read its unit as milliseconds; its factor was not read again).
  3. If the connection's start time `+0xc0` is more than 10.0 s back (`0x37b494`) and either no keepalive was ever taken (`+0x160` is 0) or the last was taken more than 10.0 s ago (`0x37b4b6`), fail.
- **known** `+0x160` has the writer the earlier findings gave: `0x37d2d3`, in `0x37d0e0`, after a control message with code 0. No scan for other writers was made in this session; the earlier findings name this one and the set-up. The value is a fresh read of the clock at that instruction (`0x37d2ce  CALL 0x1b1b0`). It is not the time the datagram arrived: `0x3795b0` does stamp each parsed message with its own clock read (`0x1b1b0`, stored with the message in the `+0x268` list), and the test does not use that.
- **known** `0x37d0e0` takes one control message per call and acts on its code: 0 stamps `+0x160`, 1 (the goodbye) stamps `+0x168`, 2 (the congestion notice) cuts the send budget. So a pass refreshes the keepalive time only when the first class 0 message in the stream is a keepalive.
- **known** The clock `0x1b1b0` is `sceKernelClockGettime(4, ...)` in nanoseconds, less the first reading (`eboot+0x1190c70`). `0x1b2a0` turns a difference into seconds.
- **known** The port answers clock 4 with `MonotonicNs()` (`kernel_time.cpp:108`, `:222`): the performance counter, and on the main thread and the game host thread the frame-steady variant, which holds a reading at most 8 ms back after a flip and never goes backwards. It counts through a freeze of the main thread and through a suspended process.
- **known** The floats, read from the file: 10.0 at `eboot+0x1061c4c` (writable segment), 1.0 at `eboot+0xe75810` and 20.0 at `eboot+0xe75844` (read-only). The 10.0 has one other reader, `0x356540` (`0x35661f`).
- **known** The port has no part in the stamp. The bridge only queues datagrams for `recvfrom` (`net_offline.cpp:421..449`); the times live in the game's connection objects.

### What follows for the thaw pass

- **inferred** A game whose main thread was frozen for more than 10 s passes its own test on the first pass, for each player whose stream gives up a keepalive on that pass. Basis: (1), and the stamp being "now". It needs three things, none of them run:
  - **The datagrams are still there.** The link runs on its own thread (`p2p_link.cpp:949`), which receives (`:881`) and hands each game datagram to the bridge from there (`:666`, `:335`; the sink is set at `np_shadnet.cpp:1241..1243`). The bridge keeps 256 per socket and drops what comes after (`net_offline.cpp:107`, `:438`), so the **first** 256 of the freeze are kept, in order. At 30 datagrams a second from one player that is 8.5 s, and about 4 s with two; either way the oldest keepalives are among them.
  - **The first control message is a keepalive.** A congestion notice ahead of it would use up the pass's one take. A player sends that notice when it receives a datagram (`0x3795b0`, at most every 0.1 s, `eboot+0xe75840`), and it receives none from a frozen game.
  - **The player is still sending.** It is, until its own test removes the frozen PC ((5)).
- **inferred** The drops past 256 leave a gap in the ordered streams, which then wait for the other player's resend (the game's own, after twice its RTT). Keepalives already taken cover the first passes, one per pass; whether new ones come out within the next 10 s was not followed. Basis: step 6 of (1) and [What does the game netcode tolerate, and which of its limits can be reached?](../../.scratch/netcode/issues/03-game-netcode-limits.md).
- **known** On a PC that hosts, the main thread is not the only reader. The host update `0x3a0900` calls `0x39f0e0` with classes 3, 5 and 7 (`0x3a0629..0x3a0878`), and `0x39f0e0` calls `0x379ab0` (`0x39f14d`), which reads the socket for classes 3 and 7.
- **inferred** So while a host PC's main thread is frozen, its game host thread may go on emptying the socket into the streams, and the 256 limit would not be reached there. Basis: the calls above; whether that thread keeps running during a freeze of the main thread, and whether it runs at all on a PC that is a guest, was not read.
- **inferred** A whole process that is suspended is a different case. Its link thread stops as well, and on resume the main thread may run its update before the link has handed over anything, so the read finds nothing and the test fails. Basis: the link feeds the bridge from its own thread; the order after a resume is a race and was not read.

## (3) Evidence in the logs

**Not found: no log holds a freeze of that length.**

- **observed** Frame gaps. Every `[video] … frames flipped (… worst N ms …)` line was scanned: 33,315 lines in 66 files (`play_log.txt`, the friend's two launcher logs, 63 `stderr*.txt` in `coop\A` and `coop\B`). No line has a worst gap of 5,000 ms or more. The largest are about 3.0 s at start-up in every log (`H:968`, 3,033 ms; before any session), 3,455.5 ms in the friend's (`F:4090`), and 2,007 ms in the two-instance runs (`B\stderr_t1.txt:984`).
- **observed** The bridge never filled. No log has the line `the queue of socket … is full`, and the largest `queued` figure in the `[net]` summaries read is 5 (`A\stderr_rxB.txt`, `B\stderr_rf1.txt`).
- **observed** The friend's report covers two launches only, so four of the six connections of 15 November have no log from his side ([What did the 15 November session with the friend measure?](../../.scratch/netcode/issues/01-session-measurements.md)).
- **observed** The path by which the other side's ending reaches a game is in the 15 November logs, outside any freeze. The friend's game ended connection 1 at 19:25:11.435 on his clock (`F:7819`). The host's link logged `[p2p] 20:25:11.937 <friend>: left` (`H:8007`), and the host's game got `signaling event 0 (error 0x80552710) on context 1, connection 1` (`H:8010`), which is TERMINATED_BY_PEER (`np_shadnet.cpp:1107`). The host's game made no call to end that connection itself: its next `the game ends connection` line is connection 2 at 20:26:56.995 (`H:8881`).
- **observed** Many reads per frame. With one guest connected, the `[net]` summary counts about 6,400 receive calls in 5 s against about 450 that got data (`A\stderr_rm1.txt:5666`, `:6023`, `:6372`). That fits a socket read before every take, but the count is over all sockets, the loopback ones included, so it proves nothing by itself.
- **observed** The two paths leave different lines. The game's own keepalive kick prints `the game ends connection` with `eboot.bin+0x360555` on the stack, the return address of the remove-player call in the loop (`H:8881`, `H:8893`, the kick of 20:26:56.995). An ending by the other side prints `left` and `signaling event 0` and, in the one case above, no `the game ends connection` line and so no stack at all. Seen once, on the build of 15 November; on `fix/coop-online` the lines after a BYE were not checked.

## (4) Whether the port could spare the session

**On the thawing side there is nothing to spare. On the other PCs there is, and it costs nothing against "keep our tick".**

### The levers named in the ticket

- **A hook that lets the read run before the test.** Not needed: the game does it. Cost: none, nothing to build.
- **The bridge stamping what arrived during the freeze.** There is nothing for it to stamp. The time is written by the game, in the game's memory, when the game takes the message. The bridge's part is to still have the datagrams, and it has the first 256.
- **The port's clock for that test.** The wrong lever, and the only costly one. `0x1b1b0` is the monotonic clock the simulation and the game host thread pace from, the one the port's frame-steady clock answers. Holding it back across a freeze would move every timer in the game and breaks "keep our tick" outright. It would also act on the wrong PC: the PCs that end the session are the ones that never froze.

### What would have to change, and where

- **inferred** For a freeze of more than 10 s to be survivable, every other PC's test on the frozen player must not fail. Two ways, both in the port and both outside the simulation:
  - **Raise the kick limit on every PC.** The 10.0 at `eboot+0x1061c4c` is writable data; both players run the same release, so both would have it. With the limit at N s on all PCs, the read-first order of (1) means the thawing side needs no help. The same float sets the 9 s threshold for accepting session messages ([Do the game's own packet delay and drop settings work, and what do the other unread settings do?](netcode-unread-settings.md)), and a higher limit keeps a player who is really gone on the others' screens that much longer, unless the port says DEAD for a link that died (the link's own limit is 15 s, `p2p_link.h:76`).
  - **Keep the frozen player's keepalive time fresh from the port.** A link whose game has sent no datagram for some seconds, while the link itself answers, is a frozen game ([How long can a joining guest that hangs keep everyone from stepping?](netcode-hung-join-blocks-all.md), (4)). The port on the other PCs could write the current clock into `+0x160` of that player's connection object once a second for as long as it chooses, and stop when the link dies or its own limit runs out. The sync probe already reads game memory this way (`core/sync_probe.cpp`); this would be the first write.
- **known** Cost against "keep our tick": none for either. Both act only with online on, on a float or a field the simulation does not step from. The loopback path (`net_offline.cpp:203`) and the epoll timeouts are not involved.
- **inferred** A larger queue for datagrams from other players would cut the resends after a long freeze. It can be done at `net_offline.cpp:438` alone; `DeliverRemote` is never called with online off, and the loopback limit at `:203` stays as it is. It is not needed for the test.
- **not found** Whether the session is worth having after such a freeze. The frozen game comes back 300 or more steps behind and has to catch up; if it is the host, nobody stepped meanwhile. The per-player timers (240 s and 360 s) are far off, but what the catch-up, the resends at a 16 KB/s send budget and the host's wait for input do over 10 s or more was not read and is in no log.

## (5) What the other PCs do meanwhile

**They remove the frozen PC at 9 to 10 s, and that ends the session whatever the thawing side does. Known for each step from the code; the whole sequence was never run.**

1. **known** A frozen game sends no keepalive: they are sent from `0x3788a0`, on the thread that runs the network update ([How long can a joining guest that hangs keep everyone from stepping?](netcode-hung-join-blocks-all.md), (1)).
2. **inferred** Every other PC took the frozen game's last keepalive at most about 1 s before the freeze began, so its test fails 9 to 10 s into the freeze (plus the path's delay). Basis: the 1.0 s period and the test of (2).
3. **known** That PC's game then sends the goodbye, flushes and calls `sceNpSignalingTerminateConnection` (remove-player, `0x3666f0`). The port's `Link::Terminate` sends a BYE (`p2p_link.cpp:1020..1025` on `fix/coop-online`, `:990` on `main`).
4. **known** The frozen PC's link takes the BYE on its own thread, logs `left` and queues a Dead event (`p2p_link.cpp:688..699`). On `fix/coop-online` the peer goes to a closing state in which game datagrams still pass for 10 s (`p2p_link.h:101`).
5. **known** After the thaw the game hears DEAD on its next `sceNpCheckCallback`, at the top of the frame (`0x9e29`), and the signaling update at the start of `0x3604a0` removes the player (state 5 in `0x377b20`), before the keepalive loop is reached.
6. **known** If the DEAD did not come, the goodbye would do it: it is a class 0 message behind the last keepalives, `0x37d0e0` takes it a few passes later, and 20 ms after that the test fails by its first rule.

- **inferred** So after a freeze of more than 10 s the thawed PC's game loses every connection right after the thaw, and that is the picture the earlier inference expected. The cause is the other PCs' kicks, not its own. Its log should show `left` for each player during the freeze and `signaling event 0` after the thaw, and no `the game ends connection` line with `0x360555`; the other PCs' logs should show that line, 9 to 10 s into the freeze. Basis: steps 1 to 6 and the lines of (3).
- **inferred** The two sides' limits fall at the same moment to within about a second: the thawing side's last keepalive from each player is also at most about 1 s older than the freeze. If the thawing side did test before reading, it would fail from 9 to 10 s of freeze on, the same range in which the others remove it. Since it reads first, it does not fail there at all.
- **Plainly:** sparing the thawing side cannot matter. With the kick at 10 s on the other PCs, the session is gone before the thaw. With the kick held back on the other PCs, the thawing side already spares itself.
- **inferred** With three players and a frozen guest, the host and the third player each remove the frozen one and play on; the thawed guest ends up alone. With a frozen host, each guest removes the host. Basis: the test runs on every connection of the full mesh ([What does the game netcode tolerate, and which of its limits can be reached?](../../.scratch/netcode/issues/03-game-netcode-limits.md), (e)); what the guests' games do after removing their host was not followed.

## What a later pass needs to settle

Not found, listed in the sections above: which party stage and player the leave-session site `0x3638aa` tests; whether the game host thread reads the socket during a freeze of the main thread, and on a guest's PC at all; whether keepalives keep coming out of the streams in the seconds after a thaw that overflowed the bridge; what the simulation does after a freeze of 10 s or more when nobody is removed; the order of "hand over what arrived" and "nothing heard for 15 s" in the link's loop after a suspended process resumes; what a guest's game does after it removes its host.

Corrections to earlier findings:

- [How long can a joining guest that hangs keep everyone from stepping?](netcode-hung-join-blocks-all.md), (1): "`0x3788a0` … takes that connection's control messages (`0x37d0e0`) … Only after that loop are the sockets flushed and read". `0x37d0e0` reads the socket itself, through `0x379ab0` (`0x379b1b`), before it takes a message. The read after the loop is one more, not the first.
- The same file, case B and question 5: "A game that thaws after more than 10 s removes every other player itself, before it reads what arrived meanwhile." It reads first. It does lose every player right after the thaw, because the others removed it and said so.
- The same file, case B: "raising the limit in the host's memory alone would not change that." Still true, for a different reason: the limit has to be raised on the PCs that did **not** freeze, which with one release for everyone means all of them. Raised everywhere, it is enough as far as the test goes.
- The same file, (1): "`0x3788a0`, which takes that connection's control messages". It takes one per pass.
- [What does the game netcode tolerate, and which of its limits can be reached?](../../.scratch/netcode/issues/03-game-netcode-limits.md), (b): "`+0x160` is the time the last keepalive from that player was read". More exactly, the time it was taken out of the ordered stream by `0x37d0e0`, one message per pass; the datagram may have been read earlier in the same pass or by another caller of `0x379ab0`.

Questions this raises for the map:

1. **Does a game whose main thread was frozen for 12 s keep its connections when the other PC's kick is held back?** A two-instance run: raise the 10.0 at `eboot+0x1061c4c` on the PC that does not freeze (a debugger write or a test switch), suspend only the **main thread** of the other for 12 s, and look for no `the game ends connection` and no `left` on either side. Suspending the whole process tests a race instead ((2)). A plain 12 s suspend with both limits at 10 s cannot tell the two orders apart: the other PC's kick ends the session either way. If a connection does end on the thawed PC, `the game ends connection` with `eboot.bin+0x360555` on its stack is its own test; `left` and `signaling event 0` are the other side's ending. It needs the test accounts' sign-in ([Can the three test accounts sign in and find each other?](../../.scratch/netcode/issues/24-test-accounts-sign-in.md)).
2. **Is a session worth keeping after a freeze of more than 10 s?** The same run, with `LBP3_TRACE_SIM=1`: how long the thawed game takes to catch up, whether the loading screen comes up, and whether anything else ends the session. The answer decides whether the next question is worth asking.
3. **Should the port hold the others' kick back for a player whose link answers while their game is silent, and for how long?** It is the reverse of question 3 of [How long can a joining guest that hangs keep everyone from stepping?](netcode-hung-join-blocks-all.md), which asks whether to say DEAD sooner. Both are one decision for the netcode bar: how long the others stand still for a frozen player before they go on without them. Background shader builds already remove the known cause of freezes, and the longest of 15 November was 3.5 s.
4. **Should the bridge keep more than 256 datagrams from other players?** Only if question 2 says long freezes are worth surviving; the first 256 are enough for the test.
