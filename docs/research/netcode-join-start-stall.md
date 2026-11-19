# What stops the host's simulation when a guest starts to join?

Research for the [online co-op netcode map](../../.scratch/netcode/map.md), ticket 23. Written 2026-11-16. It builds on [Does a guest that is still loading make the host wait on every frame?](netcode-loading-guest-wait.md) (ticket 21), answers its open question 2, and corrects three of its points.

## Summary

- **The game stops itself, on purpose, on every PC in the session.** When a guest asks for the game state, the host thread passes the request to every PC that is in the game. Each of them saves its world as a snapshot and clears a "simulation running" byte in its game object, at `[eboot+0x130d5e8]+0x45`. With that byte clear the main loop takes no step and sends no INPUTS. It is not the byte at `+0x46` that the ticket's lead named; that one is next to it and does something narrower.
- **Each PC then waits inside a message handler until every PC has the same state.** On the host's GAME_STATE_HASH each PC rebuilds its world from the snapshot, tells the host "loaded" (message 0x66), and loops in place, still showing frames, until the host says "unblock" (message 0x67). The host sends that only when every PC that got the hash has reported, the joining guest included. The joining guest has to fetch the snapshot from the host first. Only then is the byte set again.
- **So the block lasts as long as the slowest PC needs to get and load the state, and that is mostly round trips.** Six two-instance joins on a direct path (RTT 0 to 5 ms) had 24 to 28 flips with no step, about 0.8 s. The two at 2.4 s (t2 and r10) ran through the relay at an RTT of about 354 ms. A third relayed run with message logging (rm1, RTT 370 ms) shows 2.5 s between the request and the unblock, twice. That is about 0.8 s plus five round trips.
- **With three or more players everyone already in the game stands still for the whole block**, not only the host (known from the code, no log). The slowest PC decides its length. A guest that has just loaded and sits at state 10 is held by the `+0x46` byte only for the host's own turnaround of about two frames, which lies inside the block. The block itself holds it far longer, like everyone else.
- **The port can shorten it only through the path.** Each millisecond of RTT to the joining guest costs about five in the block, so a direct path is the lever. Nothing in the peer link or the socket layer starts or ends the block. A joining guest that hangs while loading keeps every PC waiting until it answers or its slot is removed; how long that can take was not found.

## How this was read

- **Binary:** `eboot.bin`, CUSA00473 1.28. Addresses are module offsets, written `eboot+0x...`, or bare `0x...` where the context is clear.
- **Assembly:** `llvm-objdump` (Intel syntax) on `D:\Recomps\lbp3-recompiled\analysis\disasm_cache\analysis_eboot.elf.text.elf`, and the whole-image listing ticket 21 made from it, searched in this session.
- **Ghidra:** a private copy of `D:\Recomps\lbp3-recompiled\analysis\ghidra\lbp3.gpr` made in this session, opened read-only, headless, at low priority, no analysis run. Decompiler output of the two earlier tickets was read for `0x9d70`, `0x34af50`, `0x3606f0`, `0x39f250`, `0x3a24c0`, `0x3a5b60`, `0x3a5bb0`, `0x3a6300` and `0x3672a0`; the rest was made here.
- **Labels:**
  - **known**: read in this session in the decompiler or the assembly at the address given. "Checked in assembly" is said where the instructions themselves were read.
  - **observed**: seen in a log; file and line or time given.
  - **inferred**: reasoned from known or observed facts; the basis is given.
  - **not found**: looked for and not settled.
- **Logs:** the two-instance runs in `D:\Recomps\lbp3-wt\coop\A\` (host, Kistopawa) and `B\` (guest, Kistopawa2): d1, d2, d3, t0, t1, t2, r10, m2 of 13 November (with `[sim]` lines) and rm1 of 15 November (with `[msg]` lines: `LBP3_SYNC_PROBE=2` prints the first 24 bytes of every session message a game takes, `sync_probe.cpp:353`). No game was started for this ticket. No log has a join with three players: the three-instance files `stderr_m1.txt` written on 16 November hold no join.
- **Names.** Types up to 0x39 are as in ticket 21. Three more are **inferred** here from the same string list: 0x66 LOADED_LEVEL, 0x67 UNBLOCK_LOADING, 0x68 UPDATE_DATA_LABELS. Basis: the list at `eboot+0xe7a534` has two names without `TYPE_` in it (`E_MESSAGE_CLOSED_SAVE_LEVEL_MESSAGE` before index 0x3a, `E_MESSAGE_GO_TO_SCOREBOARD` before 0x4c), so the plain order is two short from there on; counted with them, the three names land on the three numbers, and each fits what its handler does. In a `[msg]` line the first byte is the type, with the top bit set on some (`e8` is 0x68), and the second is the send mode the code passes with it (`0a 13`, `0b 14`, `67 12` match the constants `0x130000000a`, `0x140000000b`, `0x1200000067` at `0x3a5bd2`, `0x3a6576`, `0x3a84e0`).

## (1) What holds the steps

**A byte in each game's own object, cleared when the game saves a snapshot and set when the host has unblocked it. Known from the code; that it is what held the steps in the logged blocks is inferred, with the message times and the frame timing as support.**

### The byte

- **known, checked in assembly** The main loop `eboot+0x9d70` tests the byte at `[eboot+0x130d5e8]+0x45` at the top of every pass of its step loop and leaves the loop when it is 0 (`0xae9f`: `cmp byte ptr [rax+0x45], 0`, `je 0xb49b`). The test comes before the call that takes a COMBINED_INPUTS message (`0x3ae640`), so frames from the host stay unread.
- **known, checked in assembly** The same byte gates the INPUTS sender: with it 0, the call to `0x358180` at `0xc6f2` is skipped (`0xc66b`, `je 0xc6f7`). A game that is stopped sends no input.
- **known** Who writes it, among the byte writes at `+0x45` in `0x347000..0x364000`, from a search of the whole image. That the eight hits outside that range (`0x3eb25`, `0x23ca6a`, `0x23d530`, `0x23edde`, four in `0x4f0000..0x4f2800`, `0x6ec2cb`, `0xd08f82`, `0xd096f6`) belong to other classes is **inferred**: only `0x3eb25` was read, and it is another object's.
  - cleared: `0x349638` (the handler of REQUEST_GAME_STATE, below); `0x347dda` and `0x3480eb` (set-up of the object); `0x349880` (`0x349860`, called from `0x35fc10`); `0x36145c`, `0x3619d8`, `0x361bc3` (party stages 8, 0x0d and 0x0f in `0x3606f0`).
  - set: `0x34ba97` and `0x34c605` (the handler of GAME_STATE_HASH, below); `0x350225` (`0x350110`, on CHANGE_LEVEL); `0x363886` (party stage 0x36).
  - a setter at `0x3480a0` has no direct caller.
- **known** The byte at `+0x46` is a different one. It is read in three places only (`0x35d854`, and through `0x3a1ec0` at `0x3a0a22` and `0x3a7b21`), none of them in the main loop. It holds back the "players changed" entry and a list of deferred room changes on the host thread, as ticket 21 found. See (3).

### The sequence

1. **known** The guest's REQUEST_GAME_STATE (0x0a) reaches the host thread. `0x3a5b60` raises the guest's slot to 8, clears `+0x46`, and `0x3a5bb0` sends the message on to **every slot whose state is 8 or more** (`0x3a5cb1`: `cmp dword ptr [rbx+0x130], 8`, `jl`). That is every PC in the game, the host's own game among them, and the guest itself.
2. **known** A game handles 0x0a in `eboot+0x348e50` (registered at `0x372f82` through `0xba2d10`):
   - If `0x878cd0` is true (a state of the object at `eboot+0x13c0d70` equal to 9 with its top bit set; what state that is was not read), the request is put on a list at `+0x68` of the game object and handled later: `0x3482a0`, run on every network update, takes the list up again once `0x878cd0` is false.
   - If no frame has been stepped since this game's last snapshot (`+0x258` equals `[eboot+0x130d934]`, `0x348fa4`), it only notes who asked.
   - Otherwise it saves the world: three resources are written out (`0x23cc40` at `0x349224`, `0x349246`, `0x349286`), with the players' own data, and the byte at `+0x45` is cleared (`0x349638`). There is no test for "am I the host" or "did I ask" before that. **Every PC in the game stops here.**
   - Only the host's game goes on (`0x3672a0` at `0x3496be`): it sends GAME_STATE_HASH (0x0b, `0x349711`) with the asker's online ID, the four resource descriptors and a state number (`0x3a1fe0`).
3. **known** The host thread handles 0x0b in `0x3a6300`: it sets `+0x46` (`0x3a6338`), raises the asker's slot and every slot at 8 to 9, makes one entry **per slot at 8 or more** in a map at `+0x2d8` of its object at `+0xa10` (`0xbb5c50` at `0x3a6541`; key: one descriptor and the state number; a "loaded" byte at `+0x40`, clear), and sends 0x0b on to every slot at 8 or more (send mode 0x14).
4. **known** A game handles 0x0b in `eboot+0x34af50` (through `0xba2b50`):
   - A game whose state for the host is below 8 ignores it (`0x34b04d`).
   - If the message is for someone else and names the state this game loaded last, it keeps its world.
   - Otherwise it drops its world (`0x246cc0(0)`), loads the snapshot's resources by their descriptors (`0x2373e0`, then `0x236550`, which waits for a resource) and builds a new world. A PC that was in the game made the same snapshot itself in step 2; the joining guest has none of it and must get it from the host.
   - Both ways end the same: `0x34a000` (called at `0x34ba66` and `0x34c2aa`), then `0x349df0`, then the byte at `+0x45` is set (`0x34ba97`, `0x34c605`), checked in assembly.
5. **known** `eboot+0x34a000` is the wait. It sends LOADED_LEVEL (0x66, `0x34a0a2`) with the key, then loops: platform callbacks (`0x6fe00`), one network update (`0x34a760` with 4), resource work (`0x2361a0`). It leaves the loop when the key is in the set at `+0x320` of the game object (`0xba8880`), when the session has at most one PC (`0x34a650`), when the host is no longer the same PC, or when the game is quitting (`0x13ef0`). There is no time limit in it.
6. **known** The host thread handles 0x66 in `0x3a8070`: it sets the "loaded" byte of the sender's entry (`0x3a80ee`) and calls `0x3a8180`, which also runs at the end of every host update (`0x3a0f12`). `0x3a8180` removes entries whose slot is gone, and for a key whose entries are **all** loaded (`0x3a84d5`) sends UNBLOCK_LOADING (0x67, `0x3a84e0`, `0x3a8584`) to every slot at 9 or more.
7. **known** A game's handler of 0x67 (`0xba8ce0`) puts the key into the set at `+0x320` (`0xba81f0`). The loop of step 5 ends.
8. **known** `0x349df0` then does the same once more for the level's data labels. The host's game sends UPDATE_DATA_LABELS (0x68, `0x87a7c0`) and loops in `0x3a5d50` until it has handled that message itself, for at most 10.0 s (`eboot+0xe7a490`). The host thread (`0x3a6640`) makes entries for every slot at 9 or more and sends 0x68 on. A game's handler (`0xba8c00`, `0x87a920`) loads the labels resource and goes through `0x34a000` again, but only when the message carries labels.

What follows:

- **known** While a game waits in step 5 or 8 its main loop is not running at all: the wait is inside the message handler. Frames still come, from `0x236060`, which the loop reaches through `0x2361a0`: it presents a frame when a busy count (`eboot+0x13a55b8`, raised by `0x34af50`) is not 0 and 34.3 ms have passed since the last one (`eboot+0xe65fa4`).
- **known** The step counter the `[sim]` lines print (`0x25a210`) is read out of the world object at `[eboot+0x12ecc80]`, and is 0 when there is none.
- **inferred** The block in the logs is steps 2 to 8 on the host's game. Basis: the code above, and three things seen:
  - **observed** Frames during the block come 35 ms apart, not 33, and the block ends on one short pair: `A\stderr_d3.txt:1609` (`… 38 29 41 34 34 36 35 34 35 35 35 35 35 35 35 35 36 34 35 35 7 23 34 …`), and all 60 gaps of `A\stderr_t2.txt:1592` are 34 to 37 ms. That is the 34.3 ms of the wait's own presenter.
  - **observed** Every block has two jumps of the step counter two or three flips apart near its start (`100?0?000…` in d1, d2, t0, m2, t2, r10; `100?00?000…` in d3, t1). That fits the world being dropped and built again in step 4.
  - **observed** Run rm1, the host's game (`A\stderr_rm1.txt`): it takes `0a 13` at 23:13:37.091 (`:4675`), `0b 14` at 37.156 (`:4677`), `67 12` at 39.622 (`:4710`) and `e8` at 39.689 (`:4712`). The second join of that run: 23:15:09.973, 10.040, 12.538, 12.623 (`:12858`, `:12862`, `:12884`, `:12887`). Neither game takes any other session message between its `0b` and its `67`, so whatever the guest fetches in that time travels on a channel the probe doesn't print. The guest's game takes the same four at 37.372, 37.376, 39.802 and 39.955 (`B\stderr_rm1.txt:13620`, `:13621`, `:13669`, `:13670`).
- **not found** A log that shows the byte itself, or `[sim]` and `[msg]` lines in one run. rm1 has no `[sim]` lines and the 13 November runs have no `[msg]` lines. One two-instance run with `LBP3_TRACE_SIM=1` and `LBP3_SYNC_PROBE=2` together would put the block against `0a` and `e8` directly.
- **not found** Which state `0x878cd0` tests. While it holds on a PC, that PC's handling of the request waits; on the host that delays the answer for everyone.

## (2) What its length depends on

**Mostly the RTT to the joining guest, about five round trips, on top of about 0.8 s of work. Observed for the RTT; the rest is inferred or not found.**

| Run | Path, RTT | Block on the host |
|---|---|---|
| d1 | direct, 5 ms | 28 flips |
| d2 | direct, LAN, 0 ms | 24 |
| d3 | direct, 5 ms | 25 |
| t0 | direct, 5 ms | 26 |
| t1 | direct, 5 ms | 24 |
| m2 | direct, LAN, 0 ms | 25 |
| t2 | relayed, 356 ms | 72 |
| r10 | relayed, 351 to 354 ms | 71 |
| rm1 | relayed, 369 to 372 ms | 2.60 s and 2.65 s from `0a` to `e8` (no `[sim]` lines) |

- **observed** The RTT is in the `[p2p]` line next to each block: `A\stderr_d1.txt` 17:59:48.871, `A\stderr_d2.txt` 18:08:38.818, `A\stderr_d3.txt:1614`, `A\stderr_t0.txt` 21:46:58.528, `A\stderr_t1.txt` 21:53:51.364, `A\stderr_m2.txt:1589`, `A\stderr_t2.txt` 22:05:40.906, `A\stderr_r10.txt:1570` and `:1645`, `A\stderr_rm1.txt:4573` and `:12869`. Flip counts are from ticket 21's table, re-read here.
- **inferred** The block is about 0.8 s plus 4.5 to 5 round trips: flips in the block are 35 ms apart, so (72 − 25) flips is 1.65 s, which at 354 ms is 4.6 round trips; 2.60 s − 0.85 s at 370 ms is 4.7. Basis: the table. Two of those trips are fixed by the sequence in (1): the hash going out and "loaded" coming back. The others must be the joining guest fetching the snapshot from the host, one request after another.
- **inferred** Of the 0.8 s on a direct path, the host's own work is small: its request-to-hash turnaround is 65 ms in rm1 (37.091 to 37.156, and 67 ms at the second join), and the two jumps of the step counter put its rebuild at two or three flips. The rest is the guest getting and loading the state. Basis: those times; the guest's side was not timed.
- **observed** Through the whole join, not only the block, the host's datagrams to the guest leave 100 to 133 ms apart: `B\stderr_d3.txt:1445..1567`, from 18:36:40.0 to 43.0, with the block at about 40.8 to 41.6. Inside the block it is one datagram per 133 ms (`from Kistopawa: seq 26 sent 133.0 ms after its previous datagram`, then 27 and 28), with one run of 20 at 41.36 to 41.49; after it, about three per pause. 26 such pauses in that 5 s window (`:1569`), none in the next (`:1642`). The largest datagram is 317 bytes (`A\stderr_d3.txt:1615`).
- **not found** What sets that 100 to 133 ms rhythm, and whether it bears on the block at all. It may only be the host's cadence toward a guest that is not in the game yet. The wait loop's resource call waits 1 ms, not 100 (`0x21070`: the argument times 1000 µs). What the 0.8 s on a direct path is made of was not found either.
- **not found** The size of the game state, and so how much the send budget matters. Ticket 15 found that the budget limits resource transfer; the snapshot travels that way (inferred: `0x34af50` asks the resource system for it by descriptor). In these runs the level was the Pod and the block did not grow beyond round trips, so the state was small. A large level at a cut budget could add seconds; no log has one.
- **inferred** On 15 November, at 455 ms, the same sum gives about 3 s. Ticket 21 read "about 5 s" off the `[framegen]` lines, which count flips with any number of steps other than one and so include the catch-up after the block, and the friend's PC was also building shaders. Basis: the sum above; nothing in that log marks the block's ends.

## (3) Three or more players, and a guest at state 10

**Everyone in the game stops for the block. The `+0x46` window is a small part of it. Known from the code; no log.**

- **known** A third PC that is in the game has state 0xb on the host, so it gets the request (step 1), saves a snapshot and clears its byte (step 2), gets the hash, rebuilds its world (step 4: its state for the host is 8 or more), reports and waits (step 5). That it finds the snapshot's resources on its own disk or in memory, having written them in step 2, is inferred. It steps again when UNBLOCK_LOADING reaches it.
- **known** The host unblocks nobody until every PC that got the hash has reported (step 6). One slow PC, in practice the joining guest, holds all the others.
- **inferred** For a PC already in the game the standstill is as long as the host's, shifted by its own trip from the host. Basis: it stops on the forwarded request and resumes on the forwarded unblock.
- **inferred** After the block the host resumes first. A third PC's INPUTS stopped while its byte was clear and start again one trip later, so the builder finds that PC's queue empty for about one RTT and waits, by the rule of ticket 17 (at most about a second). With two players this doesn't arise: the joining guest's players are still pending. Basis: the INPUTS gate at `0xc66b` and ticket 17 (a).
- **known** The window in which `+0x46` is 0 runs from the host thread's handling of the request (`0x3a5b8a`) to its handling of the host game's hash (`0x3a6338`). In it `0x35d840` gives out no "players changed" entry, so a guest whose REQUEST_CHANGE_PLAYERS is handled then stays at state 10.
- **observed** A stand-in for that window is two frames: in rm1 the host's game takes `0a` and `0b` 65 and 67 ms apart. The window itself, on the host thread, is not logged; it is that gap less the two loopback hops' difference. It grows only if the host's game puts the request off (`0x878cd0`).
- **inferred** So the hold at state 10 adds no standstill of its own, as ticket 21 guessed: it lies inside the block, in which nobody steps. But the guest at state 10 is itself at 8 or more, so it gets the other guest's request and hash like everyone else, loads the new state and waits for the unblock. What keeps it out of play is the block, for seconds, not the byte. Basis: steps 1 to 6.
- **inferred** A PC at state 8 or 9, still loading its own game state when another guest asks, gets a new hash and has to load that state too. Basis: the forwarding rules of steps 1 and 3 and the test at `0x34b04d`; the path was not followed for such a PC.
- **not found** How a second request is handled while the first block is still on. Each game's handler is inside its wait loop then, and the loop's network update takes messages (`0x34a760` calls `0x3acef0`); whether a 0x0a or 0x0b taken there is handled nested or put off was not read.

## (4) Whether the port can shorten it

**Through the RTT, yes. The block itself is the game's, and nothing under the game's sockets opens or closes it.**

- **inferred** A direct path is worth about five times its RTT saving on every join: at the relay's 455 ms the block is about 3 s, at 5 ms about 0.8 s. This is the largest effect of the path found so far that every player sees, since all of them stand still. Basis: (2).
- **inferred** The link should not hold datagrams in this phase. The exchange is one request after another, so any delay the link adds to a datagram is paid about ten times over (five round trips). Basis: (2); no run with added delay was made.
- **inferred** Keeping the send budget uncut matters here if the state is large (ticket 15). Basis: the snapshot goes by resource transfer; not measured.
- **known** No timer of the port's is involved. The stop is a byte the game writes on a message, and the wait is a loop in the game's handler that ends on another message. The loopback path and the epoll timeouts carry these messages like any others, so "keep our tick" is not touched by anything here.
- **inferred** Skipping or shortening the wait in the eboot is not a port-side option. The games resume from one snapshot on the host's word; a PC that stepped on would run a different world from the one the guest loads. That is the simulation's netcode, which the map rules out. Basis: steps 2 to 7.
- **not found** What ends the wait when the joining guest never reports. The host drops a guest's entries when its slot goes (`0x3a8180`), and a slot goes by the 10 s kick or the join timeouts (30 to 60 s in the `[slots]` lines). Until then every PC shows frames with no step. Whether the rejoin that loaded the Pod but never spawned, or the friend's shader freezes during a join, ran into this was not checked.
- **not found** Whether the 100 to 133 ms rhythm of the host's sends during a join comes from the game or from the layer under its sockets, and whether it lengthens the block. Only if both were so would the port have something here to shorten on a direct path.

## What a later pass needs to settle

Not found, listed in the sections above: a log with the byte or with step counts and message times together; the state `0x878cd0` tests; what paces the host's sends at 100 to 133 ms during a join, and what the 0.8 s on a direct path is made of; the size of the game state and the part the send budget plays; a join with three players in any log; how a second request during a block is handled; what ends the wait for a guest that never reports.

Corrections to earlier findings:

- Ticket 21, (1): type 0x0b is not "the host's game state on its way to the guest, whatever the string says". The string is right: the message carries four resource descriptors and a state number, and the state itself is fetched as resources.
- Ticket 21, (3): "the host's frames kept coming … the host's game was not frozen". The frames came from the wait loop's own presenter; the main loop was not running.
- Ticket 21, (3) and question 2: the blocks of t2 and r10 were three times as long because those two runs went through the relay at about 354 ms, not because of the state 5 seen on the way.
- Ticket 21, (4): the handler of type 0x0b replaces the world object on every PC already in the game at every join, not only on the guest that joins. The lag sample count is reset by that too.
- Ticket 21's lead (the window in which `+0x46` is 0) is real but is two frames long. The byte that stops the steps is `+0x45`.

Questions this raises for the map:

1. **How long can a joining guest that hangs while loading keep every PC in the session without a step, and what do the players see meanwhile?** The wait has no time limit of its own. A two-instance run in which the guest is frozen right after it takes the hash would show whether the 10 s kick or a join timeout ends it.
2. **What is the 0.8 s of a join on a direct path made of, and does the 100 to 133 ms rhythm of the host's datagrams to a joining guest play a part?** The rhythm runs from before the block to about 1.4 s after it; whether the game or the port sets it was not found.
3. **How large is the game state of a real level, and how long is the block at a cut send budget?** All runs here joined in the Pod.
4. **What does a join do to the players already in a three-player session?** The code says they all stop for the block and that the host then waits about one RTT more for each of them. The third-player run should carry `LBP3_TRACE_SIM=1` and `LBP3_SYNC_PROBE=2` on all three PCs.
5. **Should the session log lines of ticket 19 mark the block?** The byte at `[eboot+0x130d5e8]+0x45` going to 0 and back, with the time between, is the standstill every join causes, and it can't be told from a wait for input in the step counts alone.
