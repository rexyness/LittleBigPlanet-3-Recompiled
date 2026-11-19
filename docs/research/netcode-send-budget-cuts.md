# What makes the game netcode cut its send budget, and does the peer link trigger it?

Research for the [online co-op netcode map](../../.scratch/netcode/map.md), ticket 15. Written 2026-11-16. It builds on [the game netcode's limits](netcode-game-netcode-limits.md) (ticket 03) and [the 15 November measurements](netcode-session-measurements.md) (ticket 01).

## Summary

- **The delay-trend test fires when datagrams arrive steadily later for a second or two.** It compares the last 64 datagrams' travel times in ticks of 1/6 s. It needs the travel time to climb by roughly half a second or more inside those 64 datagrams. Steady delay, however long, and jitter do not fire it.
- **There is a second cut the ticket did not know about.** A game also cuts its *own* budget, with no notice from anyone, when its count of timed-out resends rises through four half-second periods in a row.
- **The budget starts at 16,384 bytes a second, not at the bandwidth the port reports.** It grows only while the game is sending at 75 % of it or more. Steady play never does, so the port's 10 Mbit/s cap is out of reach except during a bulk send.
- **Recovery is slow and any new notice restarts it.** Five seconds after the last notice the budget is multiplied by 1.0, then every five seconds by 1.1, 1.5, 2.0, 2.0 and so on, and only while the game is sending hard. A guest in steady play never recovers a cut at all.
- **The peer link's own behaviour rarely fires the delay-trend test.** Doubled datagrams and the 100 ms repeat never reach the game. A burst released after a gap gives falling delays, not rising ones; in a model it fired the test in 0 to 15 % of runs, usually once. A path whose delay grows smoothly fires it many times. Whether the delay spells measured on 15 November did so depends on their shape inside each 5 s window, which the logs do not hold: the model gives anything from no notice to enough to reach the floor.
- **What the budget limits is narrower than "how fast the game sends".** First sends on the ordered streams are not limited. The budget limits resends (budget ÷ 30 − 50 bytes per flush) and the pace of resource transfer between players (budget minus a reserve). At the floor that is one resend per flush and about 6 KB a second of resources.
- **Not found: whether a cut budget dragged the 15 November loads.** The logs hold datagram counts, not bytes or contents. The counts fit several stories. The values that would settle it are named at the end.

## How this was read

- **Binary:** `eboot.bin`, CUSA00473 1.28. Addresses are module offsets, written `eboot+0x...`.
- **Assembly:** `llvm-objdump` (Intel syntax) on the cached text image `D:\Recomps\lbp3-recompiled\analysis\disasm_cache\analysis_eboot.elf.text.elf`. Constants were read from `analysis\eboot.elf` at their module offsets. Everything marked "known" below was read as instructions in this session. Most of it was read from a previous agent's dump of `eboot+0x374000..0x382000`; the dump was made again in this session and is byte for byte the same.
- **Ghidra:** not run in this session. A previous agent's decompiler output from a read-only copy of `analysis\ghidra\lbp3.gpr` (made 2026-11-15) was used as a guide. Where a point rests on it alone it is marked "inferred (decompiler, previous pass)".
- **Peer link:** `D:\Recomps\lbp3-recompiled\runtime\src\net\p2p_link.cpp` and `runtime\src\hle\net_offline.cpp`, read only.
- **Logs:** `analysis\play_log.txt` (the host, cited `H:line`). The friend's log was not re-read; its figures come from the measurements file. Host lines H:7436, H:7553, H:8423, H:8494, H:8531, H:10732, H:10764, H:13361, H:13540, H:13671, H:19841 and H:19883 were re-read; other line numbers are the measurements file's.
- **Where this file is.** The harness refused writes and git commands in the ticket's own worktree, so the file was written to the `netcode-host-address` worktree. The session that fired this research committed it there, on branch `wayfinder/netcode-host-address`.
- **Model:** a Python copy of the test as read below, fed with made-up and replayed arrival times. Throwaway, not kept. Its results are marked "inferred (model)".
- **Labels:** **known** (read in the code at the address given), **observed** (in a log), **inferred** (reasoned; basis given), **not found**.
- **Field names.** `conn+0x...` is an offset into the connection object of ticket 03. The statistics function `eboot+0x37d330` is called with `conn+0xc8` (`add r14, 0xc8` at `0x378b3b`, jump at `0x378b56`), so its `+0x50` is `conn+0x118`. Both forms are given where it helps.

## (a) What the delay-trend test measures

### The time stamp

- **known** Every datagram a game sends ends in a 5-byte trailer. Its first two bytes are a time stamp written at `eboot+0x37e6da..0x37e71a`, when the datagram is built.
- **known** The stamp is the game's clock in **ticks of 1/6 s**. The code takes nanoseconds since start (`eboot+0x1b1b0`), divides to milliseconds, multiplies by 3, shifts right by 2 and divides by 125 (`0x37e6e7..0x37e712`). Run in Python, the same integer steps give 0 at 166 ms, 1 at 167 ms, 6 at 1 s and 360 at 60 s. The stamp is the low 16 bits, so it wraps after about three hours.
- **known** The receiver computes its own clock the same way and subtracts the stamp, keeping 16 bits (`eboot+0x37ec54..0x37ec93`). That is one **sample**: the datagram's travel time plus the fixed difference between the two games' clocks, in whole ticks.
- **known** The receiver's clock is read when the game *processes* the datagram, inside the test. A datagram that waited in the link, in the port's socket queue or behind a frozen frame counts that wait as travel time.

### The window

- **known** Samples go to the end of a list at `conn+0x130` (count at `conn+0x138`). When the list passes 64 the oldest is removed (`0x37eca7..0x37ecc6`). The test runs on every datagram once 64 samples exist (`0x37ecc9`). So the window is the **last 64 datagrams**, whatever time they span: 2.1 s at 30 a second, 0.5 s at 130 a second.
- **known** Nothing clears the list when the test fires. It can fire again on the next datagram.
- **known** The test is skipped for the game's own address (`eboot+0x1b640` at `0x3797e4`).
- **known** Every datagram that reaches the game is a sample. The test runs before the messages are looked at (`0x3797f7` is ahead of the message loop at `0x379880`), so the game itself would count a duplicate datagram. Only the link's own drop of duplicates (see (c)) keeps them out.

### The test

`eboot+0x380ce0` cuts the 64 samples into 8 groups of 8 in arrival order, sorts each group (`eboot+0xba09c0`) and takes the mean of its 4th and 5th values, times 0.5 (`eboot+0xe75950`): the **median** of each group. That gives 8 medians. Then, in `eboot+0x37ec30` (`0x37ecf8..0x37ed97`):

1. **known** Count the steps where a median is strictly above the one before it. Divide by 7. Call it the share of rises.
2. **known** Add up the absolute size of the 7 steps. If the sum is zero the test does not fire.
3. **known** The test fires when **the share of rises is above 0.7** (`eboot+0xe75918`) **and** (last median − first median) ÷ (sum of step sizes) **is above 0.5** (`eboot+0xe7591c`).

- **known** With 7 steps, "above 0.7" means at least 5 rises.
- **inferred** These are the two trend tests of the Pathload bandwidth tool (pairwise comparison and pairwise difference). Basis: the same two statistics and thresholds near its published ones. It changes nothing below.

### What it takes to fire it

- **inferred (model)** A travel time that stays put does not fire it, with jitter of 0 to 30 ms or 0 to 150 ms, at 30, 42 or 130 datagrams a second, at any offset of the two clocks: 0 of 180 runs each.
- **inferred (model)** A travel time that climbs steadily does. At 30 datagrams a second:

  | Climb | Runs that fired (of 60) | Most notices in a run |
  |---|---|---|
  | 0.1 s per second for 5 s | 0 | 0 |
  | 0.2 s per second for 3 s | 44 | 3 |
  | 0.3 s per second for 2 s | 60 | 7 |
  | 0.5 s per second for 1 s | 37 | 9 |
  | 0.5 s per second for 2 s | 60 | 20 |
  | 1.0 s per second for 1 s | 60 | 15 |
  | 1.0 s per second for 2 s | 60 | 28 |

- **inferred (model)** A faster sender needs a steeper climb, because 64 datagrams span less time. At 130 a second only 1.0 s per second fired, in about half the runs.
- **inferred** So the test answers "are datagrams arriving more and more slowly than they are sent, right now". The tick of 167 ms makes it blind to anything under about half a second of added delay. Basis: 5 rises of at least half a tick each, and the model.
- **inferred (model)** One sudden jump (a step up, or a burst that arrives late) fires it only by luck. It needs the steady travel time to sit near a tick boundary, so that the medians before the jump already wobble upward four times; the jump is the fifth rise. In the model that happened in 0 to 35 % of runs depending on rate and jump size, usually with one notice.

### What happens when it fires

- **known** The receiver sends connection-control code 2 to the sender, at most once per 0.1 s (`eboot+0xe75840`; the last time is at `conn+0x170`; `0x379800..0x379840`).
- **known** The sender's handler is `eboot+0x37d0e0` at `0x37d261`. See (b).

## (b) How the budget is cut and how it recovers

### Where it starts

- **known** The statistics object is set up at `eboot+0x37ccbe..0x37ccea`: budget (`conn+0x118`) 16,384, cap (`conn+0x11c`) 16,384, growth step (`conn+0x12c`) 3, and the "last notice" time (`conn+0x120`) set to now.
- **known** A connection to the game's own address is pinned at budget and cap 1,048,576 (`0x37d8c3..0x37d8d3`).

### The two cuts

**Cut 1: a notice from the other game** (`eboot+0x37d261..0x37d2c8`):

- **known** Every notice writes now to `conn+0x120`.
- **known** If more than 0.25 s (`eboot+0xe7585c`) have passed since the last cut (`conn+0x124`): budget = budget × 0.75 (`eboot+0xe75860`), not below 10,240; growth step = 0; last cut = now.

**Cut 2: the game's own resend trend** (`eboot+0x37d63b..0x37d857`, inside the statistics function):

- **known** The statistics function keeps ten buckets of 0.5 s each (`eboot+0xe75868`). Field `+0x1c` of a bucket counts **timed-out resends**: the flush sets a message's byte `+2` to 1 when more than 2 × RTT has passed since its last send (`0x37dc04..0x37dc0b`) and counts each such message that goes out (`0x37dcf2`).
- **known** At the end of each bucket that count is appended to a list of the last four (`0x37d63b..0x37d690`).
- **known** Not for the game's own address, and only with four values: the same trend test as in (a) runs on those four (`0x37d769..0x37d806`). Share of rises = rises ÷ 3 (`eboot+0xe75878`) above 0.7 (`eboot+0xe7587c`), so **all three steps must rise**; and (last − first) ÷ sum of steps above 0.5, which is then always true.
- **known** When it fires: now is written to `conn+0x120` (the same field a notice writes), and if more than 0.25 s (`eboot+0xe75880`) have passed since the last cut, budget = budget × 0.75 (`eboot+0xe75884`), not below 10,240; growth step = 0 (`0x37d808..0x37d857`).
- **inferred** So a game cuts its own budget when it resends more in each of four half-second periods running: 2 s of a growing pile of unacknowledged messages. Since the test runs once per bucket, this path cuts at most twice a second.

**How fast a cut reaches the floor:**

- **known** From 16,384: two cuts (12,288, then 9,216 raised to 10,240).
- **known** From the port's cap of 1,250,000: 17 cuts, 4.25 s of notices at the 0.25 s limit.

### Recovery

All in `eboot+0x37d8eb..0x37da08`, which runs on every call of the statistics function:

1. **known** Nothing happens until more than 5.0 s (`eboot+0xe75888`) have passed since `conn+0x120`. That field is then set to now.
2. **known** `conn+0x120` is written by every notice received and every firing of cut 2, whether or not the 0.25 s rule let them cut. **So each notice restarts the 5 s wait.**
3. **known** Then the cap is refreshed: `sceNpSignalingGetConnectionInfo` bandwidth ÷ 8, or 16,384 if the call fails (`0x37d966..0x37d985`; ticket 03).
4. **known** Then, only if the game was "busy" within the last 5.0 s (`conn+0x128`, below): budget = budget × table[growth step], and the step goes up by one, stopping at 3. The table at `eboot+0xe75710` is **1.0, 1.1, 1.5, 2.0**. The result is held to at most 8,388,608, at most the cap, and at least 10,240.

- **known** "Busy" is set at the end of each half-second bucket when the bytes the game sent to all other players in that bucket, doubled, are at least 0.75 × this connection's budget (`0x37d85d..0x37d897`, sum in `eboot+0x37bbf0` over `conn+0x110`). A datagram counts as its length plus 50 (`0x37eb64`, `0x37eb8c`).
- **known** A new connection has step 3, so its budget doubles every 5 s while busy: 16,384, 32,768, 65,536 and so on up to the cap.
- **known** After a cut the step is 0. The first 5 s step multiplies by 1.0 and changes nothing. From the floor: 10,240 for 10 s after the last notice, then 11,264, then 16,896 at 15 s, 33,792 at 20 s, doubling from there. All of it only while busy.

**Does steady play count as busy?**

- **observed** A guest sends 30 datagrams a second of about 100 bytes (ticket 03, D3); the session owner about 42 a second, mostly 159 to 190 bytes (measurements, "Session summary" and "Joins and level changes").
- **inferred** With the 50 added per datagram that is about 4,500 bytes a second for a guest and about 9,500 for the owner. Busy needs 12,288 at a budget of 16,384 and 7,680 at 10,240. So:
  - neither game is busy in steady play at 16,384: **the budget stays at 16,384 until a bulk send**, and the port's 10 Mbit/s report plays no part until then;
  - an owner cut to the floor is busy and climbs back to 11,264 and then 16,896, where it stops being busy;
  - **a guest cut to the floor stays there** until it has something big to send. Busy at the floor needs 7,680 bytes a second, which at 30 datagrams a second is datagrams of 206 bytes on average; the guest's were about 100.
- **not found** The budget's real values in a session. Nothing logs `conn+0x118`.

### The link level

- **known** The same function sets a level 0 to 2 at `conn+0xc8` from budget and RTT (`0x37da0c..0x37da69`): with a budget under 15,360 it is 1 below 200 ms RTT and 0 otherwise; from 15,360 to 1,048,575 it is 2, 1, 0 at RTT under 200 ms, under 400 ms, above; from 1,048,576 it is 2 below 400 ms and 1 otherwise.
- **not found** Who reads it (still open from ticket 03).

## (c) Can the peer link fire the test?

### What the link does to a game datagram

- **known** Sending is immediate. `Link::SendData` builds the packet and sends it in the same call; there is no queue and no pacing (`p2p_link.cpp:1042-1068`).
- **known** With DATA3 each packet carries the new datagram and up to two earlier ones while it stays under 1,200 bytes (`p2p_link.cpp:312-322`).
- **known** The receiver drops a datagram it already holds or has already delivered (`p2p_link.cpp:601`). **Doubled datagrams and the repeat after 100 ms idle (`p2p_link.cpp:791-794`) never reach the game twice.** They cannot add samples.
- **known** Datagrams go to the game strictly in order. Behind a gap they wait until the missing one arrives, or 2 s, or 512 are held (`p2p_link.cpp:331-351`).
- **known** Delivered datagrams go into the socket's queue of at most 256; beyond that they are dropped (`net_offline.cpp:107`, `:203`, `:438`). The game empties the queue each frame (ticket 03).

### Against the delay-trend test

| Link behaviour | What the game's samples look like | Result |
|---|---|---|
| Doubled datagrams | No extra samples | **known**: cannot fire it |
| Repeat of the last datagram after 100 ms idle | No sample unless the original was lost; then one sample 100 ms late, under one tick | **inferred**: cannot fire it |
| One lost packet, filled from the next packet's copy | One sample about 33 ms late | **inferred**: cannot fire it |
| A gap, then a burst released together | The held datagrams all get the release time: the first is latest, each next one 33 ms less. A jump up, then a fall | **inferred (model)**: holds of 0.1 and 0.25 s never fired. Holds of 0.5 to 2 s fired in 0 to 6 of 60 runs across the three rates, one notice each. By luck only (see (a)) |
| A 0.5 s hold every second, ten times | A sawtooth | **inferred (model)**: 0 of 60 runs at 30 a second, 2 at 42, 11 at 130 with up to 3 notices |
| Holds one after another, each longer than the last (0.2 s up to 2 s) | A rising sawtooth | **inferred (model)**: 25 of 60 runs at 30 a second with up to 3 notices, 4 at 42, 0 at 130 |
| The receiving game frozen (a shader build), then reading its queue | Same shape as a burst | **inferred (model)**: 0.3 s never fired; 1 s and 3.5 s fired in 3, 7 and 9 of 60 runs at 30, 42 and 130 a second, one notice |
| The relay path's steady delay (about 220 ms each way) and its jitter | Flat | **inferred (model)**: 0 of 360 runs |
| A switch from direct to relay (a step up of 0.2 to 1 s) | One jump | **inferred (model)**: 0 to 21 of 60 runs across the rates, up to 8 notices for a 1 s step at 30 a second |
| The path's delay growing for seconds | A ramp | **inferred (model)**: fires for as long as it grows; see below |

- **inferred** The answer for the link's own mechanisms is "seldom, and by luck": a hold or a freeze of half a second or more has a chance of 0 to 15 % of one notice, and one notice is one 25 % cut. From 16,384 that is 12,288. Two inside the recovery wait reach the floor. Loads are where it is likeliest: more datagrams a second, more freezes, more holds.
- **inferred** A freeze longer than 256 datagrams' worth overflows the socket queue and the game loses datagrams the link had delivered. That is 8.5 s at 30 a second but about 2 s at the 130 a second seen in loads. Basis: `net_offline.cpp:107`. This is loss, not a budget matter, but it follows from the same burst.

### The measured delay spells

The measurements file found friend → host datagrams arriving late, in order and without loss. The logs give only how many arrived in each 5 s window, not when inside it. The model was run on three guesses: arrivals spread evenly in each window (a smooth ramp), all at the window's start, and all at its end (a stall, then a burst). Twelve clock offsets each.

| Spell | Host's lines | Received per 5 s (friend sent 150) | Spread evenly | All at the start | All at the end |
|---|---|---|---|---|---|
| Connection 1, 20:23:48 to 20:24:08 | H:7436 to H:7553 | 134, 93, 215, 174 | 40 to 42 notices | 0 or 1 (1 run of 12) | 0 |
| Connection 2, 20:25:58 to 20:26:18 | H:8423 to H:8531 | 27, 12, 140, 426 | 13 to 26 notices | 0 or 1 (1 of 12) | 0 or 1 (7 of 12) |
| Connection 6, 20:49:30 to 20:49:40 | H:19841, H:19883 | 98, 203 | 27 to 34 notices | 0 | 0 |
| Connection 3, 20:30:53 to 20:31:03 | H:10732, H:10764 | 138, 163 | 0 | 0 | 0 |

- **observed** The counts and times (`[p2p] ... last 5 s` lines).
- **inferred (model)** A spell that was a smooth slowing-down made the host's game send enough notices to take the friend's budget from 16,384 to 10,240 in its first seconds. A spell that was a stall and a burst sent one notice or none.
- **not found** Which shape they had. The RTT samples in connections 1 and 2 stayed high for two and three windows running (1,217 and 3,371 ms; 5,062 and 9,800 ms), which speaks for delay that lasted rather than one stall, but a sample every 5 s cannot tell a ramp from a row of stalls.
- **inferred** If any of them did cut the friend's budget: the friend was the guest each time, and by (b) a guest does not recover in steady play. Its budget would have stayed cut for the rest of that connection. A new connection starts again at 16,384.
- **not found** The same for host → friend. The friend's log ends at 20:26:46 and no spell in that direction was measured.

### Against the second cut (the game's own resend trend)

- **known** A message is resent when unacknowledged for 2 × the game's RTT. Through the relay that RTT is about 0.45 s, so about 0.9 s; with no sample it is 1.6 s (ticket 03).
- **inferred** The count of resends per half second rises four times running when acknowledgements stay away for roughly 2.5 s or more while the game keeps sending new ordered messages. Each of these gives that:
  - the other game frozen for 2.5 s or more (the friend's 3.5 s freeze at 20:16:38);
  - a delay spell in the direction the acknowledgements travel (all four above);
  - the link holding the other side's datagrams behind a gap toward its 2 s limit, plus the round trip. This one is marginal.
- **observed** The host's sent counts climbed in just those places: 317, 337, 432 per 5 s during connection 1's spell, and 278, 389, 246, 266, 227, 305, 300 while the friend's game was frozen, against a median of 207 (measurements, "Delivery delays" and "Freezes").
- **inferred** So the host probably cut its own budget at those moments, with no notice from the friend. The session owner is busy at the floor, so it climbs back to 16,896 within about 15 s of the last firing.
- **not found** Any direct sign of either cut. Datagram counts cannot show a budget.

## (d) What a cut budget does to a join or a level change

### What the budget limits

Readers of `conn+0x118`, found by listing every use of that offset in `eboot+0x374000..0x382000`. A reader outside that range is not ruled out.

1. **known** **Resends in a flush** (`eboot+0x37dab0`, `0x37dd21..0x37dde6`). The flush list is sorted by `eboot+0xba1620` (call at `0x37dd2c`). **inferred (decompiler, previous pass):** the order is never-sent messages first, then timed-out resends oldest first, then extra copies. The trim itself was read in assembly. With limit = budget ÷ 30 − 50 bytes:
   - a never-sent message always goes;
   - a timed-out resend goes while the bytes ahead of it in this flush are under the limit;
   - an extra copy goes only if it still fits under the limit;
   - the list is cut at the first message that fails.

   The limit is 496 bytes at 16,384 and **291 bytes at 10,240**.
2. **known** **First sends are not limited.** The slicer `eboot+0x37e150` turns the whole waiting byte stream into messages of up to 300 bytes in one go (loop from `0x37e310`), and they are all "never sent".
3. **inferred (decompiler, previous pass)** **Extra copies** (`eboot+0x37f440`): the spare budget decides whether a message goes out one to three extra times. Its read of `conn+0x118` is known (`0x37f54f`).
4. **known** **Resource transfer between players** (below).
5. **known** Two telemetry reports (`eboot+0x254450`, event 0x24; `0x37d503`, event 0x23) and a helper `eboot+0x37c040` whose caller was not found.

### Resource transfer

- **known** `eboot+0x39b730` sends one piece of at most 1,300 bytes (`cmp edx, 0x514` at `0x39b7cd`) as a class 1 message (`mov esi, 1` at `0x39b78c` before `eboot+0x37a710`). Class 1 is the session code's class from ticket 03.
- **inferred (decompiler, previous pass)** The piece is a slice of a resource, read from a file or a cache and named by its hash and offset.
- **known** `eboot+0x39ab70` adds (allowance + 29) ÷ 30 to a per-player credit on each call (`0x39aca9..0x39acbe`). `eboot+0x399700` calls it (`0x399721`), compares the credit with 1,300 (`0x39984a`), calls `0x39b730` (`0x39985a`) and writes the credit back (`0x399874`).
- **inferred (decompiler, previous pass)** The write-back is the credit minus the bytes sent.
- **known** The allowance is `eboot+0x37b5e0` (assembly): the budget minus a reserve from the table at `eboot+0xe75720` (**0, 4,096, 8,192, 12,288**, indexed by a count minus one, at most 3), or 4,096 if the budget is not above reserve + 4,096. It is halved while a timer at `+0xd8` of the connection manager is under 1.0 s old (`eboot+0xe7584c`).
- **inferred** The count is the number of players in the room, so two players reserve 4,096. Basis: it sums a per-entry count over a four-entry table. The table's owner and the `+0xd8` timer's writer were not read.
- **inferred** So resources move at about the allowance in bytes a second, if `0x399700` runs once a frame (it uses the constant 1/30 at `eboot+0xe7a394`).

| Budget | Allowance, two players | 100 KB takes | 1 MB takes |
|---|---|---|---|
| 10,240 (the floor) | 6,144 B/s | 16 s | 163 s |
| 16,384 (the start) | 12,288 B/s | 8 s | 81 s |
| 32,768 | 28,672 B/s | 3.5 s | 35 s |
| 1,250,000 (the port's cap) | 1,245,904 B/s | under 1 s | under 1 s |

- **inferred** From a fresh connection with no cut, the budget doubles every 5 s during a transfer (it is busy), so 1 MB takes about 19 s. With the budget held at the floor by repeated notices it takes 163 s: **about eight times longer**. Halve the rate again while the `+0xd8` timer is fresh.
- **not found** Whether any class 1 data flowed in the 15 November loads, and how many bytes a join or a level change moves in each class. Two of the levels entered were story levels (`play/developer/...`), which both players have on disc.

### Ordered game messages

- **inferred** A join's or level change's game-state messages go out at once whatever the budget, because first sends are not limited. A cut budget does not slow them directly.
- **inferred** It slows **repair**. At the floor one resend fits per flush (a resend goes only while under 291 bytes are ahead of it, and a full slice is about 300), so a backlog of lost or late-acknowledged slices is resent at 30 a second at most, about 9 KB a second. At 16,384 it is two per flush.
- **inferred** In a flush that already holds 291 bytes or more of never-sent messages, **no resend goes at all** at the floor. While a game keeps producing that much new data every frame, a lost message is not repaired, the receiver's ordered stream stays stuck behind it, and its keepalive is not delivered. Basis: the trim rule in point 1. Whether any real traffic keeps a flush that full for seconds is not known; the session owner's steady datagrams are 159 to 190 bytes, under the limit.

### Size of a load

- **observed** The session owner sent 292 to 342 datagrams in a join, 1,634 in the role swap, and 724 to 3,003 in a level or scene change (measurements, "Joins and level changes").
- **observed** The largest datagram size logged is 313 bytes (one size per 3,000 datagrams).
- **inferred** So a level change moved at most about 0.94 MB (3,003 × 313) and a join at most about 0.1 MB, resends and the steady 42 a second included.
- **not found** The real byte counts.

## (e) Do the 15 November logs fit a cut budget?

**Not found.** The logs can neither show nor rule out a cut. What they hold:

- **observed** The host's sent counts per 5 s in each load (measurements, "Joins and level changes"):

  | Load | Host sent per 5 s | Host received per 5 s |
  |---|---|---|
  | Level entered, 20:36:44 (H:13361..13671) | 274, 359, 219, 667, 471, 509, 504 | 61, 117, 44, 48, 14, 69, 130 |
  | Level entered, 20:45:40 (H:17930..18110) | 120, 327, 263, 298, 447 | 91, 134, 158, 76, 232 |
  | Scene change, 20:55:01 (H:22791..22898) | 210, 167, 347 | 106, 53, 140 |
  | Level entered, 20:56:26 (H:23584..23682) | 592, 335, 624 | 150, 43, 48 |
  | Role swap, 20:15:03 (H:3281..3541) | 69, 864, 260, 441 | 68, 70, 56, 140 |

- **inferred** A sender held back by the budget would show a flat rate. One run looks flat: 471, 509, 504, about 100 datagrams a second, 58 above the steady 42. Several readings give a number near that and the counts cannot choose between them:
  - resends at the flush limit of the *uncut* starting budget of 16,384 (two a flush, if each resend is its own datagram, which was not established);
  - resource pieces at an allowance between the one for 16,384 (12,288 B/s, about 40 slices a second) and the one for 32,768 (28,672 B/s, about 95);
  - first sends that happened to come at that rate.
- **inferred** The other windows jump about (219 then 667; 592, 335, 624) in a way no budget rule gives. First sends, which the budget does not limit, would look like that.
- **observed** In the 20:36:44 load the host received 14 datagrams in one window and 44 to 69 in three others, against 150 normal: the guest's game was barely sending for about 20 s. In the same load the host's link sent 12 datagrams again at the friend's link's request (H:13540).
- **inferred** With the guest nearly silent its acknowledgements were late, which is the condition for cut 2 on the host. So a cut of the host's own budget in that load is possible. Its effect would be on resends and on resource transfer only.
- **inferred** The loads that dragged did so with the guest's game quiet for 15 to 20 s. Nothing in the counts needs a cut budget to explain their length, and nothing rules one out.
- **not found** Whether the friend's budget was cut before connection 6's loads. No delay spell was seen in that connection before 20:36:44 (the first sign is at 20:37:19, H:13671).

### What would settle it

One line every 5 s per connection, on both games, read from the connection object (the list is at `[eboot+0x130d638]+0x50`, count `+0x58`):

- `+0x118` budget and `+0x11c` cap;
- `+0x12c` growth step (0 right after a cut);
- `+0x120` and `+0x124`, the times of the last notice and the last cut (seconds as a float, from `eboot+0x1b3f0`);
- `+0x110` bytes sent in the last half second, `+0xf0` RTT in microseconds, `+0x188` unacknowledged messages;
- and from the link, bytes per window, which the measurements file already asks for.

A cut then shows as `+0x118` falling with `+0x12c` at 0, and its cause as `+0x120` moving with (a notice) or without (cut 2) a control message of code 2 in the datagram bytes.

## Limits and knobs reachable from the port's side

- **Bandwidth reported to the game.** Only the cap. It cannot lift a cut budget and does nothing until the game is busy.
- **`conn+0x118` itself.** The port can read and write the game's memory. Writing the budget (and step 3 to `+0x12c`) each few seconds would undo both cuts. Not tried.
- **Constants in read-only data, patchable before start:** the 0.75 factors (`eboot+0xe75860`, `0xe75884`), the growth table (`0xe75710`), the two trend thresholds (`0xe75918`, `0xe7591c`; `0xe7587c` for cut 2), the 0.1 s notice limit (`0xe75840`). Check each for other readers first: `0xe75868` (0.5) is shared by the bucket length and a threshold, and `0xe75888` (5.0) by two tests.
- **Control code 2 in transit.** The link could drop the notice. It travels inside an ordered message with other data, so that means parsing the stream; not a simple filter.
- **The floor, 10,240,** is an immediate in the code at three places (`0x37d29b`, `0x37d839`, `0x37d9f6`), not a data constant.

## New questions for the map

1. **Does a cut budget explain the unexplained kicks of ticket 03?** At a budget of 10,240 a flush holding 291 bytes or more of new messages sends no resend. Is there traffic (the session owner's backlog of 309 to 313-byte datagrams seen in D3) that keeps every flush that full for 10 s, so that a single lost message is never repaired? Reading `conn+0x118` and `conn+0x188` once a second in a D3-style run would answer it.
2. **What are the budget, growth step and last-cut times on both games during a join and a level change through the relay?** (The probe above.) This turns (e) from "not found" into an observation.
3. **Should the port hold the budget up itself?** If question 2 shows cuts, is writing `conn+0x118` from the port safe, given that the path through the relay has real limits the game's test was built to respect?
4. **Does resource transfer between players (class 1) run at all in the port's sessions, and for what?** It is the one flow whose speed is the budget directly.
5. **Can the socket queue's limit of 256 lose datagrams the link delivered, during a freeze in a load?** At 130 datagrams a second a 2 s freeze fills it (`net_offline.cpp:107`).
