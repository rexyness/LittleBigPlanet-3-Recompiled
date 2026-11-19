# Does a guest that is still loading make the host wait on every frame?

Research for the [online co-op netcode map](../../.scratch/netcode/map.md), ticket 21. Written 2026-11-16. It builds on [What the host does with a guest's late input](netcode-late-guest-input.md) (ticket 17) and answers its open point in "How input travels" and its question 4.

## Summary

- **At a join: no.** A joining guest is not one of the players the builder looks at while it loads. Its players are in the host's room list from the start of the join, but with a "pending" byte set, and the builder skips pending players. The host clears the byte when it handles the guest's REQUEST_CHANGE_PLAYERS message, and moves the guest's slot to state 0xb in the same host update, before the builder is called again. The guest enters the builder's set and reaches 0xb together.
- **What a join does cost.** One short wait, until the guest's first accepted INPUTS is queued: 3 to 13 flips with no step in the two-instance runs. Before it, each join has one longer block with no step on the host (0.8 s, or 2.4 s in two runs), around the time the guest asks for the game state and the host's game answers. Its cause was not found; it is not the builder's wait for the guest, because the guest's players are still pending then.
- **A second guest joining at the same time adds nothing new** (inferred, three or more players). The host holds back a first guest's step from state 10 to 0xb while it prepares the game state for a second guest. That window looks like the block with no step above, in which nobody steps anyway; the match was not checked.
- **At a level change: the guest does not go below 0xb** (inferred). Nothing on the level-change path lowers a slot's state or sets the pending byte again. So the guest stays in the builder's set with its INPUTS accepted, and the rule of ticket 17 applies unchanged: the host waits whenever the guest's queue runs empty. Whether a guest's game keeps sending INPUTS while it loads a level was not found. That is now the whole question.
- **The logs can't settle the level change.** No two-instance run with `[sim]` lines has a level change. The 15 November log has no `[sim]` lines; its `[framegen]` lines count flips that had no step or two, and those are high through each level change, but they can't tell the host's own loading from a wait for the guest.
- **The lag sample count is reset whenever a player is added to or removed from the game**, on every PC (known). So the catch-up loading screen's start-of-game rule is live for up to 33 frames after every join and leave, not only for a new game object.

## How this was read

- **Binary:** `eboot.bin`, CUSA00473 1.28. Addresses are module offsets, written `eboot+0x...`, or bare `0x...` where the context is clear.
- **Assembly:** `llvm-objdump` (Intel syntax) on the cached text image `D:\Recomps\lbp3-recompiled\analysis\disasm_cache\analysis_eboot.elf.text.elf`, the whole image, searched and read in this session.
- **Ghidra:** a private copy of `D:\Recomps\lbp3-recompiled\analysis\ghidra\lbp3.gpr` (the copy ticket 17 made, copied again), opened read-only, headless, at low priority, no analysis run. Used for decompiler output and reference lists. Decompiler output of ticket 17's session (15 November) was read for `0x35c2a0`, `0x39ee60`, `0x3a0900`, `0x39e7b0`, `0x35d840`, `0x35d750` and `0x3672a0`.
- **Labels:**
  - **known**: read in this session in the decompiler or the assembly at the address given. "Checked in assembly" is said where the instructions themselves were read.
  - **observed**: seen in a log; file and line or time given.
  - **inferred**: reasoned from known or observed facts; the basis is given.
  - **not found**: looked for and not settled.
- **Logs:** the two-instance runs of 13 November in `D:\Recomps\lbp3-wt\coop\A\` (host, Kistopawa) and `B\` (guest, Kistopawa2), and the runs `rt4` and `rw1` of 15 and 16 November there; the host's `D:\Recomps\lbp3-recompiled\analysis\play_log.txt` of 15 November (cited `H:line`).
- **Names.** Message types 0x0e (INPUTS) and 0x0f (COMBINED_INPUTS) are known from the game's table (ticket 17). The other type names used here are **inferred** from the order of the `E_MESSAGE_TYPE_*` strings at `eboot+0xe7a534`, which puts INPUTS and COMBINED_INPUTS at 0x0e and 0x0f as the table does: 2 REQUEST_JOIN_PARTY, 8 REQUEST_LEVEL_RESOURCES, 9 LEVEL_RESOURCES, 0x0a REQUEST_GAME_STATE, 0x0b GAME_STATE_HASH, 0x0c REQUEST_CHANGE_PLAYERS, 0x10 CHANGE_LEVEL. Two more fits: a game registers `0x375550`, which copies in a whole room record, for type 0x16 (ALL_PARTY_DATA; `0x372fdc`), and the handler that applies a change of players for type 0x0d (SET_NUM_PLAYERS; `0x347e6b`). The party stage names are from the `E_PARTY_STAGE_*` strings, numbered in order; stage 0x21 is REQUEST_LEVEL_RESOURCES and the code of case 0x21 sends message type 8, which fits both lists.

## Two slot tables, and which one "below 0xb" is about

The ticket's premise mixes two tables. They have the same state numbers and move in step, but they are different memory.

- **The host's table** (known). The host thread's object is at `eboot+0x1322760` (`[eboot+0x10cc3b0]` points to it). It holds two arrays of four slots of 0x140 bytes, at `+0x0` and `+0x500`; a slot's state is at `+0x130` and the time it began at `+0x138`. The host update walks the slots at `+0x500` whose state is not 0. `SetState(slot, state, force)` is `eboot+0x3abe80`: it only raises the state unless `force` is set. **This is the table the INPUTS test reads** (`0x39e9b9`: `cmp dword ptr [slot+0x130], 0xb`).
- **Each game's own table** (known from `sync_probe.cpp` and [rejoin_no_spawn.md](../../.scratch/lbp3-port/coop/rejoin_no_spawn.md), not re-read here). `[eboot+0x130d5c0]` points to 8 slots of 0x300 bytes with the state at `+0x2f8`; `SetState` is `eboot+0x3ac240`. It is a game's view of the other players. The `[slots]` lines of the sync probe print this table.
- **observed** On the host, the probe shows the guest at "state 1" for a whole working session (`A\stderr_d3.txt`, 18:36:39 to 18:43:16, 399 s). So a host `[slots]` line says nothing about the state that gates INPUTS. On the guest, the same lines show its slot for the host going 1, (5,) 9, 11, which mirrors what the host's table does for the guest (below).
- **The room** (known). `eboot+0x1323470` (the host object `+0xd10`) holds four player records of 0xb0 bytes, keyed by online ID. Each has a list of 16-byte items at `+0x50`, count at `+0x58`: one item per player on that PC, with the pad number at `+0`, the player number at `+4` and a byte at `+8`. `eboot+0x34cea0` finds the record for a slot. The byte at `+8` is the "pending" byte of this file. Each game keeps its own copy of the room in its own table (`[slots+0x1808]+0x30`, per the probe's source), filled from the host's by message type 0x16 (`0x375550`). The probe's `room:` lines on the newer runs print that copy (`{Kistopawa2 n=1 100000000:1}`: one item, byte 1), not the host thread's room.

## (1) When a joining guest enters the set of players the builder looks at

**Together with state 0xb, in the same host update. Not before.**

What the code does:

- **known, checked in assembly** The builder's players are collected by `eboot+0x39ee60`: for every slot at `+0x500` with a state other than 0 (`0x39ef0b`, `0x39f029`), it takes that PC's room record and adds each item whose pending byte is 0 (`0x39ef6d`: `cmp byte ptr [item+0x8], 0`). There is no test on the slot's state beyond "not 0". A PC with only pending items adds nothing, and is not in the list of PCs the wait rule looks at either.
- **known, checked in assembly** `eboot+0x39e7b0` drops the INPUTS record of a player whose item is pending (`0x39e925`), then drops the whole message when the slot is below 0xb (`0x39e9b9`). It restarts the slot's timer when the state is 10 or 11 (`0x39e815`, `0x39e822`).
- **known, checked in assembly** The pending byte is set and cleared in one function, `eboot+0x3a8ed0`, from its fifth argument (elsewhere items are only copied whole). It is called from one place, in `0x3a6990` (`0x3a6ae4`), which is called from two:
  - `0x3a4907`, in the function that handles a join (`0x3a4490`), right after the new slot is put in state 1: argument 1. The guest's items are **created pending**.
  - `0x3a6969`, in the handler of message type 0x0c, REQUEST_CHANGE_PLAYERS (`0x3a6920`), right after the slot is raised to state 10: argument 0. Existing items of that guest **stop being pending** (`0x3a8ed0`: an item whose byte is not 0 gets the argument).
- **known** The same handler then queues a "players changed" entry in the input manager (`0x3a697e` → `0x35d750`, list at `[eboot+0x130d5c8]+0x4b30`).
- **known** Later in the same host update, `eboot+0x3aa650` takes that entry (`0x35d840`) and raises the slot of that online ID to 0xb (`0x3aa7fa`, not forced). The call order in the update `0x3a0900`, checked in assembly: read the players' messages (`0x3a0770` at `0x3a0978`, which reaches the dispatcher `0x39f250` through `0x39f0e0`, its only caller), then in a loop `0x3aa650` (`0x3a09f3`) and the builder (`0x39ee60` at `0x3a0a16`), then at the end `0x39fde0` (`0x3a0f09`).
- **known** `0x35d840` gives no entry while the byte at `[eboot+0x130d5e8]+0x46` is 0. That byte is set to 1 in the host thread's own function (`0x39c3de`, in `0x39c290`), on message type 0x10 (`0x39f8a0`) and by the handler of type 0x0b (`0x3a6338`); it is cleared by the handler of type 0x0a, REQUEST_GAME_STATE (`0x3a5b8a`). The setter is `eboot+0x356bb0` through `0x3a1ed0`. In `eboot+0x340000..0x3c0000` it is read only at `0x35d854` and through `0x3a1ec0` (`0x3a0a22`, `0x3a7b21`).
- **known** Type 0x0b reaches the host thread from the host's own game, not from the guest. Its handler `0x3a6300` finds the slot by an online ID inside the message (`0x39d0f0`), raises it and every other slot in state 8 to 9, and sends the message on (`0x3a24c0`). The handlers of types 8 and 0x0a act on the sender's slot. A game registers `eboot+0x34af50`, which loads a game state, for type 0x0b (`0x372f9f`). So type 0x0b is the host's game state on its way to the guest, whatever the string says, and state 7 (type 9, same lookup at `0x3a5b1f`) is by the same shape the host game's answer to type 8 on its way.

The host's states for a guest, as the handlers set them (known; `0x39f250` dispatches by type):

| State | Set by | At |
|---|---|---|
| 1 | a join seen; the guest's items are created pending | `0x3a48da` |
| 2 or 3 | `0x39fde0`, when its test of the slot's connection passes (`0x37b1f0`) | `0x39ffe7` |
| 4 | type 2, REQUEST_JOIN_PARTY | `0x3a3ef7` |
| 5 | `0x3a93d0`, which sends the answer (type 3) | `0x3a9414` |
| 6 (forced) | type 8, REQUEST_LEVEL_RESOURCES | `0x3a589f` |
| 7 | type 9 from the host's game, passed on | `0x3a5b33` |
| 8 | type 0x0a, REQUEST_GAME_STATE; byte `+0x46` cleared | `0x3a5b83` |
| 9 | type 0x0b from the host's game, passed on; byte `+0x46` set | `0x3a6364`, `0x3a642f` |
| 10 | type 0x0c, REQUEST_CHANGE_PLAYERS; pending byte cleared | `0x3a694e` |
| 0xb | the "players changed" entry taken | `0x3aa7fa` |

What follows:

- **inferred** While a guest loads on a join (states 1 to 9), its items are pending, so the builder does not look at it and the host does not wait for it. Basis: the two callers of `0x3a6990` and the test at `0x39ef6d`.
- **observed** The pending byte stays set through the guest's load. Run rt4, the host game's copy of the room: the guest's item is `100000000:1` from 19:03:45.043 and `100000000:0` at 19:03:57.675 (`A\stderr_rt4.txt`, `[slots]   room:` lines); the guest's own table has the host at state 9 from 19:03:53.683 and at 11 at 19:03:57.846 (`B\stderr_rt4.txt`).
- **inferred** The guest enters the builder's set and reaches 0xb in one host update: its REQUEST_CHANGE_PLAYERS is read, the byte is cleared, the entry is queued, `0x3aa650` takes it, and only then is the builder called. Basis: the call order above.
- **inferred** The builder then finds this PC with an empty queue, because every INPUTS it sent so far was dropped. It produces nothing on the first call of each update until an INPUTS from the guest is read. That is the wait of ticket 17, for as long as the guest's next INPUTS takes: about one frame plus the trip, if the guest is sending them. Basis: ticket 17's step 5; that the guest sends INPUTS before it is in the game is suggested by `0x39e815`, which restarts the timer of a slot in state 10 on every INPUTS.
- **observed** That short wait is in the logs: see (3), "the second stall".
- **inferred** The one hold. If the byte at `+0x46` is 0 when a guest's REQUEST_CHANGE_PLAYERS is handled, the entry stays queued and the guest sits in state 10 with its pending byte clear: in the builder's set, its INPUTS ignored. Nothing else gets a slot out of 10 (`0x39fde0` only raises a slot from exactly 9, `0x3a047c`). The byte is 0 from another guest's REQUEST_GAME_STATE until the host's game has answered it, so this needs two guests joining at once, and it lasts as long as that answer takes. The block with no step in (3) sits in the same window, so the hold probably adds no standstill of its own; that was not checked. No log shows it: every run has two players.
- **known** A second way to 0xb: at the end of every update, `0x39fde0` raises a slot in state 9 to 0xb when one of its items is not pending (`0x3a047c..0x3a04c9`). It serves a guest that already has players in the room and comes through the join states again; see (2).

## (2) Whether a guest already in the game goes below 0xb again at a level change

**Not on the paths read (inferred). What it needs to settle is a run, not more reading.**

- **known** A slot's state goes down only through a forced `SetState` or a new slot. All 15 calls of `0x3abe80` were read (no jump reaches it). The forced ones:
  - `0x3a589f`: to 6, when the host handles type 8, REQUEST_LEVEL_RESOURCES, from that guest.
  - `0x3a86b7`, `0x3a86ce` (in `0x3a8600`, called for every slot by `0x3a76e0` when the room record changes): to 0xb or 2 when this PC has just become the host, to 3 when it is not the host.
  - `0x3a9800` (in `0x3a93d0`, the function that sends the join answer): to 2, for up to four online IDs it is given, whose items are removed first. Which case that is was not read.
  - `0x3a0066` writes 1 into a fresh slot.
- **known** The mirror on the guest: sending type 8 forces its slot for the host to 6, sending type 0x0a raises it to 8, sending type 0x0c to 10 (`eboot+0x3adb90` and the same three cases in `0xbb6870`).
- **known** A game sends type 8 from two places only, both in the party stage machine `eboot+0x3606f0`: `0x362801` (stage 0x21, REQUEST_LEVEL_RESOURCES) and `0x362acd` (stage 0x22, REQUEST_LEVEL_RESOURCES_IF_NECESSARY, which sends only when a flag at `+0x124` of the party manager is set or the game's state for the host is below 9). Found by listing every call of the typed senders (the 24 functions that call `0x3adb90` with the type from a register, and their callers' constants).
- **known** The stage lists are rows of 26 stage numbers at `eboot+0xe1c990`. Stages 0x21 and 0x22 are in rows 0, 1, 3, 4 and 13 to 18. The rows that are a level change alone are 6, 7, 11 and 12: PREPARE_ADVENTURE, (CHECK_ADVENTURE,) REQUEST_CHANGE_LEVEL or REQUEST_CHANGE_LEVEL_LEAVING_GAME, WAIT_FOR_CHANGE_LEVEL_TO_COMPLETE. They hold no stage that sends type 8 and no REMOVE_ALL_PLAYERS.
- **inferred** So a guest that is in the game is not put back to state 6 by an ordinary level change, and its pending byte is not set again (the only place that sets it is the join at `0x3a4907`). It stays at 0xb, in the builder's set, with its INPUTS accepted. Basis: the four points above. Limits: which row runs for which player action was not read; the guest's handling of type 0x10 (CHANGE_LEVEL) and of the preload messages (types 0x12 to 0x15, host handlers `0x3ab1c0`, `0x3ab330`, `0x3ab440`) was not read; no log has a slot line across a level change.
- **inferred** What this means for the ticket's worry. A level change does not make the host ignore the guest's INPUTS. The host still waits, by ticket 17's rule, whenever the guest's queue runs empty, and a guest that stops sending INPUTS while it loads empties it for the length of the load: about a second of standstill per round, then catch-up. Basis: ticket 17 (a); the builder's set does not change.
- **not found** Whether a guest's game sends INPUTS while it loads a level. The sender `eboot+0x358180` is called from the main loop (`0xc6f2`, ticket 17); whether the main loop runs through a load was not read. On 15 November the friend's game sent 46 to 92% of its usual datagrams during the level changes, and in single 5 s windows as few as 14 of 150 (measurements file, "Joins and level changes"; H:13583), so there were gaps of some kind.
- **known** A level change is still marked on the host thread: type 0x10 sets the byte at `+0x46` (`0x39f8a0`). Nothing else in the level-change messages touches the slot states.

## (3) What the session logs show

### The two-instance runs (13 November)

`[sim]` prints 60 digits per line, the simulation steps of each flip. `?` is a jump of the step counter by 10 or more or backward (`videoout.cpp:596`), not a stall. Both PCs on one machine, direct path.

**observed** Every join looks the same on the host: one block of flips with no step, then steady steps, then one short stall. No run shows rounds of thirty.

| Run | Block with no step (host) | Second stall (host) | Guest's own table: host at 9, at 11 |
|---|---|---|---|
| d1 | 17:59:47.0, 28 flips | 17:59:49.2, 5 flips | no slot lines |
| d2 | 18:08:37.2, 24 | 18:08:39.5, 5 | no slot lines |
| d3 | 18:36:40.8, 25 | 18:36:42.9, 5 | 18:36:42.039, 18:36:43.972 |
| t0 | 21:46:57.9, 26 | 21:47:00.3, 3 | 21:46:59.187, 21:47:01.188 |
| t1 | 21:53:49.7, 24 | 21:53:51.6, 5 | 21:53:50.880, 21:53:52.878 |
| t2 | 22:05:40.7, 72 | 22:05:47.4, 12 | 22:05:44.073, 22:05:48.338 |
| r10 | 23:15:29.0, 71 | 23:15:35.6, 13 | 23:15:32.554, 23:15:36.651 |
| m2 | 23:09:39.3, 25 | 23:09:41.5, 5 | no slot lines on the guest |

- Lines, for d3: `A\stderr_d3.txt:1608` (`[sim] 18:36:42.485 steps per flip: 111111100?00?00000000000000000002111…`) and `:1663` (`…111111111100000111…`); `B\stderr_d3.txt:1547` and `:1600` for the guest's states. For t2: `A\stderr_t2.txt:1562`, `:1591`, `:1635`, `:1704`; `B\stderr_t2.txt:1555`, `:1622`. Times in the table are worked out from the line stamps at 30 flips a second. The probe samples the slot table about every 2 s, so the guest's times are late by up to that.
- **observed** The host's frames kept coming at 33 to 36 ms through the blocks (`A\stderr_d3.txt:1609`, `A\stderr_t2.txt:1592`, the `ms between submits` lines). The host's game was not frozen.
- **observed** The block ends before the guest's table shows the host at 9, and carries two jumps of the host's step counter. In t2 and r10 the guest's table also showed state 5 on the way, and the block was three times as long.
- **inferred** The block is not the builder waiting for the guest. Basis: the guest's items are pending until its REQUEST_CHANGE_PLAYERS, so the wait rule can't see it yet ((1)).
- **not found** What stops the steps in that block. A lead, not checked: the block ends just before the guest shows 9, and 9 is the host's game state passing through the host thread, so the block fits the window from the guest's REQUEST_GAME_STATE to the host game's answer, in which the byte at `+0x46` is 0. The jumps of the step counter suggest the host's game does something to its own simulation to produce that state. Neither what the host's game does with a relayed type 0x0a nor a reader of the byte that stops frames was found.
- **inferred** The second stall is the wait of (1): it falls between the guest's samples "9" and "11" in every run that has them, which is when REQUEST_CHANGE_PLAYERS is handled, and it is short. Basis: the timing; no log line marks the message itself.
- **observed** The guest's side of a join is longer and rougher: `B\stderr_d3.txt:1556` and `:1607` show 36 flips with no step, then about 2 s of alternating 0, 1 and 2 steps.
- **not found** A level change in any run with `[sim]` lines. No file under `coop\A` or `coop\B` has a `play` request. The runs of 15 and 16 November (`rj*`, `rw*`, `rt*`, `rx*`) have slot and room lines but no `[sim]` lines and no level change either.

### 15 November (the session with the friend)

- **observed** `play_log.txt` has no `[sim]`, `[slots]` or `[sync]` line (0 matches each), as the measurements file said.
- **observed** It has a stand-in. Each `[framegen] warp-gpu:` line covers 30 flips and counts as `steps N` the flips whose simulation step count was not exactly one (`runtime/src/video/framegen.cpp:375`: `simSteps != 1`). 3,005 such lines; 1,689 say `steps 0`. It can't tell a flip with no step from one with two, and it can't tell the host waiting for the guest from the host's own game not stepping while it loads. The `rate` count in the same lines is 0 in 3,001 of the 3,005, so it hides nothing. The `[framegen]` lines printed after a `[p2p]` stamp cover the 5 s after it, while that `[p2p]` line's datagram counts cover the 5 s before it.
- **observed** Joins. Connection 1: the five lines after H:2513 (20:13:46) read `steps 30 29 30 30 23`, then `18 22 15 6 13`, then `20 19 4 0 0`, then zeros: about 5 s with almost no clean flip and 9 more of mixed ones. Connection 6: `0 0 3 30 30`, `21 0 0 0 0` after H:13113 (20:36:24), about 3 s.
- **observed** Level changes (the windows of the measurements file):
  - 20:36:44 to 20:37:19 (H:13321..13671): 10 to 29 of every 30 flips, with a few clean seconds in between (`1 0 25 27 28`, `26 10 1 0 5`, `29 18 5 23`, `20 25 19 17`, `11 21 23 24 20`, `27 22 17 17 14`, `18 20 12 13 11`, `21 16 14 13 7`). It did not settle after the traffic did: 43 to 82 of 150 flips per 5 s until 20:37:59.
  - 20:45:40 to 20:46:05 (H:17854..18065): `13 2 16 30 15`, `25 12 9 0 7`, `9 17 2 1 14`, `14 10 12 19 9`, `16 19 18 15 5`.
  - 20:56:26 to 20:56:41 (H:23584..23645): `21 30 15 29 29`, `29 29 28 26 19`, then `8 1 0 1 4`. Ten seconds with almost no clean flip, in the two windows where the friend's game went from 150 to 43 datagrams per 5 s.
- **observed** For scale: in steady play on connection 6 the same count runs from 0 to about 70 of 150 flips per 5 s, and in the calm stretches of connections 1 to 3 it is mostly 0.
- **inferred** The level changes of 15 November disturbed the host's stepping for 10 to 35 s each, far longer than a join in the two-instance runs. The pattern is not clean rounds of thirty, and the counts can't say how much was the host's own load. Basis: the lines above. The log lines ticket 19 adds (steps, flips with no step, longest run with no step) would separate the two.

## (4) What resets the lag sample count behind the catch-up loading screen

**Adding or removing a player. Known.**

- **known, checked in assembly** The samples are a ring of 16-bit values at `[world]+0x3ec`, with the count at `+0x4b4`, the write position at `+0x4b8`, the ring size at `+0x4bc` and the sum at `+0x4c0` (ticket 17). The main loop only ever raises the count (`0xac9d`, `0xad38`). Three places zero the whole block with a `memset` of 0xdc bytes from `+0x3ec` and set the ring size to 100:
  - the constructor `eboot+0x2472b0` (`0x247597`);
  - `eboot+0x2576a0` (`0x25784e`), on every call: no branch before it skips the `memset`. It then appends to the object's player list (`+0x3c0`, entries of 0x13f0 bytes, count at `+0x3c8`; `0x257881`), so it adds a player.
  - `eboot+0x259760` (`0x259b3d`), which looks its argument up in that list by player number (`+0x108`) and returns early when it isn't there (`0x2597b3`, `0x2597d5`); otherwise it takes the entry out and reaches the `memset`. So it removes a player.
- **known** Both are called from `eboot+0x34d960` (`0x34e14c`; `0x34e10f`, `0x34e23f`), which applies a change of players on each PC. Its caller `eboot+0x3563a0` is a game's handler of message type 0x0d, SET_NUM_PLAYERS (registered at `0x347e6b`); the same handler raises the game's state for the host to 0xb, the "11" of the probe. `0x259760` is also called for every player by `eboot+0x259540`, from party stages 0x0d and 0x0f (SETUP_GAMEHOST_STATE_FOR_GAME, REMOVE_ALL_PLAYERS) and from `0x34cc06`.
- **known** The object itself is replaced in two functions only. The pointer at `eboot+0x12ecc80` is written by `eboot+0x246cc0`. It gets a new object at start-up (`0x371`) and in `eboot+0x34af50`, a game's handler of message type 0x0b (registered at `0x372f9f`): directly (`0x34b3fb`, `0x34b48d`) and through `eboot+0x34caf0` (`0x34cbe5`), which that handler alone calls and which then removes every player (`0x34cc06`). The handler first raises the game's state for the host to 9, so it is the game state arriving on a join. The other callers of `0x246cc0` pass the pointer's current value.
- **not found** Whether that handler also runs at a level change. If the host sent every game a new game state for each level, the object would be new each time. A game's handlers of type 0x10, CHANGE_LEVEL, are `0xba94e0` (which calls `eboot+0x350110`) and `0xba9b50`; they were not read. So ticket 17's "whether the world object is rebuilt for each level" stays open, narrowed to those two functions.
- **inferred** So after every join and every leave, on every PC in the game, the count is 0 until the next sample, which is taken when the frame number is a multiple of 33: up to 1.1 s. In that time the first rule of ticket 17 is live: 23 or more host frames waiting raises the catch-up screen, and `FlickerThreshold` keeps it for at least 2 s. Basis: the reset above and the rule at `0xab4a..0xab6d`.
- **inferred** For a guest that has just joined, this is the screen ticket 17 described. For the players already in the game, it needs 23 unread host frames within a second of someone joining or leaving, which takes a stall of that length on their side. Basis: the same rule.
- **not found** Whether a level change adds or removes players in this sense. The level-change stage rows hold no REMOVE_ALL_PLAYERS ((2)), and a game's handling of CHANGE_LEVEL was not read.

## Not found, and new questions

Not found, listed in the sections above: what stops the host's steps in the block at the start of a join; whether a guest's game sends INPUTS while it loads a level; the guest's handling of CHANGE_LEVEL (`0x350110`) and the preload messages; which stage row runs for which action; a log of a level change with step counts; whether a level change adds or removes players or brings a new game object.

Corrections to earlier findings:

- Ticket 17, "How input travels": "the sender's slot on the host is in a state below 0xb, which is a player still joining" is right about the host's own table (`eboot+0x1322760`, state at `+0x130`), not the table the probe prints. A joining guest's players are also left out of the builder by their pending byte, which that file didn't have.
- Ticket 17, (c): the sample count is reset by every player change, not only by a new game object.

Questions this raises for the map:

1. **Does a guest's game send INPUTS while it loads a level, and how long is the longest gap?** A level change leaves the guest in the builder's set, so every gap in its INPUTS during the load is a standstill for everyone, in rounds of about a second. A two-instance run with `LBP3_TRACE_SIM` and one level change, with the host's `[sim]` lines set against the guest's datagram counts, answers it. It replaces the ticket's question for the "Loads" item.
2. **What stops the host's simulation for 0.8 to 2.4 s when a guest starts to join, and why was it three times longer in t2 and r10?** It is not the wait rule. The lead in (3): it is the time the host's game takes to answer the guest's REQUEST_GAME_STATE. On 15 November the same phase took about 5 s on connection 1. With three or more players it is also how long a guest that has just finished loading can be held at state 10.
3. **Should the session log lines of ticket 19 also carry the host's own slot states** (`eboot+0x1322760`, four slots of 0x140 at `+0x500`, state at `+0x130`) **and the byte at `[eboot+0x130d5e8]+0x46`?** The probe prints the other table, in which the host shows a guest at state 1 for the whole session.
