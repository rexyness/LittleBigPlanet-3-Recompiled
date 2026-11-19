# Can the host's snapshot go away while a guest is still fetching it?

Research for the [online co-op netcode map](../../.scratch/netcode/map.md), ticket 30. Written 2026-11-16. It builds on [Can a game take part in a join and never report the game state loaded?](netcode-never-reports-loaded.md), answers its open questions 5 and 6 and the open point of [What stops the host's simulation when a guest starts to join?](netcode-join-start-stall.md) on a second request, and corrects three points of the first and two of the second.

## Summary

- **No, by no road found.** A snapshot is not an object in the host's memory that a later join overwrites. Each of its resources is written once into the game's temporary cache, the file `output/temp_orbis` (512 MB, 4,096 entries), under the hash of its bytes. A second snapshot goes in under other hashes, or finds its bytes already there and writes nothing. Nothing read in a join, a rejoin or a leave deletes an entry; the handler of a level change was not read.
- **An entry that is being sent can't be evicted.** The cache is a ring: a new entry takes the place of old ones only when the write position comes round to them, and it steps over any entry with a reader. The host holds a reader on the entry for the whole of a transfer. On the test instances the two cache files hold 480 KB and 948 KB after three days of runs, with nothing evicted yet.
- **A second request during a block is queued, not handled.** While a game waits for the host's "unblock" it reads one class of session message only, the class that carries the unblock. The forwarded request and the hash are of another class and stay in the stream until the handler has returned. So the two exchanges run one after the other on every game, and a guest has finished fetching the first snapshot before any game acts on the second request. There is one side door: a request a game had already taken and put off is handled inside the wait; that only adds a snapshot beside the first. Known for the reading rule and the side door's call path; that two overlapping joins then run cleanly is inferred, and no log has one.
- **So the first guest gets neither silence nor empty pieces nor a failure from a second join: its transfer goes on.** Had the entry been gone before the transfer was opened, the host would answer "I don't have it", which fails cleanly in seconds. Bytes overwritten under a transfer would arrive whole and fail the hash check at the end, cleanly too. Empty pieces need a file read on the host that returns nothing, which is a fault of the disk or of the port's file layer, not of a join.
- **The snapshot is read from a file, not from memory.** The earlier finding's "a failed read answers with silence for a resource kept in memory" is about two other stores (an archive loaded into memory and the streaming store). The snapshot's road is the file branch.
- **One way a host does answer with silence was found, and no way into it during a join.** The host ignores a request, for the size or for the bytes, when it is already sending that resource to the same player. A guest that asks again while the host's earlier transfer to it is still open gets no answer and waits 600 s. An honest join asks once per resource.
- **With three or more players, the untimed second player is real but narrow.** A second player is asked with kind 2 only after the first one, who had given the size, dropped out. It is untimed only until its first piece arrives; every piece stamps it. If it leaves, its entry is cleared. The request is then put back in the queue when nobody is left to ask; when another player is still listed (four or more players), nobody asks that player and the fetch is stranded. A player that stays connected and sends nothing holds it without limit, but that needs one of the silences above.
- **The `[slots]` lines can't show any of this.** Their `xfer out n=… in n=…` part reads another transfer manager, the one at `eboot+0x12cc4e8` of the rejoin without a spawn, not the resource transfer manager at `eboot+0x1315b18` that carries the snapshot. In the three runs that have the lines, the guest's record in that other manager does not drain after a rejoin (run rt2, rt4); that is the other lane's bug.

## How this was read

- **Binary:** `eboot.bin`, CUSA00473 1.28. Addresses are module offsets, written `eboot+0x...`, or bare `0x...` where the context is clear.
- **Ghidra:** a private copy of `D:\Recomps\lbp3-recompiled\analysis\ghidra\lbp3.gpr` made in this session, opened read-only, headless, at low priority, no analysis run. Decompiled here: `0x348e50`, `0x23cc40`, `0x220db0`, `0x220e70`, `0x220790`, `0x220cb0`, `0x220a10`, `0x21fbf0`, `0x21fb90`, `0x21ff10`, `0x221150`, `0x21ed20`, `0x224450`, `0xb51f20`, `0x223530`, `0x2239b0`, `0x223ab0`, `0x223c30`, `0x223cd0`, `0xb511a0`, `0xb512d0`, `0xb513b0`, `0xb50e90`, `0xb50da0`, `0x223dd0`, `0x911cd0`, `0x911d30`, `0x911d70`, `0x911e00`, `0x911f10`, `0x2269d0`, `0x225010`, `0x9b9f0`, `0x9ba80`, `0x9bb70`, `0x9bdc0`, `0x9c140`, `0x9c4c0`, `0x8b6cd0`, `0x8b6d60`, `0x8b6e40`, `0x8b6a30`, `0x8b6850`, `0x4efa30`, `0x12ff0`, `0x39b730`, `0x39be10`, `0x39b9d0`, `0x39bd50`, `0x39c0c0`, `0x399700`, `0x39ab70`, `0x399970`, `0x39ad70`, `0x39ae70`, `0x39b0d0`, `0x39b240`, `0x39b3d0`, `0x3994e0`, `0x3995b0`, `0x39a260`, `0x39a5f0`, `0x39a190`, `0x39a0d0`, `0x39a010`, `0x399e10`, `0x929cf0`, `0x92a090`, `0x92a1f0`, `0x92a3d0`, `0x92a610`, `0x92a730`, `0x92a960`, `0x92ade0`, `0x2a5bb0`, `0x34e9f0`, `0x3a5b60`, `0x3a5bb0`, `0x3a6300`, `0x3a8070`, `0x3a8180`, `0x3a23e0`, `0x3a24c0`, `0x3a1fe0`, `0x34a000`, `0x34a760`, `0x3acef0`, `0x3ad130`, `0x3add00`, `0x379ab0`, `0x379d10`, `0x37a710`, `0x34af50`, `0xba2d10`, `0xba2b50`, `0x3482a0`, `0x3a5d50`, `0x87a920`, `0x359a50`, `0x357d40`, `0x358090`. Call sites and short stretches were read in assembly at `0x39b7d3`, `0x3a6122`, `0x22da9e`, `0xa0d5`, `0xae90`, `0x34a2be`, `0x3a5db5`, `0x35c3cd`, `0x35d6da`. The stores' tables of functions (vtables) are filled by relocations; their entries were read out of the relocation records of `eboot_etexec.elf`. Floats were read straight from that file.
- **Port source, read-only:** `D:\Recomps\lbp3-wt\res` (`fix/coop-online` at `54c538f`), `runtime/src/core/sync_probe.cpp`.
- **Labels:**
  - **known**: read in this session in the decompiler, the disassembly or the port's source, at the place given. "Checked in assembly" is said where the instructions themselves were read.
  - **observed**: seen in a log or a file on disk; file and line or time given.
  - **inferred**: reasoned from known or observed facts; the basis is given.
  - **not found**: looked for and not settled.
- **Logs and files:** the two-instance runs in `D:\Recomps\lbp3-wt\coop\A\` (host, Kistopawa) and `B\` (guest, Kistopawa2), `stderr_<run>.txt`; and the two instances' cache files `user\temp0\hdd1\CUSA00473_00\output\temp_orbis`, of which only the entry table was read. No game was started and the online server was not contacted. The `m1` files of 16 November were not used.
- **No log has two joins that overlap, a leave inside a join's block, or a line from the resource transfer manager.** Everything below about overlapping joins is from the code.

## (1) What owns the snapshot's resources, and when they go

**The temporary cache `temp_orbis`, a file. An entry goes only when the ring's write position reaches it while nobody reads it, or when a reader finds its bytes don't match its hash. Known from the code; the file's state is observed.**

### Where a snapshot is written

- **known** The handler of REQUEST_GAME_STATE, `0x348e50`, makes the snapshot with three calls of `0x23cc40` (`0x349224`, `0x349246`, `0x349286`) and one of `0x22da40`, which ends in the same function (`0x22daad`). All four pass 0 for the fifth and sixth arguments (the fourth call checked in assembly, `0x22da9e..0x22daad`).
- **known** `0x23cc40` serialises the object into a buffer (`0x7bcef0`) and, with those arguments, hands the buffer to `0x220db0`, which calls the "put" function of the store at `eboot+0x12cc2d8`. It then writes the descriptor it returns: the hash of the bytes and the type. The buffer is freed at the end. The resource object itself keeps no copy of the serialised bytes for sending.
- **known** The handler does not look at what `0x23cc40` returns. A put that fails (`0x6b`) leaves that descriptor all zero in the hash message.
- **known** The stores are set up in `0x21ed20`. The one at `eboot+0x12cc2d8` is made by `0x224450("temp_orbis", "output/", 0x20000000, 0x1000, clean)`: a file of 512 MB with a table of 4,096 entries. Its neighbours are `intro_farc_cache` (`eboot+0x12cc2c0`), `Streaming_orbis` (`eboot+0x12cc2e8`, 128 MB), `StreamingLevelFarcs_orbis` (`eboot+0x12cc2f0`) and `fsb_orbis` (`eboot+0x12cc2f8`).
- **known** The labels resource of the second pass takes the same road: `0x3a5e30` calls `0x23cc40` with the same zeros (`0x3a613e..0x3a6146`, checked in assembly).
- **known** So does every resource a game has finished fetching from another player: `0x92a960` puts the bytes into the same store when the hash matches (`0x92abd7`).
- **observed** Both test instances have the file, 536,870,912 bytes each (`A\…\output\temp_orbis`, `B\…\output\temp_orbis`).

### The cache's rules

The store's functions, from its table at `eboot+0x10708e0`: find `0x2239b0`, open `0x223ab0`, close `0x223cd0`, delete `0xb511a0`, size `0xb512d0`, put `0xb513b0`. An entry is 0x28 bytes: a magic number `0xf0feb0bb`, the offset in the file, the size, a sequence number, a count of readers at `+0x10`, and the 20-byte hash.

- **known** **Put** (`0xb513b0`): if an entry with that hash is already there, it returns "done" and writes nothing. Otherwise it asks `0xb50e90` for a place, starting at the position after the last write (`+0x274`) and going back to the start of the data when the end is reached; writes the bytes there; and fills in the entry. It refuses bytes as large as the whole data area.
- **known** **Finding a place** (`0xb50e90`): every entry whose bytes overlap the place wanted is dropped, **unless its count of readers is not 0; then the place is moved to just after that entry** and the search starts again. For the table slot it takes a free one, or else the unread entry with the lowest sequence number.
- **known** **Find** adds one to the entry's count of readers. **Open** opens the cache file once more and seeks to the entry; **close** takes one off the count. A find whose open fails takes it off again (`0x223c30`, `0x223bf3`).
- **known** **Delete** (`0xb511a0`) clears the entry's magic number whatever its count. Its two callers found are the whole-file readers `0x220a10` and `0x21fbf0`, which call it when the bytes they read back don't hash to the entry's name. It is called through the table, so other callers can't be ruled out by a search.
- **known** The table is cleared only in `0x223530`, at start-up, when the file's size or the number in `output/_version` is wrong or the game was told to clean it. `0x223530` has one caller, the constructor.
- **inferred** So an entry lives until 512 MB of later entries have been written past it, or until 4,096 newer entries exist, and not even then while someone is reading it. Basis: the three functions above.
- **observed** How far that is in practice. Host's file: 184 entries, sequence numbers 1 to 184, 479,699 bytes in all, the largest 29,252. Guest's file: 359 entries, sequence 1 to 359, 948,053 bytes, the largest 29,594. Every sequence number ever given out is still there in both, so nothing has been evicted since the files were made on 13 November. 157 hashes are in both files. (The entry tables as they stood on 16 November; the readers' counts on disk mean nothing, they are zeroed at start-up.)
- **inferred** That also bounds the Pod's state from above: no resource either instance has ever cached, its own snapshots included, was larger than 30 KB. Which entries are snapshots was not worked out. Basis: the tables.

### What the events do

- **known** **A second REQUEST_GAME_STATE.** `0x348e50` either makes no snapshot at all (when no frame was stepped since the last one, `+0x258` equal to `[eboot+0x130d934]`; it then only notes who asked) or makes a new one by the same four puts. Neither touches the earlier entries. A world that has not changed serialises to the same bytes or to others; the first case writes nothing, the second adds entries. Which it is was not tested.
- **known** **A player leaving.** Remove-player `0x34e9f0` ends with `0x399e10` (`0x34ee6a`), which, for the player who left: clears that player out of each of the eight download places (`0x92a610`); frees every one of the 32 sending places whose asker it was (`0x39bd50`, which closes the file handle and so gives back the reader's count); and drops it from the list of players to skip. No cache entry is deleted.
- **inferred** **A rejoin** is a leave and a join: the host's sending places for the old connection are freed at the removal, and the new request is handled like any other. Basis: the two points above. It holds when the host's game does remove the player before the new request; see (2), "The one silence found".
- **inferred** **A level change** can't start on the host during a block: its game is inside the message handler until the unblock, and the main loop is not running ([What stops the host's simulation when a guest starts to join?](netcode-join-start-stall.md)). Outside a block it loads resources; nothing read here makes a load delete entries. The handler of CHANGE_LEVEL (`0x350110`) was not read.
- **known** The resource objects the handler holds while it makes the snapshot are released at its end (`0x23d540` on a count of 0). That frees the object in memory, not the cache entry.

## (2) What the host does with a second request while a block is on

**The host thread passes it on at once; every game leaves it unread until its own handler has returned. Known for the rule; the run of two overlapping joins is inferred.**

### The reading rule

- **known** A game takes session messages in `0x3ad130`, called from the slot update `0x3acef0` with a mode. For each slot it calls `0x3add00` with a class: class 4 when `mode & 1`, class 6 when `mode & 2`, class 8 when `mode & 4`.
- **known** `0x3add00` asks `0x379ab0` for the next message of that class, and `0x379d10` walks the connection's stream of received messages and takes the first whose class matches, **leaving the others where they are**.
- **known, checked in assembly** The main loop reads with mode 7 (`0xa0d5`, `0xae90`): all three classes. The wait for the unblock, `0x34a000`, reads with mode 4 (`0x34a2be`): **class 8 only**. The wait for the labels, `0x3a5d50`, reads with mode 2 (`0x3a5db5`): class 6 only.
- **known** The host thread sends the forwarded REQUEST_GAME_STATE with class 4 (`0x3a5bb0`, the last argument of `0x3a23e0`), the GAME_STATE_HASH with class 4 (`0x3a6300`, `0x3a24c0` with 4) and UNBLOCK_LOADING with class 8 (`0x3a8180`, `0x3a24c0` with 8).
- **known** The joining guest reads no session message at all while it fetches: `0x236550` pumps the sockets and the resource transfer through the presenter, not the slot update ([Can a game take part in a join and never report the game state loaded?](netcode-never-reports-loaded.md)).
- **inferred** So from the moment a game takes a hash until it has left that handler (the fetch, the wait for the unblock, the labels pass), a request or a hash for another join stays unread in its stream. It is taken on the first pass of the main loop afterwards. Nothing is refused. Basis: the four points above.
- **known** One path does handle a request inside the wait. A request that `0x348e50` put off because `0x878cd0` was true sits on the list at `+0x68` of the game object. `0x3482a0` hands that list back to `0x348e50` once `0x878cd0` is false, and the network update `0x34a760` calls `0x3482a0` in every mode, the wait's mode 4 included (before its test of the mode). `0x34af50` itself ends that state at its top (`0x878d20`). So a request taken and put off before a hash's handler began is handled on the first pass of that handler's wait: the game saves a snapshot (or notes the asker), and the host's game sends a second hash while it still waits for the first unblock.
- **inferred** That changes nothing for the first guest: the nested call only puts more entries into the cache, and the second hash waits unread in every other game. Basis: (1) and the reading rule.
- **not found** Whether `0x878cd0` can be true on a game that is about to take a hash, and so whether this path ever runs. The state it tests is still unread ([What stops the host's simulation when a guest starts to join?](netcode-join-start-stall.md)).

### The host thread

- **known** `0x3a5b60` has one test, that this PC is the host (`0x39e750`). It raises the asker's slot to 8, clears the byte at `+0x46`, and `0x3a5bb0` sends the request to every slot at 8 or more. It does not look at whether an exchange is open.
- **known** `0x3a6300` makes its entries under a key of one descriptor of the message (`+0x40` to `+0x5b`) and the number at `+0xb0`. That number is the host game's own count of hash messages (`+0x338` of the game object, copied to `+0x260` in `0x348e50`), so two exchanges never share a key, even with the same snapshot. `0x3a8180` sends the unblock per key, when every entry of that key has reported.
- **known** A slot at 8 when a hash passes is raised to 9 and gets an entry and the hash (`0x3a6403..`), whoever the hash names as the asker.

### Two joins that overlap

Guest 1 asks first, guest 2 while the block for guest 1 is on. All of it **inferred** from the rules above; no log.

- **Guest 2's request reaches the host thread after the first hash has passed** (the usual case; the window before it is about 65 ms). Guest 2 goes to 8. The request is forwarded to every PC. Guest 2's own game is in its main loop and takes it: it saves its own world and stops (`+0x45` clear). Every other game leaves it unread. Guest 1 finishes its fetch and reports; the host unblocks the PCs of the first key, which does not include guest 2 (the unblock goes to slots at 9 or more). Each game returns to its main loop and takes the queued request; the host's game answers with the second hash; the second exchange runs like the first, with guest 1 now one of the PCs already in.
- **Guest 2's request reaches the host thread before the first hash has passed.** Guest 2 is at 8 when the first hash passes, so it is raised to 9 and takes part in the first exchange: it gets the hash, fetches the first snapshot from the host as a second asker, reports and waits. The host's game, still in its main loop, may take guest 2's request before its own first hash comes back to it, and then sends a second hash at once. Each game takes that second hash after the first exchange. Guest 2 loads again, since the second hash names it (the test for keeping the world needs a hash for someone else, `0x34af50` at its top).
- **known** Two guests fetching the same snapshot at once are served side by side: a sending place belongs to one hash and one asker (`0x39b0d0`), each with its own handle on the cache file.
- **known** A game that gets a hash for someone else naming the state it loaded last keeps its world, reports and waits (`0x34af50`: the asker is not this PC, `+0x310` equals the message's `+0xa8`, and the world's descriptor matches). So a second exchange on an unchanged snapshot costs the PCs already in no fetch and no rebuild.
- **not found** Whether the host's game, on a second request with no step in between, takes the "no new snapshot" branch. The test compares `+0x258` with `[eboot+0x130d934]`. A game that is not the host stores that very number in `+0x258` when it saves, so for it the test is "nothing since". The host's game overwrites `+0x258` with another counter, `[eboot+0x130d938]` (`0x3a1fe0`; raised by one in `0x35c2a0`, `0x35d6e7`), which is the state number it sends, and `[eboot+0x130d934]` is set from a message's field in `0x359a50` and from the hash message's `+0xa8` in `0x34af50`. Whether the two counters are equal on a host that has not stepped was not traced; `0x35c2a0` was not read.
- **not found** Whether anything is stepped between a request answered from an old snapshot and its hash. In that branch the byte at `+0x45` is not cleared, so a game that is running goes on until the hash reaches it, about two frames on the host.
- **inferred** While the first block is on, guest 2 waits at state 8 on the host's table and in its own. Those states have timeouts (30 s × 4 on the host's table, 60 s × 4 in the guest's, [How long can a joining guest that hangs keep everyone from stepping?](netcode-hung-join-blocks-all.md)). So a first join that hangs for two minutes gets the second joiner removed, and the hung one stays. Basis: that ticket's tables and the queueing above.

### The one silence found

- **known** On the host, a request for the bytes (kind 2, `0x39b0d0`) and a request for the size (kind 0, `0x39ae70`) both begin by looking through the 32 sending places for one in use with **the same hash and the same asker**. If there is one, the function returns: **no place is taken, nothing is sent, nothing is said to the asker.**
- **known** A sending place is in use from the kind 2 request until its last byte has gone (`0x39c0c0`) or its asker is removed (`0x399e10`). A place opened for a size request is given back in the same call (`0x39b9d0`, `0x39bc7e`).
- **inferred** So a guest that asks the host again for a resource the host is still sending it gets silence, and its download place waits the 600 s. Pieces of the old transfer that still arrive are thrown away by a new download place whose size is not yet known (`0x92a960`: the piece must end inside the size), yet each one restarts the 600 s. Basis: the two points above and the stamp at the top of `0x92a960`.
- **not found** A way for a guest to ask twice in an honest join. A download place asks a player once and asks again only after that player timed out, said "busy" or "I don't have it", or left. A rejoin frees the host's places at the removal. It would take a guest that drops its fetch and starts it again while the host has not removed it; no such path was read on the guest.
- **inferred** This is the same shape as the rejoin without a spawn, in the other manager: "a 0x12 whose player and id match an object already there is ignored" (`sync_probe.cpp:243`, the other lane's finding). Whether the two share a cause was not looked at. Basis: the comment and `0x39b0d0`.

## (3) What a guest still fetching the first snapshot gets

**Its bytes, as before. The three bad endings were followed anyway. Known from the code; none is in a log.**

- **known** The host opens the resource for sending in `0x39be10` through `0x220790`, which asks each store in turn to find and open it. For the temporary cache that is a find (the reader's count goes up) and an open (a handle of its own on the file). The handle is kept in the sending place (`+0x18`) and closed when the place is given back.
- **inferred** So from the kind 2 request to the last piece the entry can't be evicted, whatever else is written. Basis: (1), "The cache's rules".
- **known** The size answer (`0x39b9d0`, through `0x220cb0`) reads the entry's size and takes no reader's count. Between the size answer and the kind 2 request, and before the guest has asked at all, the entry is unprotected.
- **inferred** Evicting it in that gap would take 512 MB of other puts, or 4,096 of them, within the round trip or two of the gap. A join makes four. Basis: the cache's rules and the observed file.
- **known** **If the entry were gone before the open:** `0x39be10` answers kind 4, "I don't have it", when no store opens the hash, and `0x39b9d0` does the same for the size. That is the clean failure of the earlier finding: asked again up to eight times, then the server, then the guest leaves.
- **inferred** **If the bytes were overwritten under an open transfer** (only after a delete for a hash mismatch, which ignores the count): the pieces go out at full length with the wrong bytes, the guest's check of the whole against the hash fails (error 7, `0x92a960`), and the fetch fails cleanly. Basis: `0xb511a0`, `0xb50e90`, `0x39b730`.
- **known** **Empty pieces** come only from a read that gives nothing: `0x39b730` sends what `0x21ff10` returned, and `0x12ff0` turns a read error into 0 bytes. Then the place never advances and every pass sends another empty piece, each of which restarts the guest's 600 s (the earlier finding).
- **not found** Anything that would make a read of the cache file fail in the port. The file is a plain file in the instance's own folder.
- **known** **Silence from the sender's own code** is confined to the other two sources. In `0x39b730` the source is the handle's file number (`+0x20` of the sending place): `-3` reads through `0x9bdc0`, `-2` through `0x911f10`, anything else is a file. `-3` is the `Streaming_orbis` store (its find `0x8b6d60` and its put `0x8b6cd0` go through the same code, "rubbery_fart.cpp" in its lock's name; where the `-3` is written was not read). `-2` is an archive that `0x2269d0` loaded whole into a 16 MB area (`0x911cd0`, "squeaky_fart.cpp"), looked up by offset; `0x225010` gives it out. A failed read from either drops the message and sends nothing. The snapshot is in neither.
- **known** The sender also sends nothing when it can't open a message to the asker (`0x37a710` false at the top of `0x39b730`): no connection object with an address for that player. The place stays taken until the asker is removed.

## (4) Three or more players: the second player asked with kind 2

**It is untimed only until its first piece. A leave clears it; whether the fetch is then picked up again depends on who is left. Known from the code; no log.**

### How a download place asks

A download place (0x108 bytes, eight of them from `eboot+0x1315b18` + 0xb8) holds up to four players to ask, 0x38 bytes each from `+0x1c`: the online ID, the range asked of it (`+0x24`, `+0x28`), the stamp (`+0x30`) and a "has not got it" byte (`+0x34`). The place's size is at `+0x8` and the bytes received in order at `+0x100`.

- **known** `0x39a5f0` fills the four from the session's connection list, every player but this one, sorted by a value that carries a random number and two flags for the players in the slot roles 0 and 1. The comparator (`0xbb0530`) was not read, so whether the host comes first is **not found**.
- **known** `0x929cf0(place, player)` picks whom to ask: the player named, if it is listed, not marked and not being skipped; else the first listed player with no range yet, not marked and not being skipped. With the size unknown it sends kind 0 and **stamps** that player. With the size known it writes the range, from the bytes received so far to the end, and sends kind 2 **without stamping**.
- **known** The size answer stamps its sender and calls `0x929cf0` with that same player (`0x92a730`). So in the ordinary case kind 2 goes to a player that was stamped twice a moment before.
- **known** Every piece stamps its sender (`0x92a960`, at the top). So an unstamped player is untimed only until its first piece.
- **known** Kind 2 goes to an unstamped player only when the size is known and `0x929cf0` has to pick someone else. That happens after the player who gave the size: timed out (`0x92a3d0`, its entry wiped, skipped for 1 s); said "busy" (kind 4 with reason 1, `0x92ade0`, its entry wiped, skipped for 10 s); said "I don't have it" (reason 0, marked); or left (`0x92a610`).

### When that player leaves or goes quiet

- **known** A leaving player's entry is wiped in every download place (`0x399e10`, `0x92a610`). **Only if its stamp was armed** does `0x92a610` call `0x929cf0` to ask the next player.
- **known** Each update of the manager then runs `0x92a3d0` on the place. It counts the players still worth asking (`0x92a090`: listed, not marked, not being skipped). With none, `0x92a1f0` puts the request back on the peers' queue, or hands it to HTTP after eight goes. With one or more and no timeout in this pass, it does nothing.
- **inferred** So when an unstamped player leaves:
  - with nobody else listed (three players: both others have dropped out), the request is put back and ends as the earlier finding says;
  - with another player still listed (four or more players), that player is never asked and nothing times anything. The fetch is stranded for as long as the session lasts.
  Basis: the two points above. Never run.
- **inferred** An unstamped player that stays connected and sends nothing holds the place without limit: `0x92a3d0` skips entries whose stamp is not armed. That needs a sender that goes quiet, for which see (2) and (3). Basis: the test against -1.0 (`eboot+0xeeaa10`) in `0x92a3d0`.

### What this means for the snapshot

- **inferred** For the joining guest to end up asking the host with kind 2 untimed, a player other than the host must have been asked first, must have had the resource (it gave the size), and must then have dropped out of the transfer. Basis: the rules above.
- **not found** Whether a player already in the game has the host's snapshot under the host's hashes. Each PC writes its own snapshot of the same world in `0x348e50`; it has the host's hashes only if both serialise to the same bytes. If it does not, it answers "I don't have it" to the size request, the guest marks it, and the host is asked with kind 0 and stamped: the ordinary, timed road.
- **inferred** In the worst case found for three players, the guest is fetching from the other guest, that guest leaves, and the host is asked for the rest untimed; the host then sends its first piece within a second and is timed from there. Basis: the allowance of at least 4,096 bytes a second ([What makes the game netcode cut its send budget, and does the peer link trigger it?](netcode-send-budget-cuts.md)).

## (5) What the existing logs show

**Nothing about the snapshot's transfer. The lines the ticket points at belong to the other manager.**

- **known** The `xfer out n=… in n=…` part of a `[slots]` line is read from `eboot+0x12cc4e8`: its outgoing records and incoming objects (`sync_probe.cpp:241..280`, `fix/coop-online` at `54c538f`). The snapshot travels through the manager at `eboot+0x1315b18`. No probe line reads that one.
- **observed** Three runs have the part: rt1, rt2 and rt4, on both sides (14, 18 and 20 lines on the host; 20, 41 and 41 on the guest). None of the rj, rw, rx or rm runs has it.
- **observed** At the first join of each, the records drain within seconds: `B\stderr_rt2.txt:2349` (`xfer out n=1 6:st1 in n=1 Kistopawa2#6`, 18:54:09), `:2366` (`n=1 7:st1 in n=0`), `:2406` (`xfer out n=0 in n=0`, 18:54:13).
- **observed** After the rejoin of rt2, the guest's outgoing record never drains: `B\stderr_rt2.txt:4652` (`xfer out n=1 10:st1 in n=0`, 18:55:41) to `:5050` (`xfer out n=1 58:st1 in n=0`, 18:56:27), the record's number rising by two or three from line to line, to the end of the log. On the host an incoming object shows up four times and is gone again on the next line (`A\stderr_rt2.txt:4482` `in n=1 Kistopawa2#10`, `:4571` `#20`, `:4702` `#37`, `:4811` `#52`). rt4 is the same: `B\stderr_rt4.txt:4670` (`10:st1`, 19:05:26) to `:5067` (`59:st1`, 19:06:12), with `A\stderr_rt4.txt:4515`, `:4585`, `:4702`, `:4811`.
- **observed** In rt1 the rejoin's record drains: `B\stderr_rt1.txt:4619` (`10:st1`), `:4639` (`11:st1`), `:4665` (`n=0`, 23:42:50).
- **inferred** That is the rejoin without a spawn ([rejoin_no_spawn.md](../../.scratch/lbp3-port/coop/rejoin_no_spawn.md)): a transfer the guest announces again every couple of seconds. It comes after the unblock and is not the snapshot. Basis: [How long can a joining guest that hangs keep everyone from stepping?](netcode-hung-join-blocks-all.md), (3).
- **not found** Any run with a rejoin or a leave inside a join's block. The rejoins in the logs begin after the earlier connection has ended.

## What a later pass needs to settle

Not found, listed in the sections above: which branch the host's game takes on a second request with no step in between, and whether anything is stepped before its hash; whether a non-host player holds the host's snapshot under the same hashes; the order in which a download place lists its players; a guest-side path that asks the host twice for one resource; whether a put-off request is ever handled inside a wait (the state `0x878cd0` tests); the handler of CHANGE_LEVEL; other callers of the cache's delete; what a hash message with an all-zero descriptor does to the guest; any log of two overlapping joins, of a leave inside a block, or of the resource transfer manager's places.

Corrections to earlier findings:

- [Can a game take part in a join and never report the game state loaded?](netcode-never-reports-loaded.md), "Who moves a resource": "The snapshot is made in the host's memory at the join". It is serialised in memory and written at once to the temporary cache file; the host sends it from that file.
- The same, "Asking a peer" and the first candidate of (1): "for the two kept in memory … a failed read drops the message", with the snapshot given as the case. Those two sources are the streaming store and an archive loaded into memory. The snapshot is the third source, the file, where a failed read gives empty pieces, not silence. The candidate "its entry can be freed or replaced while it is being sent" does not hold: an entry with a reader is stepped over.
- The same, "The one timer" and the fourth candidate of (1): "whether a player's leaving clears its entry in a download place was not read". It does, and the fetch is re-issued only if that player's stamp was armed. And a second player asked with kind 2 is stamped by its first piece.
- [What stops the host's simulation when a guest starts to join?](netcode-join-start-stall.md), (3): "whether a 0x0a or 0x0b taken there is handled nested or put off was not read". Neither is taken there: the wait reads class 8 only. A request taken earlier and put off (`0x878cd0`) is the exception; it is handled inside the wait, through `0x3482a0`.
- The same, step 3: the host thread's key is "one descriptor and the state number". The number is the host game's count of hash messages (`+0xb0` of the message), not the state number at `+0xa8`.

Questions this raises for the map:

1. **The resource transfer manager is not in any log line.** The `[slots]` part that looks like it reads the other manager. The watchdog of [Can a game take part in a join and never report the game state loaded?](netcode-never-reports-loaded.md) and any check of this ticket need the 32 sending places (hash, asker, bytes sent of length) and the eight download places (bytes received of size, who is asked, stamps) of `eboot+0x1315b18` printed. That belongs in [Which log lines does a session need to measure itself?](../../.scratch/netcode/issues/19-session-log-lines.md).
2. **Can a guest ask the host twice for a resource the host is still sending it?** The host answers the second request with nothing. If any path on the guest drops a fetch and restarts it while staying connected, that is a 600 s hang with a cause. It is a static read of who cancels a download place.
3. **Do two overlapping joins run one after the other as the code says?** The three-player run of [What does a third player do to a session?](../../.scratch/netcode/issues/18-third-player.md), with `LBP3_SYNC_PROBE=2` on all three: the second guest's `0a` taken by the host's game only after its `67` of the first join, then a second `0b` and `67`. And the second block's length, which says whether the PCs already in kept their world.
4. **With four players, can a fetch be stranded by two leaves in a row?** By the code, yes. The rig has three instances, so this stays as read unless a fourth is added.
5. **Is the rejoin without a spawn the same rule in the other manager?** Both managers ignore a request that matches a transfer they still hold. That lane owns the question.
