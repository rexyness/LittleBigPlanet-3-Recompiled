# What are the entries the host thread keeps awaiting after a join?

Research for the [online co-op netcode map](../../.scratch/netcode/map.md), ticket 34. Written 2026-11-17. It builds on [What stops the host's simulation when a guest starts to join?](netcode-join-start-stall.md) (ticket 23) and [Can a game take part in a join and never report the game state loaded?](netcode-never-reports-loaded.md) (ticket 28), and corrects one sentence of the second.

## Summary

- **The pair that stays is not a game state pair. It is the pair the host thread makes for the data labels message (UPDATE_DATA_LABELS, 0x68) that follows every game state exchange.** The host thread makes one entry per PC for every 0x68 it takes, whether the message carries labels or not. A game reports "loaded" for it only when the message carries labels. In the Pod it carries none, so nobody ever reports and the entries stay.
- **It is a new pair, not the first pair with its "loaded" bytes cleared.** Nothing in the eboot clears that byte: it is written 0 when an entry is made and 1 by the handler of 0x66, and nowhere else. The first pair is erased when the unblock is sent; the labels pair is made a few milliseconds later, which is why two of three logs caught no empty sample.
- **A level load with two or more PCs in the session makes the same kind of pair again**, by the same message, sent from the level loader.
- **Nothing waits on these entries.** Only four functions of the host thread touch the map. The only thing an entry can hold back is the unblock (0x67) for its own key, and no game ever waits for the unblock of a labels message without labels. Later joins, level changes, spawns and timers do not look at the map.
- **All entries of this kind share one key** (an all-zero descriptor of type 0x42 and the number 0), so they collect in one group. The host thread's per-update check leaves that group at its first entry. The earlier finding that each such message has its own key was wrong.
- **They pile up, slowly, and it costs next to nothing.** Each entry is 80 bytes. A join or a level load without labels adds one per PC in the session. A guest's entries go when its slot goes; the host's own are never removed. A thousand level loads with four players are 4,000 entries, 320 KB, and one walk over them per host update.
- **The session log reads the entries correctly but names them wrongly.** `SesExchange` has the offsets right. It cannot tell a labels entry from a game state entry, so it prints stale labels entries as "game state … awaited", never prints "over" again after the first join, and stops at 16 entries. A watchdog that goes by the "loaded" bytes must skip entries whose descriptor is all zero.

## How this was read

- **Binary:** `eboot.bin`, CUSA00473 1.28. Addresses are module offsets, written `eboot+0x...`, or bare `0x...` where the context is clear.
- **Assembly:** `llvm-objdump` (Intel syntax) on `D:\Recomps\lbp3-recompiled\analysis\ghidra\eboot_etexec.elf`, the whole image written to one listing in this session and searched.
- **Ghidra:** the project's headless decompiler script (`.scratch/lbp3-port/tools/ghidra_decomp.sh`), at low priority. Decompiled here: `0x3a6640`, `0x3a8180`, `0x3a8070`, `0x3a6300`, `0xbb5c50`, `0xbb5890`, `0xbb46a0`, `0xbb41d0`, `0xbb5e20`, `0xba8c00`, `0x87a920`, `0x87a7c0`, `0x349df0`, `0x3a5e30`, `0x3a5d50`, `0x34a000`, `0x73d0` (the function around `0x9035`), `0xcf9060`, `0xcf8f10`, `0xbb59b0`, `0x1b0d0`, `0x33be70`, `0x9710`, `0x33efe0`, `0x33efd0`.
- **Labels:**
  - **known**: read in this session in the decompiler or the assembly at the address given. "Checked in assembly" is said where the instructions themselves were read.
  - **observed**: seen in a log; file and line given.
  - **inferred**: reasoned from known or observed facts; the basis is given.
  - **not found**: looked for and not settled.
- **Logs:** the host's session logs `D:\Recomps\lbp3-wt\coop\A\stderr_sl3.txt`, `stderr_sl4.txt` and `stderr_sl6.txt` (two instances, a join in the Pod, a level start, the guest dropped). They have `[ses]` lines and no `[msg]` lines. No game was started for this ticket.
- **Names.** GAME_STATE_HASH is message 0x0b, LOADED_LEVEL 0x66, UNBLOCK_LOADING 0x67, UPDATE_DATA_LABELS 0x68, as in ticket 23. "The map" is the tree whose head pointer is at `eboot+0x1322760 + 0xa10 + 0x2e0`. An "entry" is one node of it.

## (0) The map and its entries

- **known, checked in assembly** The map is a tree that allows equal keys. Its head node is allocated in the constructor of the host object (`0xbb3e0d`: 0x50 bytes; `0xbb3e2f` sets the "is head" byte at `+0x19`; `0xbb3e33` stores the pointer at `+0xcf0` of the object, which is `0xa10 + 0x2e0`). The count of entries is the quadword after the head pointer, at `+0x2e8` of the object at `+0xa10` (set to 0 at `0xbb3df7` and again in the destructor at `0xbb35cb`).
- **known, checked in assembly** An entry is 0x50 bytes (`0x3a677f`: `mov edi, 0x50` before the allocator `0x14f80`). Its layout, from the writes at `0x3a6794` to `0x3a67fd`: left `+0`, parent `+8`, right `+0x10`, colour `+0x18`, "is head" `+0x19` (0), the key at `+0x20` to `+0x3f`, the "loaded" byte at `+0x40` (0), the pointer to the host thread's slot at `+0x48`.
- **known** The key is a resource descriptor and a number: a 32-bit value at `+0x20`, 20 bytes at `+0x24` (the hash of the resource's bytes), a type at `+0x38` whose top bit is ignored in comparisons, and the number at `+0x3c`. The insert `0xbb5c50` orders by type, then the value at `+0x20`, then the 20 bytes, then the number.
- **known** `0xbb5c50` never refuses an insert: it walks down to a leaf and calls `0xbb5e20` with no test for an equal key on the way. An entry with a key that is already there goes in after the others.
- **known** Who touches the map, in the whole image:
  - `0x3a6300`, the host thread's handler of 0x0b: inserts (`0x3a6541`).
  - `0x3a6640`, the host thread's handler of 0x68: inserts (`0x3a6814`).
  - `0x3a8070`, the host thread's handler of 0x66: sets "loaded" (`0x3a80ee`).
  - `0x3a8180`, at the end of every host update (`0x3a0f12`) and at the end of `0x3a8070` (`jmp` at `0x3a8170`): erases and sends 0x67.
  - the constructor (`0xbb3e33`) and the destructor `0xbb3580` (called only from `0xbb33d1`, reached only by a `jmp` at `0xbb2850`).
  - Basis, from a search of the whole image for `call` and `jmp`: the tree's own routines have no other callers (`0xbb5c50`: `0x3a6541`, `0x3a6814`; `0xbb41d0`: `0x3a82a4` and the erase-by-key at `0xbb4846`; `0xbb46a0`: `0x3a85a8`; `0xbb5890`: `0x3a8471`; the range search `0xbb5410`: `0xbb46d5`). And in the host thread's code, `0x39c000` to `0x3ab000`, the displacements `+0x2d8`, `+0x2e0` and `+0x2e8` are used as a tree's only at ten instructions of those four functions (`0x3a63d4`, `0x3a64bf`, `0x3a66d4`, `0x3a6794`, `0x3a8087`, `0x3a80a0`, `0x3a8204`, `0x3a8212`, `0x3a82b0`, `0x3a85b4`); the other hits there are two compares of a quadword at `+0x2d8`, at `0x3995e7` and `0x399e68`, and a stack slot at `0x399a5e`.
- **inferred** No function outside those reaches the map. Basis: the list above. Not every road was closed:
  - the object at `+0xa10` is also passed to nine other functions (the twelve `mov edi, 0xa10` sites, at `0x39ca90` and in the dispatcher `0x39f250`, call `0x3aa0f0`, `0x3a2b60`, `0x3a3e60`, `0x3a4b60`, `0x3a5160`, `0x3a5120`, `0x3a5a30`, `0x3a6920` and `0x3a5170` besides the three handlers above); they were not read, but none of them holds one of the displacements;
  - the two compares at `0x3995e7` and `0x399e68` were not read and are taken to be on another object, being in the resource code that ticket 28 read;
  - the hits of the same displacements elsewhere in the image (in the game object's code between `0x344000` and `0x35f000`, and about forty further away) were not read one by one.

## (1) What makes the pair that stays, and what would mark it loaded

**The host thread's handler of UPDATE_DATA_LABELS makes it. Only a 0x66 with the same key would mark it loaded, and no game sends one for a message without labels. Known from the code; that the logged pair is this one is inferred, with the order of the log lines as support.**

### The message and who sends it

- **known** The host's game sends 0x68 from one place, `0x87a7c0`, called only from `0x3a5d50` (`0x3a5d63`), which is called only from `0x3a5e30` (`0x3a61ab`). `0x3a5e30` has two callers:
  - `0x349df0` (`0x349f2a`), which the handler of GAME_STATE_HASH `0x34af50` calls after its wait for the unblock (`0x34ba92`, `0x34c5f9`). This is the one at every join (ticket 23, step 8).
  - the level loader `0x73d0` (`0x9035`), after a level has loaded with success (the call follows the string `LEVEL_init_success` being logged with 1, `0x9011`).
- **known** `0x3a5e30` builds the message: a count of labels, a descriptor of 0x1c bytes, and a number from the clock (`0x1b0d0`). When this PC is not the host, or the world has no labels, the descriptor is all zero with the type 0x42 (`0x3a5e30`: the seven fields set to 0 and to 0x42). Only with labels does the host write them out as a resource and get a real descriptor (`0x23cc40`, called only when the count is not 0).
- **known** At a level load the call is made only when a count from `0x33efe0` is above 1 and a byte at `+0x170` of the loader's object is not 1. The argument is the "am I the host" answer of `0x33be70`, which is `0x3672a0`. The two tests were checked in assembly (`0x85bc`: `cmp eax, 1`, `jbe 0x85eb`, where the flag in `r13b` is cleared; `0x85d5`: `cmp bl, 1`, `setne r13b`). That this flag is the one tested just before the call (`0x9028`) was read in the decompiler only.
- **known** `0x33efe0` counts the records in use among the four of 0xb0 bytes in the session's table of members (the table `0x3af8c0` returns, the same one the wait loop `0x34a000` walks).
- **inferred** So a host alone in its session sends no 0x68 at a level load. Basis: the two points above; no log shows a lone host's level load with the host thread's entries printed.

### What the host thread does with it

- **known, checked in assembly** `0x3a6640` goes over the host thread's slots and makes one entry for every slot whose state is 9 or more (`0x3a6715`: `cmp dword ptr [r12 + 0x130], 9`, `jl`). It does not look at the count of labels.
- **known, checked in assembly** The entry's key is the message's descriptor (the message's `+4` to `+0x1f`, read at `0x3a672b` to `0x3a6748`) and the number **0** (`0x3a67d5`: `mov dword ptr [rax + 0x3c], 0`). The clock number of the message, at its `+0x20`, is not read here. Compare `0x3a6300`, which takes the number from the hash message (`+0xb0`).
- **known** It then sends 0x68 on to the PCs (`0xbb59b0`, `0x3a24c0` with class 6).
- **inferred** So every 0x68 without labels gives entries with one and the same key: 0 at `+0x20`, twenty zero bytes, type 0x42, number 0. Basis: the two points above.
- **observed** In the Pod the message carries no labels: `e8 00 00 42 80 e1 d1 86 02`, 9 bytes (`A\stderr_rm1.txt:4712`, `:12887`, cited by ticket 28).
- **inferred** Those bytes are the count 0, a descriptor of two bytes (`00 42`: empty, type 0x42) and a five-byte number, the clock value. Basis: the order of the fields in `0xcf8f10` and the length; the descriptor's own writer `0xbadd70` was not read.

### What would mark it loaded, and what erases it

- **known** A game's handler of 0x68, `0x87a920`, tests the count first (`*param_1 == 0`). With no labels it skips everything, stores the clock number at `+0x564` of the world, sets the "handled" byte at `+0x650` and returns. Only with labels does it fetch the labels resource and go into the wait `0x34a000`, which sends 0x66 with the key (the descriptor and the number 0).
- **known, checked in assembly** `0x3a8070` sets "loaded" on every entry whose slot is the sender's and whose key equals the one in the 0x66 (`0x3a80e3` to `0x3a80ee`). That is the only write of 1 to `+0x40`. The only writes of 0 are the two at creation (`0x3a6509`, `0x3a67dc`). The tree's routines between `0xbb4000` and `0xbb6000` write no byte at `+0x40`.
- **known** `0x3a8180` erases in two ways:
  - an entry whose slot pointer is not among the slots in use (state not 0) is erased, loaded or not (`0x3a82d7` compares the pointer, `0x3a82fe` and `0x3a838f` test the state, `0x3a82a4` erases the one node; no test of `+0x40` on this path);
  - a key whose entries are all loaded gets the unblock sent and all its entries erased (`0xbb46a0` at `0x3a85a8`, which finds the key's whole range with `0xbb5410` and erases every node of it).
- **known** At its top `0x3a8180` returns at once when no slot is in use at all, before it erases anything.
- **inferred** So an entry of a 0x68 without labels is never marked loaded. It goes only when its slot's state becomes 0 while another slot is still in use. For a guest that is when it leaves. For the host's own slot no such moment was found, so its entries stay as long as the game runs. Basis: the points above; the destructor `0xbb3580` is reached from one `jmp` only and was not traced further.

### The logs against this

- **observed** The pair is a new one in sl4: the map is empty at 19:11:34.638 (`A\stderr_sl4.txt:1705`, "over after 4.1 s") and holds two awaited entries 0.1 s later (`:1716`, `:1719`).
- **observed** In sl6 the new pair is there 0.11 s before the block ends (`A\stderr_sl6.txt:1459` at 19:39:05.171, `:1468` at 05.282). That is the order of the code: `0x349df0` is called at `0x34ba92` and the "simulation running" byte is set after it, at `0x34ba97`.
- **inferred** In sl3 (`A\stderr_sl3.txt:1635`, `:1652`, `:1653`) and sl6 no empty sample was caught because the host thread erases the first pair and makes the second within one pass of the host game's handler: the game takes the 0x67, leaves its wait, and sends the 0x68 straight away. The log samples every 0.1 s. Basis: the code path above and sl4.
- **observed** A level start adds two entries (`A\stderr_sl3.txt:2548`, `A\stderr_sl4.txt:2601`, `A\stderr_sl6.txt:2135`: four awaited). No join block is logged with it, which fits the level loader's call and not a game state exchange.
- **inferred** The level of those runs has no data labels either. Basis: the entries were never marked loaded; the logs have no `[msg]` lines to show the message.
- **observed** When the guest's slot goes, its two entries go and the host's two stay (`A\stderr_sl3.txt:5724`, `A\stderr_sl4.txt:5529`, `A\stderr_sl6.txt:5250`).

## (2) Whether anything waits on it

**No. Known from the code.**

- **known** The map has one reader that acts on "loaded": the second loop of `0x3a8180`. For each key in turn it finds the end of the key's range (`0xbb5890`) and walks the range. At the first entry that is not loaded it goes on to the next key (`0x3a84d5`: `cmp byte ptr [rcx + 0x40], 0`, then `jmp 0x3a85bf`, which continues with the range's end; checked in assembly). An unloaded entry holds back the 0x67 of its own key and nothing else.
- **known** A game waits for a 0x67 in one place, the loop of `0x34a000`, and for the key it was called with: the lookup in the game's set of unblocked keys, `0xba8880`, has one caller (`0x34a386`), and the handler of 0x67 fills that set through `0xba81f0`, called only at `0xba8da3`. `0x34a000` has three callers in the whole image: `0x34ba66` and `0x34c2aa` in the handler of GAME_STATE_HASH, and `0x87aaf8` in `0x87a920`, on the path taken only when the message carries labels. For a 0x68 without labels no game calls it, so no game waits for the 0x67 that the stale entries hold back.
- **known** A later join makes entries under the hash message's own key (`0x3a6300`: one descriptor of the message and the number at its `+0xb0`).
- **inferred** That key differs from the stale one, so the stale group is not in its range and the join's unblock does not depend on it. Basis: the descriptor names a resource the host has just written out, by the hash of its bytes, where the stale key has twenty zero bytes; the type of that descriptor was not read. The logs agree: the level start and the rest of each session ran with stale entries in the map (`A\stderr_sl3.txt:2548` and after), though none of the three runs has a second join.
- **known** A 0x68 with labels has a real descriptor, so its entries are a group of their own too, reported and erased as ticket 28 describes.
- **known** No timer reads the map, and neither do the "players changed" entry, the `+0x46` byte or the slot states: see the list of the map's users in (0).
- **inferred** A spawn and a level change are not held either. Basis: neither goes through any of the four functions except by sending a new 0x0b or 0x68, which only adds entries.
- **known** `0x34a000` empties the game's own set of unblocked keys (at `+0x320` of the game object) every time it returns (`0xba8830`, then the head's links reset), so nothing is carried from one wait to the next on the game's side either.

## (3) Whether they pile up, and what it costs

**They do, by one entry per PC for every join and every level load without labels. The cost is small. Known for the mechanism; the sizes are arithmetic.**

- **known** One entry is added per slot at state 9 or more for each 0x68 (`0x3a6640`). A guest's entries are erased when its slot goes; the host's own are not (see (1)).
- **inferred** A host alone adds nothing at a level load, so the count grows only while guests are in. Basis: the test at `0x85bc` and what `0x33efe0` counts.
- **inferred** Memory: 80 bytes an entry before the allocator's own overhead. Two PCs add 160 bytes per level load, four add 320. A thousand level loads with four players are 4,000 entries, 320 KB, of which the guests' three quarters go when they leave. Basis: the allocation of 0x50.
- **known** The walk: the first loop of `0x3a8180` visits every entry once per host update and compares its slot pointer with the slots in use, four at most. The second loop costs one search (`0xbb5890`) and one test for the whole stale group, since they share a key. `0x3a8070` also visits every entry once per 0x66, with a key comparison.
- **inferred** At a few thousand entries that is some tens of microseconds per host update, spent chasing pointers. Basis: the loop bodies; not measured, and how often the host update runs was not read here.
- **inferred** Nothing else grows with them. The entries hold no reference to a resource or a slot beyond the bare pointer, and an entry whose slot is reused by a new guest was already erased when the slot went to 0. Basis: the entry's layout and the first loop of `0x3a8180`.

## (4) The session log's reading of the map

- **known** `SesExchange` (`D:\Recomps\lbp3-wt\netdelay\runtime\src\core\sync_probe.cpp:662`) has the layout right: the head pointer at `+0xa10 + 0x2e0`, the root at the head's `+8`, left `+0`, right `+0x10`, "is head" `+0x19`, "loaded" `+0x40`, the slot at `+0x48`, slots 0x140 apart from `+0x500`. Each matches the code in (0). A wrong offset is not the explanation.
- **known** What it gets wrong is the meaning:
  - It does not read the key, so entries of a data labels message print as "game state … awaited".
  - It prints "game state exchange: over" only when the map is empty. After the first join in a session without labels the map is never empty again, so that line cannot come a second time.
  - It stops at 16 entries (`seen < 16`). With two PCs that is one join and seven level loads; after that a real exchange's entries may not be printed at all.
  - It walks the tree from the root with a stack, so the order of the printed slots is not the order of the keys (`[1] [0] [1] [0]` in `A\stderr_sl3.txt:2548`).
- **inferred** A reading that tells them apart: print the type at `+0x38` (0x42, with the top bit masked off, is data labels) and the number at `+0x3c`, and treat an entry as stale when its type is 0x42 and its 20 bytes at `+0x24` are all zero. That is exactly what `0x3a6640` writes for a message without labels. The count of entries can be read at `+0xa10 + 0x2e8`. Basis: the key layout in (0) and (1).
- **inferred** For the watchdog that ticket 14 weighs: "an entry that is not loaded" does not mean "a PC that is awaited". It should leave out entries of type 0x42 with an all-zero hash, and can go by the "simulation running" byte at `[eboot+0x130d5e8]+0x45` first, looking at the entries only while that byte is 0. A data labels entry with a real hash is a PC truly awaited. Basis: (1) and (2).

## Not found

- Whether the host's own stale entries are ever erased before the game exits: no moment was found in which slot 0 is free and another slot is in use, and the destructor's caller was not traced.
- What the byte at `+0x170` of the level loader's object means; it was read only as a test.
- How often the host update runs, and so the walk's cost per second.
- A `[msg]` line for a 0x68 at a level start, or for one that carries labels. No log has either.
- The type value of the game state key (the descriptor of the hash message that `0x3a6300` uses), which would let a log line name both kinds of entry.

## Corrections to earlier findings

- [Can a game take part in a join and never report the game state loaded?](netcode-never-reports-loaded.md), section (3): "The key carries a number from `0x1b0d0`, so two such messages don't share one." The key's number is 0 for every data labels message (`0x3a67d5`); the clock number is a separate field of the message that only the games store. Messages without labels all share one key, and two messages with the same labels resource share one too. The rest of that point stands: the entries are never reported, stay until their slot goes, and hold nobody.
- Ticket 34's question: "a pair whose loaded bytes were cleared isn't ruled out". It is ruled out: nothing clears the byte.
- The session log's line "game state exchange: … awaited" after a join's block, and the map's wording "the host thread's list of who has loaded keeps entries after every join": the entries kept are data labels entries, not game state entries.

## Questions this raises

1. **Should the session log name the two kinds of entry and drop the stale ones?** As it is, the exchange line is misleading after the first join and blind after 16 entries. See (4).
2. **What happens when two data labels messages with the same labels resource are open at once?** They share a key, so the host thread treats them as one group and sends one unblock for both. This needs a level with labels and two joins close together; no log has a level with labels at all.
3. **Does a level with data labels behave as the code says at a level start?** There the level loader's 0x68 carries labels, every PC fetches them and waits for the unblock, and that wait is not inside a join block, so the session log's "join block" lines would not mark it. A run that starts a level with labels would show whether the players see a standstill there.
