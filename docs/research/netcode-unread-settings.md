# The game's own packet delay and drop settings, and the other unread settings

Research for the [online co-op netcode map](../../.scratch/netcode/map.md), ticket 16. Written 2026-11-16. It builds on [What the game netcode tolerates](netcode-game-netcode-limits.md) (ticket 03, branch `research/netcode-game-netcode-limits`), called "the earlier findings" below.

## Summary

- **The packet delay and drop settings do nothing in this build.** The file's keys are parsed into the settings object, but no reader of the four fields or of the loopback switch was found by any of six routes, and the send and receive paths have no branch that drops or holds a datagram (known). So the simulator was compiled out and only its settings keys are left (inferred).
- **So the game's own settings cannot stand in for ticket 05.** The delay, jitter and hold settings have to be added to the peer link in full.
- **`EnableKeepAlive` is the HTTP keep-alive switch**, not the netcode keepalive. **`EnablePrediction` has no reader.**
- **The 9 s test** widens whose room updates are accepted once the simulation has not stepped for 9 s (the 10 s kick limit minus 1 s).
- **The link level** has one reader found. It turns the level into a 0 to 3 quality figure per player, and those figures are sent on in game message type 0x7e.
- **Not found:** who shows that quality figure, and which of the three measures it uses.

## How this was read

- **Binary and project:** as in the earlier findings. `eboot.bin`, CUSA00473 1.28; addresses are module offsets, written `eboot+0x...`. The Ghidra project `D:\Recomps\lbp3-recompiled\analysis\ghidra\lbp3.gpr` was read from a private copy in a temp folder, headless, read-only, at low priority, with no analysis run.
- **Two passes.** A pass on 2026-11-15 was cut off before writing anything. Its decompiler and scan output (same project copy) was read in this pass. The functions the answers rest on were decompiled again on 2026-11-16 and gave the same text. Where a point still rests on the earlier output alone it is marked **(15 Nov output)**.
- **Labels:**
  - **known**: read in the decompiler or the disassembly at the address given. "Checked in assembly" is said where the instruction itself was read.
  - **inferred**: reasoned from known facts; the basis is given.
  - **not found**: looked for and not settled.
- **The port:** `D:\Recomps\lbp3-recompiled\runtime\src\hle\http_online.cpp`, read only.
- **LBPPC:** the read-only clone. It has no `.nws` file and no mention of the packet keys; its one mention of `network_settings.nws` is a comment in `net_resolver.cpp:68` about `CDNHostName`. Its copy of shadPS4's `net_error.h` was used to name one error number.
- The game was not launched and the online server was not contacted.

### The scans, and what each can and can't show

"No reader" is a claim of absence, so the routes checked are listed here.

1. **Every use of the settings pointer** `eboot+0x130d670` (84 references, Ghidra's list), each followed inside its function to the fields it touches. (15 Nov output, script `FieldScan.java`.) It misses a reader that gets the object some other way.
2. **A whole-program scan that does not depend on the pointer.** Most readers of a settings field first test the object's state byte `+0x45` (the earlier findings, (a)); the loader's read of `+0x5f` and the read of `+0x16e` at `eboot+0x7b4747` do not. The scan took every instruction with a memory operand `[reg+0x45]` (1,929 of them) and listed uses of `[same reg + 0x48, 0x4c, 0x50, 0x54, 0x5c, 0x5d, 0x8c, 0x94, 0x98, 0xb4, 0xbc]` in the next 14 instructions (929 hits). `+0x45` is the state byte of every resource type, so most hits are other types loading a pointer at `+0x50`. None is a float read at any listed offset, and none is a byte read at `+0x5c` or `+0x5d`. It misses a reader that skips the state test or sits more than 14 instructions after it.
3. **Every instruction in the program with displacement 0x5d** (23), and every byte access with displacement 0x5c (64).
4. **Every float or integer access at displacement 0x48 and 0x54 in `eboot+0x340000..0x420000`**, the range that holds the netcode.
5. **Every function with a float access at all four of 0x48, 0x4c, 0x50 and 0x54** (17 functions; 15 Nov output). None is among the functions that use the settings pointer. The one that also has byte reads at 0x5c and 0x5d, `eboot+0x6164b0`, was looked at in assembly: each of those reads is on a different object, reached through a different pointer of a game object (`[RSI+0x50]` then `+0x128` at `0x62327f`, `+0x150` at `0x62686c`, or a slot picked by a bit mask at `0x619ac6`, `0x61a163`, `0x627670`, `0x62842a`). It is a large accessor over game-object parts, not a settings reader.
6. **The send and receive functions themselves**, read for any branch that skips or defers a datagram.

## (a) The keys without a known reader

### Types and defaults

- **known** The key table `eboot+0xc51520` registers each key with one of a few helper functions. `eboot+0xceb5f0` is used for every `TIMEOUT_...` key, whose fields the constructor fills with floats, so it is the float helper. `eboot+0xceb2d0` is used for the on/off keys. `eboot+0xcdb5b0` and `0xcdb9c0` are used for whole numbers.
- **known** All four packet keys use the float helper: `ProbabilityOfPacketDelay` (`+0x48`, at `0xc51538`), `MinPacketDelayFrames` (`+0x4c`, `0xc5154b`), `MaxPacketDelayFrames` (`+0x50`, `0xc5155e`), `ProbabilityOfPacketDrop` (`+0x54`, `0xc51571`). So the two "frames" fields are floats too.
- **known** The constructor `eboot+0xb5e580` zeroes `+0x48..+0x57` and sets `+0x5c`, `+0x5d`, `+0x5e` and `+0x5f` to 1.

### Key by key

| Key | Field | Reader | What it does |
|---|---|---|---|
| `ProbabilityOfPacketDelay` | +0x48 float, default 0 | none | nothing; see (b) |
| `MinPacketDelayFrames` | +0x4c float, 0 | none | nothing |
| `MaxPacketDelayFrames` | +0x50 float, 0 | none | nothing |
| `ProbabilityOfPacketDrop` | +0x54 float, 0 | none | nothing |
| `EnableFakeConditionsForLoopback` | +0x5c on/off, on | none | nothing |
| `EnablePrediction` | +0x5d on/off, on | none | nothing |
| `EnableKeepAlive` | +0x5f on/off, on | loader `eboot+0x33df86` | HTTP keep-alive |
| `GameStateUpdateRateWithConsumers` | +0x168 float, 1.0 | `eboot+0x9f9d2` | an update rate, in one state |
| `TIMEOUT_WAIT_FOR_REQUEST_JOIN_FRIEND` | +0x8c float, 15 | none | nothing |
| `TIMEOUT_WAIT_FOR_ROOM_ID_TO_JOIN` | +0x94 float, 60 | none | nothing |
| `TIMEOUT_WAIT_FOR_GET_NUM_PLAYERS_ONLINE` | +0x98 float, 60 | none | nothing |
| `TIMEOUT_WAIT_FOR_DIVE_IN_PLAYERS` | +0xb4 float, 120 | none | nothing |
| `TIMEOUT_DIVE_IN_TOTAL` | +0xbc float, 120 | none | nothing |

**`EnableKeepAlive`**

- **known** The settings loader reads the byte at `eboot+0x33df86` and passes it to `eboot+0x21e70` (checked in assembly). That function stores it in byte `+4` of the object returned by `eboot+0x221e0`, which is the pointer at `eboot+0x1190ef0`.
- **known** That object is the game's HTTP client. `eboot+0x21e80` creates it right after `sceHttpInit` and the user-agent string `MM CHTTPClient LBP3 %s`.
- **known** The request function `eboot+0x222a0` passes byte `+4` as the last argument of `sceHttpCreateConnection`, which is that call's keep-alive flag.
- **known** `eboot+0x21e70` has one caller, the loader. So the netcode keepalive of the earlier findings (once a second, 10 s kick) has no switch in the settings file.

**`EnablePrediction`**

- **known** No reader. Of the 23 instructions in the program with displacement 0x5d, two belong to the settings class: the constructor's store (`eboot+0xb5e5df`) and the key table's address-of (`0xc5159c`). None lies in `eboot+0x200000..0x420000`.
- **known** The other 21 are in unrelated code: a PlayGo object (`0x4f366`, `0x4f36a`), indexed table reads (`0x6af45`, `0x6c5cc`, `0x6c61f`, `0x5c1d31`, `0x5c2dd4`, `0x5c3105`, `0x80f896`), an image colour-profile check (`0x9ae98f`), key tables of other classes that take the address of their own field (`0xc4e28d`, `0xc4f424`, `0xd009dd`, `0xd01398`, `0xd023cc`), a telemetry text formatter (`0xae3448`), and five single reads or writes on other objects (`0xc92c0`, `0xd1053`, `0x5c26b1`, `0xd01be9`, and `0x619ae6` in the accessor of route 5).
- **inferred** Prediction is always on in this build, bounded by `MinPredictedFrames` and `MaxPredictedFrames` as the earlier findings describe. Basis: the switch is unread, and the predicted-frame count in `eboot+0x251240` depends on other fields only.

**`GameStateUpdateRateWithConsumers`**

- **known** `eboot+0x9f940` reads it at `0x9f9d2` when the dword at `[its object]+0x8bd4` is 0xd2, and reads `GameStateUpdateRate` (`+0x164`, at `0x9f9b8`) when that dword is 0xc8. Either value is stored at `+0x8c58` of the same object (checked in assembly). The earlier table's "none seen" was a miss of the earlier scan's 16-instruction window.
- **not found** What states 0xc8 and 0xd2 are, and what consumes `+0x8c58`.

**The five timeouts**

- **known** No reader by routes 1 and 2 above. (Route 1 is 15 Nov output.)
- **inferred** They are leftovers of flows this build no longer has. Basis: the neighbouring keys are all read by the join flow `eboot+0x3606f0`, and these five are not.

### Two rows the earlier table lacked

- **known** `AllowGameRendCameraSplit` is `+0x5e`, on/off, default on (string `eboot+0xeab616`, registered at `0xc515e3`). It is read at `eboot+0x253e2c` in `0x2514d0`; when it is off the function calls `eboot+0x1a9560` (checked in assembly). It is a camera setting, not netcode.
- **known** The field `+0x6c` (default 320) is read by `eboot+0x9650f0`. (15 Nov output.) **not found**: its key name, or whether it has a key at all. The script that lists the key table printed no row for it, and it is known to miss rows (it missed `+0x80`).

## (b) The packet delay and drop fields

### Nothing reads them

- **known** No instruction reads `+0x48`, `+0x4c`, `+0x50`, `+0x54` or `+0x5c` of the settings object, by routes 1 and 2 above.
- **known** In `eboot+0x340000..0x420000` no instruction before `0x3d2683` touches displacement 0x48 or 0x54 as a float. The connection and stream code of the earlier findings lies in `0x375000..0x381000`. The float uses from `0x3d2683` on belong to other objects (`0x3d1aa0`, `0x3e21b0`, `0x3e4660` and later).
- **known** The 64 byte accesses at displacement 0x5c include the constructor's store (`eboot+0xb5e5d4`) and no read of the settings object by routes 1 and 2. In `eboot+0x300000..0x420000` the only two are indexed table accesses (`0x3b454e`, `0x3b816e`).

### The send path has no place for them

- **known** A flush builds datagrams in `eboot+0x37de30`: it copies queued messages into a buffer and calls `eboot+0x37e610` when the next message would not fit, and once more at the end. No test stands between a filled buffer and the call.
- **known** `eboot+0x37e610` appends the five-byte trailer and calls `sendto` (at the socket, flag 0x80). There is no branch before the call. `0x37e610` has two callers, both in `0x37de30` (`0x37dfb4`, `0x37e0de`).
- **known** The only queue on the path is at connection `+0x2208` (entries of 0x81c bytes, count `+0x2210`, capacity `+0x2214`). `0x37e610` puts a datagram there only when `sendto` fails with errno 0xa3 (163).
- **known** `eboot+0x378b60` later hands each queued entry to the local connection object whose address equals the entry's destination (`0x378ea0..0x378f18`). So the queue delivers a datagram inside the same game, without the network.
- **inferred** This is the loopback path the key `EnableFakeConditionsForLoopback` refers to, used when the socket refuses the send. Basis: the hand-over above. shadPS4's `net_error.h` (LBPPC's copy, line 98) names 163 `ORBIS_NET_EINACTIVEDISABLED`; when the PS4 returns it was not looked up.
- **known** An entry holds the socket, the address, the flag, the length and the bytes. It has no time and no frame number, so nothing can hold it for a set time.

### The receive path has no place for them

- **known** `eboot+0x378b60` reads the socket with `recvfrom` until it is empty and gives each datagram to `eboot+0x3795b0` for the matching connection. Then it empties each connection's loopback queue, oldest first, into the same function. Neither loop has a branch that skips a datagram for any reason but an unknown sender (decompiler, and assembly of `0x378d70..0x378fa0`).
- **known** `eboot+0x3795b0` handles every datagram it is given: trailer, arrival ring, then each message into the stream code.

### No random numbers

- **known** `rand` has one caller in `eboot+0x330000..0x410000`: `0x3df300` in `eboot+0x3df120`, which fills a table of floats and is not on the send or receive path. `srand` has none there. A drop or delay "probability" needs a random draw; the connection code makes none through `rand`.
- **not found** Whether the game has a random generator of its own that the scan would not see by name. The absence of any reader of the fields makes this moot.

### The second packet layer

- **known** The strings `NetUnreliableOutManager::Send delay on low priority packet (age %d)` and its medium and high variants are used by `eboot+0x3c2980`. That function flags and reports a queued packet once it is older than 100 ticks (low priority) or 10 ticks (medium). The limits are constants in the code.
- **known** This layer (`eboot+0x3c0e80`, `0x3c22f0`, `0x3c2980`) is called only from `eboot+0x42000..0x47000`, and none of those functions uses the settings pointer.
- **not found** What that layer carries and whether co-op uses it. It is not the connection code of the earlier findings, and its "delay" is ageing in a send queue, not a simulator.

### The answers

- **known** The keys are parsed and stored, no reader was found by the six routes, and the datagram path has no drop or hold branch.
- **inferred** Live or compiled out: compiled out. Basis: the line above. A reader that takes none of the six routes is not ruled out.
- **not found** (and moot) Where they acted, above or below the reliability layer, and whether per direction: no code is left to read. The names say "packet", the loopback switch points at the datagram level, and the only place a datagram can be dropped or held below the reliability layer is `eboot+0x37e610` or `0x378b60`.
- **inferred** Units: the two probabilities are fractions and the two delays are counts of frames, all stored as floats. Basis: the key names and the float helper. With no reader the range can't be checked.

## (c) Setting them from the port

- **known** `FixUpReply` (`runtime/src/hle/http_online.cpp:251`) runs on every reply (`:569`) and has three branches: a 410 for `/farc_hashes` or `/t_conf`, an empty 200 for `/npdata`, and a 401 for `/match`. None touches `network_settings.nws`. Today the file reaches the game as the server sent it.
- **known** `AnswerSigned` (`:225`) replaces a reply's body and signs it the way the server does. A fourth branch for a path ending in `/network_settings.nws` with status 200 could append `Key value` lines to the body and call it.
- **known** The port has no per-instance switch for this. Its per-instance settings are environment variables (`LBP3_ONLINE_LOG` at `:361`, the `LBP3_P2P_TEST_...` settings of the link), so a variable naming extra lines would be the matching form.
- **known** For the packet fields this would change nothing, per (b).
- **inferred** For the keys that do have readers, the earlier findings' rule stands: any key that changes when a game steps must be the same for both players. Whether both players need the same packet values has no answer, since neither would act on them.
- **not found** Whether the game checks the signature of the `.nws` reply in particular. `AnswerSigned` signs it either way.

## (d) The 9 s test in `eboot+0x356540`

### What the function is

- **known** `eboot+0x356540` takes the session object (`[eboot+0x130d5e0]`) and a player's online ID, and returns 1 or 0.
- **known** It has two callers.
  - The room-update handler `eboot+0x375550` calls it first, with the update's sender (`0x375583`, checked in assembly). On 1 it walks the update's player entries without applying them and returns. On 0 it goes on to apply the update.
  - `eboot+0x3563a0` (called from `eboot+0xba96ea`) does the same with a message's sender: on 1 it does nothing; on 0 it passes the message to `eboot+0x34d960`.
- **inferred** So it answers "ignore what this player sent?" for session-level messages. 1 is ignore, 0 is accept.

### The test itself

- **known** The line is at `eboot+0x356610..0x35664f` (checked in assembly). The function loads two slot pointers from the slot table (`+0x1800` and `+0x1810`) and the time now in seconds. Then:
  - if both pointers are null, or
  - if now minus the float at `eboot+0x130d90c` is more than 10.0 + (-1.0) = 9 s,

  it looks the sender up in the slot table (`eboot+0x3ad430`, which walks the slots in use and compares online IDs). If the sender has a slot, the function returns 0: accept.
- **known** Otherwise it goes on to narrower tests: the sender's slot when the first slot's state is below 4; whether the sender is one of the four players named by the table's role pointers (`+0x1800`, `+0x1808`, `+0x1810`, `+0x1818`); and a stored ID that is valid for 30 s (`eboot+0xe75778`). Failing those it returns 1.
- **not found** Two parts of the function were not worked out: its first branch, taken in session states 1 and 4 (`+0x24c`), and its last test at `eboot+0x356790`, which compares the two slot pointers.
- **known** The 10.0 is the same writable float as the kick limit, `eboot+0x1061c4c`. The -1.0 is at `eboot+0xe75774`.

### What the time stamp is

- **known** `eboot+0x130d90c` is written in two places.
  - The main loop writes the time now at `eboot+0xb12c`, right after it has taken a per-frame message (`eboot+0x3ae640`), applied it (`eboot+0x359a50`) and counted the step. (15 Nov output, decompiler; the store was read in assembly.)
  - `eboot+0x254450` writes it at `0x255370` (checked in assembly). That function's one caller is the main loop (`0xb2aa`).
- **known** The main loop also reads it at `eboot+0xb4c9`: when no step ran this pass and more than 3.0 s (`eboot+0xe49ca4`) have passed since the stamp, it raises a notice with text ID 0x88440cf0.
- **inferred** The stamp is "when the simulation last stepped". Basis: the first writer. What `0x254450` is was not read; if it is the step path used without a host, the meaning is the same.

### What it guards

- **inferred** While the simulation is stepping, session messages are accepted only from players in the named roles. Once it has not stepped for 9 s, they are accepted from anyone who holds a slot. Basis: the branch above.
- **inferred** The 9 s is the 10 s kick limit less one second, so the wider rule comes in just before the game would remove a silent host, in time for a host change announced by another player to be taken. Basis: the shared constant and the role tests; the host-change flow itself was not read.
- **known** A consequence for the port: patching the float at `eboot+0x1061c4c` to lengthen the kick limit (the earlier findings, "Values in the game's memory") moves this threshold with it.

## (e) Who reads the link level

- **known** The link level is the first dword of the block at connection `+0xc8`. `eboot+0x3788a0` passes that block to `eboot+0x37d330` (`ADD R14,0xc8` at `0x378b3b`), and `0x37d330` writes the level there every 5 s.
- **known** The level is not from RTT alone. `0x37d330` first looks at the send budget (block `+0x50`, connection `+0x118`):
  - budget under 15,360 bytes a second: level 1 if RTT is under 200 ms, else 0;
  - budget under 1,048,576: level 2 under 200 ms, 1 under 400 ms, else 0;
  - budget at or above that: level 2 under 400 ms, else 1.

  The game's own connection always gets level 2.
- **known** One reader was found: `eboot+0x382fd0`, at `0x383063`. It finds a player's connection (`eboot+0x379bf0`) and writes a quality figure to `+0x28` of a per-player entry:
  - no connection: 0;
  - level 0, 1, 2: quality 1, 2, 3;
  - the player is ourselves: 3.
- **known** The same function has two other measures, chosen by the first dword of the table it is given. Measure 1 uses the game's own RTT (connection `+0xf0`): under 100 ms gives 3, under 200 ms gives 2, else 1. Measure 2 uses the budget cap from the reported bandwidth (connection `+0x11c`): over 128,000 gives 3, over 32,000 gives 2, else 1. Measures 3 and 4 go to `eboot+0x383280` and `0x3834b0`, which were not read.
- **known** The network update `eboot+0x34a760` runs this once per update for every entry of the table at `[its object+0xb0]` (`0x34ac36`). `eboot+0x382840` keeps one entry per player in the room.
- **known** Under one condition (`eboot+0x3672a0` applied to the ID at `eboot+0x130d8b0`; what it tests was not read), `eboot+0x382aa0` then collects every entry's quality figure and sends it as game message type 0x7e, once every 30 updates. It sends to one slot, looked up with `eboot+0x3adb20` from the number at slot table `+0x201c`. Whose slot that is was not read.
- **known** Code outside the connection manager gets a connection object through `eboot+0x379bf0`. It has nine calling functions (`0x379ab0`, `0x37a430`, `0x37a710`, `0x37b550`, `0x37bb80`, `0x381790`, `0x382fd0`, `0x383280`, `0x3834b0`), and in `eboot+0x379000..0x384000` the only dword read at offset 0xc8 is the one in `0x382fd0`.
- **known** The other uses of the block's address are the connection's set-up and reset (`0x37adc9`, `0x37c73e`) and the delay-trend function `eboot+0x37ec30`, which does not read the block's first dword.
- **not found** A reader outside those. The scan for offset 0xc8 covered `eboot+0x340000..0x420000` only, and three dword reads in it are on objects that were not identified (`0x351c9d`, `0x3d610b`, `0x3ef37d`).
- **inferred** The level feeds a connection-quality display (signal bars per player) and nothing in the netcode. Basis: the one reader found turns it into a 0 to 3 figure per player that is sent on in a game message; none of the pacing, resend or kick functions of the earlier findings reads it.
- **not found** Which measure is in use (who sets the table's first dword), who reads `+0x28` on the receiving side, and what is drawn.

## Can the game's own settings stand in for ticket 05?

**No.** Ticket 05 stands in full: the delay, the jitter and the one-off hold all have to be added to the peer link.

- **known** No reader of the four packet fields or the loopback switch was found by six routes, and the send and receive functions have no drop or delay branch.
- **inferred** The simulator is compiled out of the whole build. A reader that takes none of the six routes is not ruled out.
- Even a working simulator would not have covered ticket 05. Its settings are a chance of delay and a delay in frames. Ticket 05 needs a set delay per direction and a hold of one direction for a set number of seconds at a chosen moment.

## What a later pass could settle

1. **The quality figure.** Who sets the measure, who reads the 0x7e message, and what the player sees. Only worth doing if the port wants to show or log the game's own view of a link.
2. **The loopback path in the port.** Does the port's `sendto` ever fail with errno 163, so that the game's own queue (connection `+0x2208`) carries the host's datagrams to itself, or do they always go out and come back through `recvfrom`? It decides whether the link's test settings can touch the host's own per-frame messages.
3. **The key of field `+0x6c`** (default 320), and the states 0xc8 and 0xd2 that pick between the two `GameStateUpdateRate` keys.
