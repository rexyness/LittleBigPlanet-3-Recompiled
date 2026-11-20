# Which clock readings the game's connection code compares, and on which threads

Written 2026-11-18. Follows [the send-budget cuts](netcode-send-budget-cuts.md) and the game netcode's limits (branch `research/netcode-game-netcode-limits`).

## Short answers

1. **Where the two readings of an RTT sample come from.**
   - First send (`message+0x88`) and last send (`message+0x80`) are stamped in the datagram builder `eboot+0x37de30`, at `0x37e07a` and `0x37e086`. Each is a fresh call of the clock `eboot+0x1b1b0`. Nothing is passed down. (known)
   - The acknowledgement side reads a fresh clock at `0x37f0f1` in `eboot+0x37f040`. (known)
   - **Both sides run on both frame-clock threads.** 'main' flushes stream 0 every frame and reads the socket in the network update. 'game host thread' flushes stream 1 in its own loop and reads the socket when it takes messages of classes 3 and 7. All of it is on the one connection manager, under its lock. (known)
   - A third stamping path is the room-update handler `eboot+0x375550`, which flushes stream 0 on whatever thread delivers the room event. The game itself treats that as possibly another thread. (known that it stamps; which thread is not settled)
   - So −100 ns is a message stamped on one of the two frame-clock threads and acknowledged on the other, inside the same 8 ms after a flip. −6.67 ms needs a stamp from a real-time thread; the room-update path is the candidate. (inferred)
2. **Other differences of two clock readings.** Every one in the connection code goes through the unsigned conversion `eboot+0x1b2a0` (or its millisecond twin `0x1b300`). Any negative difference, −100 ns or −8 ms alike, becomes 1.8 × 10^10 s.
   - Keepalive kick: the keepalive stamp and the test both sit in the network update `eboot+0x3604a0`, so normally the same thread and no kick. An instant kick is possible only right after a connection is set up or reset, if that stamp was taken on a real-time thread; not settled. The host thread's flush runs the same test and can skip one flush.
   - **Resend timer: an instant "timed-out" resend**, counted towards the budget cut, and the message no longer gives an RTT sample.
   - Flush pacing timers: an early flush, which is what lets 'main' reach the resend timer on a stream the host thread just stamped.
   - RTT bucket rotation: one bucket closed early.
   - Join and slot timeouts: one strike of the five needed. No kick.
   - The 9 s session test and the budget timers use float seconds and signed compares. Harmless.
3. **Floor on the resend wait: none.** The wait is `RTT × 2.0 + [eboot+0x130d95c]`, and the added term is zero. With no sample the RTT counts as 0.8 s, so 1.6 s. A "timed-out resend" is a message that went out in a flush with its byte `+2` set, which the flush sets when `now − last send > wait`.
4. **No single place.** The connection code calls the shared clock `eboot+0x1b1b0` directly at 23 sites (list below), and the float clock `0x1b3f0` at more. Both are used all over the game (190 and 277 call sites). The port can pick the connection layer out by the caller's return address: a range test on `eboot+0x374000..0x382000` plus three writers outside it. Four sites alone cover the RTT sample and the resend timer.

## How this was read

- **Binary:** `eboot.bin`, CUSA00473 1.28, as `llvm-objdump` (Intel syntax) on `D:\Recomps\lbp3-recompiled\analysis\disasm_cache\analysis_eboot.elf.text.elf`. Constants from `analysis\ghidra\eboot_etexec.elf`.
- **Callers** were found by scanning the whole text for `call`/`jmp rel32` to a target. Indirect calls (tables, callbacks) are not found that way; where that matters it is said.
- **Function starts** are a guess: the nearest earlier 16-byte-aligned `push rbp; mov rbp, rsp`. A function without that prologue is credited to the one before it. This went wrong once and was corrected by hand (`0x3a2070`, a thunk). "Called from function X" below carries that risk; the call site addresses do not.
- Of the three callers of `0x3a2070`, only `0x3a0900` was traced to the host loop. `0x3a4b60` and `0x3a93d0` were not.
- **Ghidra was not run.** No game was launched. Thread names come from the task's background and the earlier research, not from a run.
- **Labels:** **known** (read as instructions in this session, address given), **inferred** (reasoned; basis given), **not settled**.

## The clock and the conversion

- **known** `eboot+0x1b1b0` returns nanoseconds since the first call as an unsigned 64-bit value (`sceKernelClockGettime(4)` at `0x1b1da`, return at `0x1b1df`; first-call branch returns at `0x1b223`). `eboot+0x1b3f0` returns the same as float seconds (return at `0x1b423`). These are the three wrapper returns of the background note, in two functions.
- **known** `eboot+0x1b2a0` converts an unsigned 64-bit count to float and multiplies by 1e-9:

  ```
  1b2a0: test rdi, rdi
  1b2a3: js 0x1b2ac
  1b2a5: vcvtsi2ss xmm0, xmm0, rdi
  ...
  1b2ac: mov rax, rdi ; shr rax ; and rdi, 1 ; or rdi, rax
  1b2b9: vcvtsi2ss xmm0, xmm0, rdi
  1b2be: vaddss xmm0, xmm0, xmm0          ; the unsigned idiom
  1b2cb: vmulss xmm0, xmm0, [0x1190c7c]   ; 1e-9
  ```

  `eboot+0x1b300` is the same with × 1000 first (milliseconds).
- **inferred (computed with the same float steps)** −100 ns, −6.67 ms and −8 ms all give 18,446,743,552 s. The size of the negative difference makes no difference to the result.

## 1. The RTT sample

### The stamps

```
37e06c: mov rbx, [r13]              ; the message
37e070: cmp qword [rbx+0x80], 0     ; never sent?
37e078: jne 0x37e086
37e07a: call 0x1b1b0
37e07f: mov [rbx+0x88], rax         ; first send
37e086: call 0x1b1b0
37e08b: mov [rbx+0x80], rax         ; last send
```

- **known** This is in `eboot+0x37de30`, the function that packs a flush's messages into datagrams. Two separate fresh reads; no `now` argument.
- **known** `0x37de30` has two callers: the stream flush `eboot+0x378fe0` (at `0x3792b6`) and the unacknowledged-stream flush `eboot+0x379310` (at `0x37956b`).

### The acknowledgement

```
37f0e3: cmp byte [r14+0x2], 0       ; r14 = the acknowledgement entry (rsi), not the message
37f0e8: jne 0x37f13b                ; set -> no sample
37f0ea: cmp byte [r14+0x3], 0
37f0ef: jne 0x37f13b                ; set -> no sample
37f0f1: call 0x1b1b0
37f0f6: sub rax, [rbx+0x88]
37f100: call 0x1b2a0
37f119: vaddss xmm0, xmm0, [rcx+rax]   ; bucket sum, bucket = conn+0xd0[conn+0xe0]
37f137: inc dword [rcx+rax+0x4]        ; bucket sample count
```

- **known** `rbx` is the unacknowledged message whose number (`word [rbx]`) and stream (`[rbx+0x1c]`) match the entry (`0x37f0ad..0x37f0b7`). `r14` is the function's second argument, the entry being processed; its first word is the acknowledged number (`0x37f096`).
- **inferred** Bytes `+2` and `+3` of that entry are the same two marks a sent message carries at its own `+2`/`+3` ("timed-out resend" and "extra copy"), echoed back by the receiver, so a sample is taken only when the acknowledged copy was the first send. Basis: same offsets and the message header's "sent again" mark in the earlier research. The decoder that fills the entry was not read.
- **known** `0x37f040` is called only from `0x37ede0` (`0x37ee29`), that only from the datagram reader `0x3795b0` (`0x3799d0`), that only from the socket reader `eboot+0x378b60` (`0x378d4a`, `0x378f18`).

### Who calls the flush (the stamping side)

| Caller of `0x378fe0` | Stream | Reached from | Thread |
|---|---|---|---|
| flush-all `eboot+0x375f00` at `0x375f97` | its `esi` | main loop `0x9d70` at `0xc725`, `esi = 0` | 'main' (known caller; thread from the earlier research) |
| same | 0 and 1 | network update `0x3604a0` at `0x36059a` (stream 0) and `0x3605ce` (stream 1), each only when more than 0.06 s (`eboot+0xe757a8`) have passed since the manager's last flush of that stream (`mgr+0xc0`, `mgr+0xc8`) | 'main' |
| same | 1 | `eboot+0x3a2070` (`mov esi, 1`, `jmp 0x375f00` at `0x3a2096`), called from `0x3a0900` (`0x3a0f26`), which the host loop `0x39c290` calls at `0x39c7df`; also from `0x3a4b60` and `0x3a93d0` | 'game host thread' |
| room-update handler `eboot+0x375550` at `0x3759be`, `0x3759ff` | 0 | `0xba2890` (`0xba2a60`), which has no direct caller: it is a callback | not settled |
| remove-player's goodbye `eboot+0x3666f0` at `0x366808`, `0x366834` | both | `0x34e9f0` | the remover's thread |

- **known** `0x39c290` is the entry of the thread named "game host thread": `0x39c244` loads it as the entry and `0x39c24b` the name string (`eboot+0xe7a3f0`) for the thread-create call `0xdfa0`.
- **known** `0x3a2070` uses the same manager as 'main': `[eboot+0x130d5b8+0x80]`, the pointer `0xc6fe` and `0x399525` load too. Flush-all takes the manager's lock first (`0x375f20`, a timed lock of 2,000,000 µs, retried).
- **known** The stream number selects the messages: the resend loop and the stamp loop work on messages whose `+0x1c` equals it (`0x37dbe0`).

### Who calls the socket reader (the acknowledging side)

| Caller of `0x378b60` | Reached from | Thread |
|---|---|---|
| network update `0x3604a0` at `0x3605dc` | main loop `0x9d70` (`0xae86`); wait loop `0x34a760` (`0x34aae7`); `0x3994e0` (`0x39954b`); `0x35f2d0` (`0x35f33d`) | 'main' for the first three; the last is not settled |
| message reader `eboot+0x379ab0` at `0x379b1b`, for every class except 5 and 6 (`lea eax,[r14-5]; cmp eax,2; jb`) | the game's readers `0x3add00`, `0x3ae1d0`, `0x399970`, `0x3f9510`, `0x37d0e0` | 'main' |
| same | `eboot+0x39f0e0` (`0x39f14d`), called from `0x3a0590` with classes 3, 5 and 7 (`0x3a0629`, `0x3a0659`, `0x3a0689`) and from `0x3a0770` | 'game host thread' (known: `0x3a0590` and `0x3a0770` are called only from `0x3a0900`, at `0x3a0970` and `0x3a0978`, and that only from the host loop `0x39c290` at `0x39c7df`) |
| `eboot+0x37a430` at `0x37a465` | `0x3adf50`, called from the main loop (`0xa68e`, `0xae06`) | 'main' |

- **known** `0x3994e0` reaches the network update only when its second argument is set. Its caller `0x236060` returns at once unless `eboot+0xe1f0` (the main-thread test) passes (`0x236068..0x23606f`).

### What this means for the two logged cases

- **inferred** A guest's sample of −100 ns: the message was stamped on one frame-clock thread and its acknowledgement read on the other, both within 8 ms of the same flip. Basis: the port's guard against going back is per thread, so one thread can't produce it; and both threads do both jobs. The likeliest pairing is a stream 0 message stamped by 'main' at `0xc725` (the stamp is then the flip time) and the acknowledgement read by 'game host thread' through `0x39f0e0`. This needs the acknowledgement back within 8 ms, so a peer on the same PC or LAN, or the connection to the game's own address.
- **not settled** Whether a guest's game runs the host thread. The thread is started from `0x39c230`, called by `0x3a1af0`, called by the wait/update function `0x34a760` (`0x34a8c4`, `0x34a8fc`). The conditions there were not read. The guest's log (two frame-clock threads wavering) says it does.
- **inferred** A host's sample of −6.67 ms: the first send was stamped with real time, so not on 'main' or 'game host thread', and acknowledged 1.33 ms later on one of those. Of the stamping paths above only the room-update handler `0x375550` can be on a third thread. The earlier research found that this handler asks `eboot+0xe220` which thread it is on, so the game expects more than one. Which port thread delivers room events was not read.
- **inferred** One bad sample poisons the RTT for five seconds. The RTT is recomputed each half second as (sum of the ten buckets' sums) ÷ (sum of their counts) × 1000 × 1000, truncated to a 64-bit integer (`vcvttss2si rax` at `0x37d4d2`) and stored as its low 32 bits (`0x37d4d7`). Run with the same float steps, one sample of 1.8 × 10^10 s among 1 to 60 samples of 50 ms stores between 1,879 and 3,389 seconds. The resend wait is then over an hour, so nothing is resent on timeout until the bucket leaves the window. The exact figure depends on float rounding; a stored 0 is possible for other sample counts and would mean the opposite, a resend on every flush.

## 2. Every difference of two clock readings in the netcode

All of these read a fresh clock. "Unsigned" means the difference goes through `0x1b2a0`/`0x1b300`, so any negative value is 1.8 × 10^10 s (or ms).

### Connection code, `eboot+0x374000..0x382000`

| Site | What | Stamp written at | Compare | A negative difference does |
|---|---|---|---|---|
| `0x37f0f1`/`0x37f100` | RTT sample, now − `msg+0x88` | `0x37e07a` | unsigned, no test | RTT wrong for 5 s (above) |
| `0x37dbf0`/`0x37dbff` | resend timer, now − `msg+0x80` vs RTT × 2 | `0x37e086` | unsigned; `vucomiss` then `jbe` skips, so above = timed out | **instant timed-out resend**: byte `+2` set to 1 (`0x37dc0b`), the message goes out again and bucket `+0x1c` is counted (`0x37dcf2`) |
| `0x379082`/`0x379091` | flush pacing, now − `conn+0x250` (stream 0) or `+0x258` (stream 1) vs 0.06 s (`eboot+0xe75818`) and vs `0x37c040`'s figure | `0x379133` | unsigned; small = skip | flush goes ahead early |
| `0x36055e`/`0x360580`, `0x3605b1` | manager's flush pacing, now − `mgr+0xc0` / `mgr+0xc8` vs 0.06 s | `0x375fa2` (flush-all, either thread), `0x375a0c` (room update) | unsigned; `jbe` skips | 'main' flushes that stream at once |
| `0x37936f`/`0x37937e` | unacknowledged-stream pacing, now − `conn+0x260` vs `0x37c040` + 0.1 s | `0x37929c`, `0x3793cc` | unsigned | one early flush |
| `0x3789d2`/`0x3789e4` | keepalive send, now − `conn+0x158` vs 1.0 s (`eboot+0xe75810`) | `0x378a1c` (same function) | unsigned; `jbe` skips | one extra keepalive. Same function and thread, so not expected |
| `0x37b476`/`0x37b488` | kick test part 1, now − `conn+0xc0` vs 10 s (`eboot+0x1061c4c`) | `0x36697a`, `0x3664d3`, `0x37adb6` (setup, reset) | unsigned; `jbe` = pass | part 1 true early (the connection counts as 10 s old) |
| `0x37b4ab`/`0x37b4b1` | **kick test part 2**, same now − `conn+0x160` vs 10 s | `0x37d2ce` (a keepalive is read) | unsigned; `ja` = fail | **test fails at once** |
| `0x37b41c`/`0x37b42b` | goodbye grace, now − `conn+0x168` vs 20 ms (`eboot+0xe75844`), through `0x1b300` | `0x37d253` | unsigned; `ja` = fail | test fails at once (the player has said goodbye anyway) |
| `0x379800`/`0x379813` | congestion notice at most every 0.1 s, now − `conn+0x170` | `0x379840` | unsigned | one extra notice allowed. Same function |
| `0x37d363`/`0x37d36f` | bucket rotation, now − `stat+0x20` (`conn+0xe8`) vs 0.5 s (`eboot+0xe75868`) | `0x37d89d` (same function) | unsigned; `jbe` skips | one bucket closed early: RTT and the resend-trend list advance a step early |
| `0x37e6da`, `0x37ec54` | the datagram trailer's 1/6 s tick and the receiver's copy | none | integer division of one reading | an 8 ms shift is far below a tick. Nothing |
| `0x37994b` | receive time stored with a received message | – | only stored here | not followed |

- **known** The kick test `eboot+0x37b3f0` is called from the per-connection update `0x3788a0` (`0x378a32`; a failure makes the network update call remove-player at `0x360550`) and from the flush `0x378fe0` (`0x37905d`; a failure only skips that connection's flush, `jne 0x3792eb`).
- **known** `conn+0x160` is stamped in `0x37d0e0`, whose only caller is `0x3788a0` (`0x3789cd`), whose only caller is the network update `0x3604a0` (`0x360535`). In `0x3788a0` the stamp comes first and the test after it.
- **inferred** So for a keepalive stamp, the stamp and the kicking test are on the thread that runs the network update. With that always 'main', the per-thread guard makes a negative difference impossible, and the kick can't fire from a keepalive stamp. Three of the update's four callers are 'main'. The fourth, `0x35f2d0`, is called from `0x35eb10`, which has no direct caller (not settled).
- **known** `conn+0xc0` and `conn+0x160` are also both stamped, with one reading, at setup and reset: `0x3664d8`/`0x3664e0` (in `eboot+0x366450`, member added), `0x36697f`/`0x366986` (address arrives), `0x37adbb`/`0x37adc2` (reset), and `0x376e88`/`0x376f33`.
- **inferred** **That leaves one way to an instant kick: a setup stamp taken on a real-time thread, and the test on 'main' within the next 8 ms.** Both parts of the test then see 1.8 × 10^10 s, the test fails, and the network update removes a player it has just added. `0x366450` is called from the wait/update function `0x34a760` (`0x34aa0f`), the session flow `0x3606f0` and `0x369ec0`, and the room-update handler `0x375550` (`0x375d42`). The earlier research found the room-update handler calls it only when `eboot+0xe220` says it is on the game's stored thread, and otherwise queues a request. If that stored thread is 'main' this can't happen. Not settled.
- **inferred** The host thread's flush can see −100 ns against a `conn+0x160` that 'main' stamped in the same 8 ms. It then skips one flush of stream 1 for that connection. The next pass sends.
- **inferred** The chain that produces spurious timed-out resends at −100 ns:
  1. 'game host thread' flushes stream 1 inside the 8 ms after a flip. It stamps `mgr+0xc8` (`0x375fa2`), `conn+0x258` (`0x379133`) and each sent message's `+0x80`.
  2. 'main' runs the network update in the same 8 ms and reads a clock 100 ns lower. `0x3605b1` sees 1.8 × 10^10 s, above 0.06, and flushes stream 1 itself.
  3. In that flush `0x379091` passes for the same reason, and `0x37dbff` marks every unacknowledged stream 1 message stamped in step 1 as timed out.
  4. They are sent again (within the per-flush byte limit of the budget) and counted as timed-out resends.

  The same works with the threads swapped only for step 3 onward, since the host thread always flushes stream 1; and for stream 0 only through the room-update path, because no other thread than 'main' flushes stream 0. Basis: the instructions above. Not observed in a log.

### Session, join and slot code

| Site | Function | What | Compare | A negative difference does |
|---|---|---|---|---|
| `0x3ac11b`/`0x3ac12a` | `0x3abee0`, host thread's per-player table | now − `slot+0x138` vs timeout × 4.0 | unsigned; `jbe` = over the limit, strike count `slot+0x24` + 1, fail when above 4 | one strike; the next call clears it. No kick unless five calls land inside the 8 ms |
| `0x3ac3e3..0x3ac406` | `0x3ac2a0`, the game's per-slot table | now − `slot+0x2f0` vs timeout × 4.0 (two reads, the second is used) | the same, strikes at `+0x2fc` | the same |
| `0x364087`/`0x364096` | session flow `0x3606f0` | now − `[r13+0x258]` vs 1.0 s (`eboot+0x1061c48`) | unsigned; above = proceed | a 1 s wait ends at once. What it gates was not read |
| `0x3a5dc9`/`0x3a5dd4` | data-labels wait `0x3a5d50` | now − loop start vs 10 s (`eboot+0xe7a490`) | unsigned | same thread, local start. Nothing |
| `0x3af192`/`0x3af19d` | `0x3af0e0` | now − local start vs 30 s | unsigned | same thread. Nothing |
| `0x3ad072`/`0x3ad08c` | slot update `0x3acef0` | now − `[r12+0x1830]` vs 40 s (`eboot+0xe7b634`) | unsigned; above = act | acts at once. What it does and who stamps `+0x1830` were not read |
| `0x3ab8c1`/`0x3ab8d4` | `0x3ab5d0` | every 5 s (`eboot+0xe126c0`), stamp `+0x80` rewritten after | unsigned | one early run |
| `0x399505`/`0x399514` | `0x3994e0` | now − `+0xcc20`; result unused | – | nothing |
| `0x39c0f1`/`0x39c100` | `0x39c0c0` | now − `+0xf8`; result unused | – | nothing |
| `0x33d771`/`0x33d780` | settings loader `0x33cc60` | now − `+0x1b0` vs 30 s; `ja` when 30 is above | unsigned | a 30 s wait counts as over |
| `0x33ecc2`/`0x33ecd1` | `0x33ebf0` | now − `+0x1b8` vs 5 s | unsigned | a 5 s wait counts as over |
| `0x34ad2e`/`0x34ad3d` | `0x34a760` | now − local start, ms, through `0x1b300` | – | same thread. Nothing |
| `0x35660b..0x356634` | the 9 s session test `0x356540` | float seconds from `0x1b3f0` minus the float at `eboot+0x130d90c`, vs 10 − 1 | **signed float** (`vsubss`, `vucomiss`, `jbe`) | −8 ms is just −0.008. Nothing |

- **known** The budget's own timers (last notice `conn+0x120`, last cut `+0x124`, busy `+0x128`, the 0.25 s and 5 s tests) are float seconds from `0x1b3f0` (`0x37ccc4`, `0x37d261`, `0x37d808`, `0x37d892`, `0x37d8db`, `0x37d8eb`). They subtract floats. A negative difference stays small and negative.
- **not settled** The many `0x1b3f0` readers in the session flow `0x360803..0x37040a` (the `TIMEOUT_...` waits) were not read one by one. They are float seconds, so the same holds if they subtract floats as the two samples read here do.

### All callers of `eboot+0x1b2a0` (52)

Netcode ones are in the tables above: `0x33d780`, `0x33ecd1`, `0x360580`, `0x3605b1`, `0x364096`, `0x3789e4`, `0x379091`, `0x37937e`, `0x379813`, `0x37b488`, `0x37b4b1`, `0x37d36f`, `0x37dbff`, `0x37f100`, `0x399514`, `0x39c100`, `0x3a5dd4`, `0x3ab8d4`, `0x3ac12a`, `0x3ac3f2`, `0x3ac406`, `0x3ad08c`, `0x3af19d`.

The rest, one line each (known: the instructions around the call; what the function is for is not known unless said):

| Site | Note |
|---|---|
| `0xa8a` | now − `0x13ed0()`; result passed on. Start-up code |
| `0x6d81`, `0x7218` | now − `[obj+0x168]` in the wait loops `0x6cf0` and `0x7200` (`0x7200` calls the network wait `0x34a760`); a loop-until-elapsed test (`jb` loops) |
| `0x9765` | now − a local start in `0x9710`, vs `eboot+0xe49c80`; sets a flag when over |
| `0x47412` | now − `[rbx+0x118]` vs a limit, a state machine in `0x473c0` |
| `0x542ba`, `0x54392` | another clock (`0x1b0d0`) minus a stamp, then clamped at 0 and rounded. Not the monotonic wrapper |
| `0x695b0`, `0x6968e` | now − `[rbx+0x30]` vs `eboot+0xe4f158` in `0x694f0` |
| `0xa0c5a`, `0xa0c91` | now − `[r14+0x8c38]` / `+0x8c40`, two periodic timers in `0xa0c10` |
| `0xab0a7`, `0xab2d5` | elapsed time of a step in `0xaaee0`, stored as a float (a timing figure) |
| `0x2288f2` | now − a table entry's stamp vs `eboot+0xe65cd0` in `0x228500` |
| `0x3bc952`, `0x3bcd35`, `0x3be49c` | `0x3bc7c0..0x3be340`: differences of stamps at `+0x750`/`+0x758` turned to whole seconds for a report. Looks like telemetry; not connection code |
| `0x3bceba` | the other clock (`0x1b0d0`) minus a global, rounded |
| `0x3bde3f`, `0x3bdfcc` | a difference passed in by the caller, turned to whole seconds |
| `0x425d6e`, `0x43634c`, `0x46bdbc`, `0x484da1`, `0x932330` | now itself as float seconds (no subtraction) |
| `0x435d90`, `0x930059` | the low 32 bits of now as float seconds (`mov edi, eax`) |
| `0x87ac23` | now − `[rbx]` vs `eboot+0xed56d4` in `0x87ab90` |
| `0x922e42` | now − `[rbx-0x38]` in `0x922d70` |

Callers of the millisecond twin `0x1b300` (12): `0x72b`, `0x9f39`, `0x4f3e0`, `0x4f458`, `0x5ce67`, `0x5ce95`, `0x5cec2`, `0xaaeda` (jump), `0x236261`, `0x254072`, `0x34ad3d`, `0x37b42b`. Only the last two are netcode.

- **not settled** Inline copies of the unsigned idiom outside these two functions were not searched for. `0x1b3f0` has one inside it, applied to now − start, which is never negative.

## 3. The resend wait and the "timed-out resend"

```
37db87: mov eax, [r13+0xf0]            ; RTT, µs, zero-extended
37db8e: vmovss xmm0, [0x1061c50]       ; 2.0
37dba0: vmovss xmm1, [0x130d95c]       ; added term
37dbbd: vcvtsi2ss xmm2, xmm0, rax
37dbc2: vdivss xmm2, xmm2, [0xe7588c]  ; / 1,000,000
37dbca: vmulss xmm0, xmm2, xmm0
37dbce: vaddss xmm0, xmm0, xmm1
37dbd2: vmovss [rbp-0x44], xmm0        ; the wait
...
37dbe0: cmp [rbx+0x1c], r12d           ; this stream's messages only
37dbe6: cmp qword [rbx+0x80], 0        ; never sent -> goes to the list as it is
37dbf0: call 0x1b1b0 ; sub rax,[rbx+0x80] ; call 0x1b2a0
37dc04: vucomiss xmm0, [rbp-0x44]
37dc09: jbe 0x37dc13
37dc0b: mov word [rbx+0x2], 0x1        ; byte+2 = 1 (timed out), byte+3 = 0
37dc13: cmp dword [rbx+0x90], 0        ; else: extra copies left?
37dc1c: mov word [rbx+0x2], 0x100      ; byte+2 = 0, byte+3 = 1 (extra copy)
```

- **known** No minimum is applied: no `vmaxss`, no compare against a constant between `0x37db87` and `0x37dbd2`. The wait is computed once per flush, before the loop.
- **known** The RTT itself has no floor either. With no sample in the ten buckets it is 0.8 s (`eboot+0xe7586c` at `0x37d4a8`). `0x37d39f` writes 100,000 to it at the start of each recomputation, but `0x37d4d7` overwrites that in the same pass.
- **known** The added term at `eboot+0x130d95c` is in zeroed memory. No writer was found by the earlier research; not searched again here.
- **inferred** So on a LAN the wait is twice a few tens of milliseconds, two or three frames.
- **known** Counting, in the flush after the list is built (`0x37dcb0..0x37dd1a`), for a connection that is not the game's own address: a message with byte `+2` set adds to the current bucket's `+0x1c`; with byte `+3` set, to `+0xc`; neither (a first send), to `+0x8`. Bucket `+0x1c` is the "timed-out resend" figure the trend test at `0x37d63b` works on.
- **known** The count is taken after the trim, so it counts timed-out resends that really go out. `eboot+0x1b640` is an equality test (`sete` on a compare at `0x1b644`). At `0x37dc8d` it compares the game's own ID with the connection's: the own connection goes straight to the count, every other connection goes to the sort and trim at `0x37dd21` and then jumps back into the count (`0x37ddca`, `0x37ddd9`, `0x37dde6` to `0x37dc9e`).
- **known** The two marks are rewritten on every flush that sends the message again: `0x37dc0b` writes byte `+2` = 1, byte `+3` = 0; `0x37dc1c` writes byte `+2` = 0, byte `+3` = 1.
- **inferred** A copy sent as a timed-out resend gives no RTT sample when it is the one acknowledged (see the acknowledgement section; rests on what the entry's bytes mean).

## 4. Can the connection layer be given its own clock?

- **known** There is no connection-level clock function. Every site calls `eboot+0x1b1b0` (or `0x1b3f0`) directly, and those two serve the whole game: 190 and 277 direct call sites.
- **known** Both wrappers keep a frame pointer (`push rbp; mov rbp, rsp` at `0x1b1b0` and `0x1b3f0`), so from inside the port's `sceKernelClockGettime` the game caller's return address is one frame up. The port's `NoteTimeCall` in `kernel_time.cpp` already walks that chain.
- **inferred** So the port can give real time to the connection layer alone by testing that return address. Real time is the same on every thread, so every difference between two such readings is then non-negative whichever threads took them.

The 23 call sites of `0x1b1b0` in `eboot+0x374000..0x382000` (return address is the site + 5):

| Site | Use |
|---|---|
| `0x37e07a`, `0x37e086` | **stamps `msg+0x88`, `msg+0x80`** |
| `0x37f0f1` | **RTT sample** |
| `0x37dbf0` | **resend timer** |
| `0x379082`, `0x379133` | flush pacing, read and stamp of `conn+0x250`/`+0x258` |
| `0x37929c`, `0x37936f`, `0x3793cc` | `conn+0x260` |
| `0x375a0c`, `0x375fa2` | stamps of `mgr+0xc0`/`+0xc8` |
| `0x3789d2` | keepalive send timer, read and stamp |
| `0x37b41c`, `0x37b476` | kick test |
| `0x37d253`, `0x37d2ce` | stamps of `conn+0x168`, `conn+0x160` |
| `0x37adb6` | stamp of `conn+0xc0` (reset) |
| `0x379800` | congestion notice limit |
| `0x37d363`, `0x37d89d` | bucket rotation, read and stamp |
| `0x37e6da`, `0x37ec54` | trailer tick, send and receive |
| `0x37994b` | receive time of a message |

Sites outside that range that read or write the same fields, and must move with them:

| Site | Use |
|---|---|
| `0x36055e` | the network update's read against `mgr+0xc0`/`+0xc8` |
| `0x3664d3`, `0x36697a` | stamps of `conn+0xc0` (member added, address arrives) |

- **inferred** The smallest change that fixes the known victim and the resend timer is four sites: `0x37e07a`, `0x37e086`, `0x37f0f1`, `0x37dbf0`. No other subtraction against those two stamps was seen in the connection range: the reads found are `0x37dbe6`, `0x37dbf5`, `0x37dd7e` (a zero test), `0x37e070` and `0x37f0f6`. Other instructions touch the same offsets (`0x37c787`, `0x37cf6e`, `0x37cf7c`, `0x380803..0x380a01`); they look like zeroing and copying of a message record and were not read. A reader outside the range is not ruled out.
- **inferred** Moving only those four leaves the flush pacing on the frame clock, so step 2 of the chain above can still make 'main' flush early. It no longer marks anything as timed out, because step 3's reading is then real time against a real-time stamp.
- **known** None of the 23 sites is in simulation or pacing code: they are all inside connection functions. What 'main' and 'game host thread' read elsewhere is untouched by a return-address test.
- **inferred** Two things to weigh. The trailer tick (`0x37e6da`, `0x37ec54`) is compared with the other game's clock, so it only needs to be the same clock at both of its sites. And `0x1b3f0` in the same range feeds the budget timers; they compare float seconds with each other only, so they can stay as they are.
- **not settled** Whether any code outside the connection functions reads a connection time stamp and compares it with its own frame-clock reading (for example the stored receive time of `0x37994b`). If the whole range moves to real time, such a reader would see stamps up to 8 ms ahead of its own clock.

## Not settled

1. Which port thread delivers room events to `eboot+0x375550` (through the callback `0xba2890`), and so whether that path is the source of the host's −6.67 ms sample.
2. Whether a guest runs "game host thread" (the conditions in `0x34a760` around `0x34a8c4` and `0x34a8fc`).
3. The thread of `0x35eb10` → `0x35f2d0` → the network update. It decides whether the keepalive kick is safe in every case.
4. What bytes `+2`/`+3` of an acknowledgement entry mean (the decoder was not read), and the thread on which connections are set up (`0x366450` and the other setup stamps), which decides the instant-kick case.
5. Whether the chain of spurious timed-out resends in section 2 happens in practice. It is read from the code, not seen in a log.
6. What the session flow's 1 s test (`0x364096`) and the slot update's 40 s test (`0x3ad08c`) gate, and which thread stamps their fields.
7. The stored RTT after a bad sample was computed, not observed; the figures depend on how many good samples share the window.
8. No log was read in this session. The two logged cases are as the task described them.
