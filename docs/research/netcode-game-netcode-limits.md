# What the game netcode tolerates, and which of its limits can be reached

Research for the [online co-op netcode map](../../.scratch/netcode/map.md), ticket 03. Written 2026-11-15.

## Summary

- **The game netcode has its own reliability layer.** Each connection carries two ordered, acknowledged streams and one unacknowledged stream. Lost messages are sent again after twice the game's own RTT average. Datagrams may arrive out of order.
- **The kick is a keepalive test.** A peer is removed when no keepalive from them has been *delivered* for 10 s. The keepalive is sent once a second on an ordered stream. The "20 s test" in the earlier notes is a 20 **ms** grace after a peer's goodbye.
- **The server sets no timeouts.** The probe's "mode 4, timeouts 60/60/30/60/50" are the game's own defaults and a state byte of the settings object. Refresh's default file has none of the timeout keys.
- **The RTT the port reports is fetched and thrown away.** The bandwidth it reports caps the game's send budget.
- **Simulation data goes through the host.** Each game takes the per-frame message from one player only. Connections are made to every player in the room.
- **Not found: which lost datagram is the fatal one.** The session survived 13 induced single drops. Both fatal cases followed a guest→host loss nobody induced. The logs hold sizes, not contents.

## How this was read

- **Binary:** `eboot.bin`, CUSA00473 1.28. Addresses are module offsets, written `eboot+0x...`.
- **Ghidra:** the existing analysed project `D:\Recomps\lbp3-recompiled\analysis\ghidra\lbp3.gpr`, copied to a temp folder and opened read-only at idle priority. No analysis or re-analysis was run. The existing analysis was usable.
- **Labels:**
  - **known**: read in this session, in the decompiler or the disassembly, at the address given. "Checked in assembly" is said where the instruction itself was read.
  - **observed**: seen in a log, with the file and time.
  - **inferred**: reasoned from known or observed facts; the basis is given.
  - **not found**: looked for and not settled.
- **Logs:** `D:\Recomps\lbp3-wt\coop\A\` (host, Kistopawa) and `B\` (guest, Kistopawa2), files `stderr_d3.txt`, `stderr_t0.txt`, `stderr_t2.txt`.
- **Refresh:** `github.com/LittleBigRefresh/Refresh` at commit `b10862eb792cb1cf4545c93f69504ec945e16813` (2026-11-15).
- **LBPPC:** the read-only clone at `951fb8d`.
- The live `network_settings.nws` on LBPPC's server was not fetched.

## (a) The server's network settings

### Where the file goes

- **known** The settings are one object of resource type 0x1a. Its constructor is `eboot+0xb5e580`. It calls the resource base constructor `eboot+0x23ca00` and then writes a default into every field.
- **known** The pointer to the live object is at `eboot+0x130d670`. The function that names `/network_settings.nws` (`eboot+0x33cc60`, string at `eboot+0xe75c4c`, used at `0x33dbce`) writes that pointer (`0x33cd96`, `0x33cf7b`, `0x33dda3`, `0x33dee2`). One more writer is at `0x360176`.
- **known** The file is text, one `Key value` pair per line (Refresh's source, below). The function `eboot+0xc51520` pairs each key name with a field of the object. It is the complete list of keys this eboot knows.
- **inferred** The loader fills the object through that table. Basis: the names are the file's keys and the offsets are the fields the readers use. The call path from the loader to the table was not traced.
- **known** A reader uses a field only when the pointer is set and the object's byte `+0x45` is 4 or 5. Otherwise it uses a constant in the code. Example: `eboot+0x3ac2a0` (the test is `(byte & 0xfe) == 4`).
- **known** Byte `+0x45` is set to 4 by the base constructor (`mov byte [rbx+0x45], 4` at `eboot+0x23ca6a`, checked in assembly). It is a state of the resource object. It is not a value from the server.

### What the probe showed

- **known** The `[slots]` probe prints byte `+0x45` as "mode" and the five floats at `+0xe8..+0xf8` as "timeouts" (`runtime/src/core/sync_probe.cpp:87-99`).
- **known** Those five fields are `TIMEOUT_WAIT_FOR_HOST_TO_GET_RESOURCE_LIST`, `..._HOST_TO_SAVE_GAME_STATE`, `..._HOST_TO_ADD_US`, `TIMEOUT_WAIT_FOR_UPDATE` and `TIMEOUT_WAIT_FOR_REQUEST_JOIN`. Their constructor defaults are 60, 60, 30, 60 and 50 (`eboot+0xb5e725..0xb5e74d`, checked in assembly).
- **observed** The probe printed `settings mode 4, timeouts 60 60 30 60 50` on LBPPC's server (`A\stderr_t0.txt`, 60 lines).
- **inferred** So the server sent none of those five keys: the values equal the game's defaults. Refresh's built-in file has no timeout key (below), and LBPPC's repository holds no `.nws` file. The live file was not read, so a server-side file with the same numbers can't be ruled out.

### What Refresh sends

- **known** Refresh answers `network_settings.nws` with the file of that name in its working directory if one exists. Otherwise it sends 15 lines (`Refresh.Interfaces.Game/Endpoints/Handshake/MetadataEndpoints.cs:66-110`): `AllowOnlineCreate true`, `ShowErrorNumbers true`, `AllowModeratedLevels false`, `AllowModeratedPoppetItems false`, `ShowLevelBoos true`, `CDNHostName` and `TelemetryServer` (both the server's config URL), `OverheatingThresholdDisallowMidgameJoin 2.0`, `EnableCommunityDecorations true`, `EnablePlayedFilter true`, `EnableDiveIn` (from the server's config), `EnableHackChecks false`, `DisableDLCPublishCheck true`, `AlexDB true`, `SECONDS_BETWEEN_PINS_AWARDED_UPLOADS 300.0`.
- **known** `AllowOnlineCreate` and `AlexDB` are not in this eboot at all (no such string). The other 13 keys are in the table.
- **known** None of the 15 lines touches the netcode fields below.

### The fields, their defaults and their readers

Offsets are into the settings object. Defaults are from the constructor. "Reader" is a function that loads the field through `eboot+0x130d670`; it was found by listing every use of that pointer and the 16 instructions after it. A field with no reader in that window is marked "none seen".

**Simulation pacing and prediction**

| Key | Offset | Default | Reader |
|---|---|---|---|
| `EnablePrediction` | +0x5d | true | none seen |
| `MinPredictedFrames` | +0x60 | 0 | `eboot+0x251240` |
| `MaxPredictedFrames` | +0x64 | 20 | `eboot+0x251240` |
| `NumberOfFramesPredictionAllowedForNonLocalPlayer` | +0x58 | 100 | `eboot+0x35a9f0` (at `0x35ad92`; 1000 without settings) |
| `FramesBeforeAgressiveCatchup` | +0x68 | 30 | main loop `eboot+0x9d70` (`0xa6c2`), `0x35bb70`, `0x35bce0`, `0x35c2a0` (half of it), `0x3ae1d0` |
| `MaxCatchupFrames` | +0x144 | 3 | main loop (`0xa7e8`) |
| `MaxLagBeforeShowLoading` | +0x148 | 23 | main loop (`0xa7ef`) |
| `MinLagBeforeHideLoading` | +0x14c | 2 | main loop (`0xa7e2`) |
| `EnableCatchUpScreenNew` | +0x141 | true | main loop (`0xa7d7`) |
| `LagImprovementInflectionPoint` | +0x150 | -1.0 | main loop (`0xa7f5`) |
| `FlickerThreshold` | +0x154 | 2.0 | main loop (`0xa7fd`) |
| `PredictionPadSides` / `Top` / `Bottom` | +0x70 / +0x74 / +0x78 | 200 each | `eboot+0x1a88f0` |
| `GameStateUpdateRate` | +0x164 | 30.0 | `eboot+0x9f940` |
| `GameStateUpdateRateWithConsumers` | +0x168 | 1.0 | none seen |

**Test hooks and the keepalive switch**

| Key | Offset | Default | Reader |
|---|---|---|---|
| `ProbabilityOfPacketDelay` | +0x48 | 0 | none seen |
| `MinPacketDelayFrames` / `MaxPacketDelayFrames` | +0x4c / +0x50 | 0 / 0 | none seen |
| `ProbabilityOfPacketDrop` | +0x54 | 0 | none seen |
| `EnableFakeConditionsForLoopback` | +0x5c | true | none seen |
| `EnableKeepAlive` | +0x5f | true | the loader, `eboot+0x33df7f` |

**Timeouts, in seconds**

| Key (`TIMEOUT_...` unless shown) | Offset | Default | Reader |
|---|---|---|---|
| `WAIT_FOR_JOIN_RESPONSE_FROM_PREV_PARTY_HOST` | +0x80 | 50 | flow `eboot+0x3606f0` |
| `WAIT_FOR_CHANGE_LEVEL_PARTY_HOST` | +0x84 | 30 | flow |
| `WAIT_FOR_CHANGE_LEVEL_PARTY_MEMBER` | +0x88 | 45 | flow |
| `WAIT_FOR_REQUEST_JOIN_FRIEND` | +0x8c | 15 | none seen |
| `WAIT_FOR_CONNECTION_FROM_HOST` | +0x90 | 30 | flow (two places) |
| `WAIT_FOR_ROOM_ID_TO_JOIN` | +0x94 | 60 | none seen |
| `WAIT_FOR_GET_NUM_PLAYERS_ONLINE` | +0x98 | 60 | none seen |
| `WAIT_FOR_SIGNALLING_CONNECTIONS` | +0x9c | 120 | flow |
| `WAIT_FOR_PARTY_DATA` | +0xa0 | 60 | flow |
| `TIME_TO_WAIT_FOR_LEAVE_MESSAGE_TO_COME_BACK` | +0xa4 | 20 | flow |
| `TIME_TO_WAIT_FOR_FOLLOWING_REQUESTS_TO_ARRIVE` | +0xa8 | 30 | flow |
| `WAIT_FOR_FINISHED_MIGRATING_HOST` | +0xac | 30 | flow |
| `WAIT_FOR_PARTY_LEADER_FINISH_JOINING` | +0xb0 | 30 | flow |
| `WAIT_FOR_DIVE_IN_PLAYERS` | +0xb4 | 120 | none seen |
| `WAIT_FOR_FIND_BEST_ROOM` | +0xb8 | 30 | flow |
| `DIVE_IN_TOTAL` | +0xbc | 120 | none seen |
| `DIVE_IN_TOTAL_LBP3` | +0xc0 | 90 | flow, `0x36aa90` |
| `DIVE_IN_WAIT_TO_RETRY` | +0xc4 | 10 | flow |
| `DIVE_IN_HOST_TOTAL` | +0xc8 | 120 | `0x36a6e0` |
| `WAIT_FOR_SOCKET_CONNECTION` | +0xcc | 120 | `eboot+0x3abee0` |
| `WAIT_FOR_REQUEST_RESOURCE_MESSAGE` | +0xd0 | 120 | `0x3abee0` |
| `WAIT_FOR_LOCAL_CLIENT_TO_GET_RESOURCE_LIST` | +0xd4 | 120 | `0x3abee0` |
| `WAIT_FOR_CLIENT_TO_LOAD_RESOURCES` | +0xd8 | 120 | `0x3abee0` |
| `WAIT_FOR_LOCAL_CLIENT_TO_SAVE_GAME_STATE` | +0xdc | 30 | `0x3abee0` |
| `WAIT_FOR_ADD_PLAYERS_TO_TAKE` | +0xe0 | 30 | `0x3abee0` |
| `WAIT_FOR_UPDATE_FROM_CLIENT` | +0xe4 | 90 | `0x3abee0` |
| `WAIT_FOR_HOST_TO_GET_RESOURCE_LIST` | +0xe8 | 60 | `eboot+0x3ac2a0` (slot state 6) |
| `WAIT_FOR_HOST_TO_SAVE_GAME_STATE` | +0xec | 60 | `0x3ac2a0` (state 8) |
| `WAIT_FOR_HOST_TO_ADD_US` | +0xf0 | 30 | `0x3ac2a0` (state 10) |
| `WAIT_FOR_UPDATE` | +0xf4 | 60 | `0x3ac2a0` (state 11) |
| `WAIT_FOR_REQUEST_JOIN` | +0xf8 | 50 | `0x3ac2a0` (states 4, 5), `0x3abee0` |
| `WAIT_FOR_AUTOJOIN_PRESENCE` | +0xfc | 60 | `0x369ec0` |
| `WAIT_FOR_AUTOJOIN_CONNECTION` | +0x100 | 120 | `0x369ec0` |

- **known** The two per-player tables multiply their timeout by 4.0 before comparing (`eboot+0xe7a504` in `0x3ac2a0`, `eboot+0xe7a4d8` in `0x3abee0`). A player fails the test only after five calls in a row over the limit.
- **known** With the settings object live, slot state 11 lasts `TIMEOUT_WAIT_FOR_UPDATE` × 4 = 240 s, not the 60 s that the code's fallback constant (15 at `eboot+0xe7a4f4`) gives. The comment in `sync_probe.cpp:17` quotes the fallback values.
- **known** The timer of state 10 or 11 restarts each time a per-frame message from the host is taken: `eboot+0x3ae640` calls `eboot+0x3ac270` with 10 and with 11, and that function sets the slot's state time to now when the slot is in the state named.
- **observed** The probe agrees: the host's slot shows `state 11 for 0 s` throughout (`A\stderr_d3.txt`).
- The remaining keys (`ShowErrorNumbers`, `AllowModerated...`, `ShowLevelBoos`, `CDNHostName`, `TelemetryServer`, `OverheatingThresholdDisallowMidgameJoin`, `AllowVoIPRecordingPlayback`, `ClosedDemo2014...`, `EnableCommunityDecorations`, `EnablePlayedFilter`, `LevelResourceWarningThreshold`, `DisableDLCPublishCheck`, `EnableDiveIn`, `EnableHackChecks`, `EnableAdvancedDLCChecks`, `UseSaveOperationPriorities`, `SECONDS_BETWEEN_PINS_AWARDED_UPLOADS`) are not netcode.
- **not found** What `EnableKeepAlive`, `EnablePrediction` and the four packet delay and drop fields do. Their readers were not located. They may be read through another pointer.

## (b) The 10 s and 20 s limits

### The test

The test is `eboot+0x37b3f0`, one call per connection object. It was checked in assembly. In order:

1. **known** If the connection is the game's own (its online ID equals ours), it passes.
2. **known** If field `+0x168` is set and more than **20 milliseconds** have passed since, the test fails. The time goes through `eboot+0x1b300`, which multiplies nanoseconds by 1000.0 (`eboot+0xe4af48`) and by 1e-9, giving milliseconds. The limit is the float 20.0 at `eboot+0xe75844`.
3. **known** Otherwise, if the connection has an address, the test fails when both of these hold:
   - more than 10 s since field `+0xc0`;
   - field `+0x160` is zero, or more than 10 s have passed since it.
   Both use `eboot+0x1b2a0` (seconds) and the float 10.0 at `eboot+0x1061c4c`.

### What each field is

- **known** The writers of `+0xc0` that were found all belong to setup or reset: `eboot+0x36697f` (the address arrives), `eboot+0x3664d8` (a room member is added) and `eboot+0x37adbb` (reset). They came from a scan of `eboot+0x300000..0x400000` for every store of the clock into offset 0xc0; the two other hits (`0x375a11`, `0x375faa`) write a different object.
- **inferred** So nothing received updates `+0xc0`, and the first half of step 3 only says "this connection is at least 10 s old". The earlier note's "nothing has been accepted for 10 s" is not what the code tests. Basis: the scan above; a writer outside its range or pattern is not ruled out.
- **known** `+0x160` is the time the last **keepalive** from that player was read. It is set in `eboot+0x37d0e0` (at `0x37d2d3`) when a connection-control message with code 0 is taken from the connection. It also starts at the setup time.
- **known** `+0x168` is the time a control message with code 1 was read (`0x37d258`). Code 1 is the **goodbye**: the only sender is remove-player (`eboot+0x3666f0`, at `0x3667cd`), which sends it, flushes both ordered streams and then calls `sceNpSignalingTerminateConnection` (`0x378186`).
- **known** Code 2 is a congestion notice (see (c)).

### What resets the 10 s timer

- **known** Each game sends a keepalive (code 0) to every connection once per 1.0 s (`eboot+0xe75810`), from the per-connection update `eboot+0x3788a0` (at `0x378a17`).
- **known** The keepalive is a class 0 message. Class 0 travels on one of the two ordered, acknowledged streams (see (c)). It is not sent on its own datagram, and it is not the unacknowledged kind.
- **known** So the timer is reset only when a keepalive comes out of the ordered stream. Datagrams that arrive but whose messages are held behind a missing one do not reset it.
- **inferred** That is how the host can read the other player's datagrams for 10 s and still kick them: the kick needs only that no keepalive came out of the ordered stream. Basis: the two facts above, and the D3 log, where datagrams arrive until the end (`A\stderr_d3.txt`, the `[p2p-ring]` lines).
- The test can't tell two cases apart: the stream stopped delivering, or the other game stopped sending keepalives. Which one happened in D3 and T0 is not known.

### Who acts on it

- **known** The network update `eboot+0x3604a0` loops over every connection object (`0x360520..0x36055c`). For each it calls `0x3788a0`; if that reports a failed test it calls remove-player `eboot+0x34e9f0` at once.
- **observed** The D3 kick's stack holds `eboot+0x37b4b6`, `0x360555`, `0x34ee60` and `0x37818b` (`A\stderr_d3.txt`, the lines after `the game ends connection 1`, 18:43:17.316). That is this path, ending in the second 10 s compare.
- **known** The flush `eboot+0x378fe0` also runs the test and sends nothing to a connection that fails it.

### Are the values constants?

- **known** 10.0 at `eboot+0x1061c4c` lies in the writable data segment (it starts at `eboot+0x1060000`). Ghidra lists only reads of it: `0x37b494`, `0x37b4b6` and `0x35661f`. No code writes it and no settings key maps to it.
- **known** 20.0 at `eboot+0xe75844` and the keepalive period 1.0 at `eboot+0xe75810` are in the read-only segment.
- **known** The third reader of the 10.0, `eboot+0x356540` (at `0x35661f`), adds -1.0 (`eboot+0xe75774`) and compares 9 s against a session time stamp at `eboot+0x130d90c`.
- **not found** What that 9 s test guards. It is called from the room-update handler `eboot+0x375550`.

### Corrections to the earlier notes

- "A second test uses 20 s (`0xe75844`)" ([shadnet_design.md](../../.scratch/lbp3-port/best/shadnet_design.md), "The drop"): it is 20 ms after a goodbye.
- "The T0 kick was a host→guest loss, the 20-s test": the T0 log shows a guest→host datagram missing 9.7 s before the kick (see (c)).

## (c) Loss and order

### The layers, from the socket up

**The datagram** (`eboot+0x3795b0`, called for each `recvfrom` result by `eboot+0x378b60`):

- **known** The game reads its socket with `recvfrom`, flag 0x80, into 1600 bytes, until nothing is left. It gives each datagram to the connection object whose address and port match the sender. A datagram from an unknown address is dropped.
- **known** The last five bytes are a trailer:
  - two bytes: a time stamp, fed to a delay-trend list (`eboot+0x37ec30`);
  - two bytes: a datagram number;
  - one byte: a flag bit and a 7-bit figure stored at `+0x2240` and `+0x2244`.
- **known** The datagram number is used only to mark a ring of "arrived" bits (`+0x2220..+0x2230`). A number below the newest one marks its old place; a number above it marks the skipped places as missing. Nothing is refused on it.
- **known** Everything before the trailer is a run of messages. A datagram with no message is five bytes long.
- **observed** Five-byte datagrams appear in the D3 log (149 of them from the host in its last 10 s).

**The message** (header coded and decoded by `eboot+0x37fff0`):

- **known** Each message names one of three streams: ordered stream 0, ordered stream 1, or the unacknowledged one.
- **known** A message on an ordered stream carries a 16-bit message number, a "sent again" mark, and acknowledgements for the other direction: groups of a 16-bit number plus a 5-bit mask for the next five (`eboot+0x37f910`).
- **known** The payload of an ordered message is a slice of a byte stream, cut at 300 bytes or the path's size limit (`eboot+0x37e150`). Framed game messages are laid into that stream with a 3-byte frame header naming a class (`eboot+0x37c280`).

**Receiving on an ordered stream** (`eboot+0x37ede0`, `0x37f270`):

- **known** Every received message number is put on a to-acknowledge list, duplicates included.
- **known** A number seen before is dropped. The history holds up to 1000 entries and 240 s (`eboot+0xe75814`).
- **known** A new message is kept in a list (`+0x268`). The stream hands on only the message whose number is the last delivered plus one, then the next, and so on. A later message waits in the list until the gap is filled.

**Sending on an ordered stream** (`eboot+0x37dab0`, `0x37de30`):

- **known** A sent message stays in an unacknowledged list (`+0x180`) until an acknowledgement names it (`eboot+0x37f040`).
- **known** It is sent again when more than 2.0 × RTT has passed since its last send. The 2.0 is at `eboot+0x1061c50`. An added term at `eboot+0x130d95c` is zeroed memory with no writer found.
- **known** RTT here is the game's own: the time from a message's first send to its acknowledgement, averaged over ten half-second buckets (`eboot+0x37d330`). With no sample in those buckets it counts as 0.8 s (`eboot+0xe7586c`).
- **known** A message may also go out up to three extra times before any timeout. The count comes from the measured loss share (thresholds 0.5 %, 2 % and 8 %) and the spare send budget (`eboot+0x37f440`).

**The unacknowledged stream:**

- **known** Its messages are appended to a buffer as they arrive (`eboot+0x37ede0`, the branch for stream 2). No number, no acknowledgement, no resend, no ordering.

**Which class uses which stream** (pairs of send and receive entries at `eboot+0xe1d340`):

- **known** Classes 3 to 8 use the two ordered streams. Class 2 uses the unacknowledged stream. Classes 0 and 1 use either ordered stream; the sender picks.
- **known** Class 0 is connection control (keepalive, goodbye, congestion notice). Class 1 is sent and read by the session code around `eboot+0x39b4f0..0x39be10` and `0x399970`. Class 2 is sent by `eboot+0x3f8920` and read by `0x3f9510`.
- **known** Game messages are sent by `eboot+0x3adbf0`: class 7 for types 0x66 and 0x67, class 5 for types 0x12 to 0x15 and 0x68, class 3 for the rest. The receive side reads classes 4, 6 and 8 (`eboot+0x3ad130`) and hands each to the handler for its type (`eboot+0x3add00`).
- **known** The per-frame message, type 0x0f, is read as class 4 (`eboot+0x3ae1d0`). It is on an ordered stream.
- **not found** How the odd send classes pair with the even receive classes, and what class 2 carries.

**The send budget:**

- **known** Each connection has a budget in bytes per second (`+0x118`). It limits how much one flush repeats: once budget ÷ 30 − 50 bytes are in the flush, further messages that were already sent wait (`eboot+0x37dab0`; 30.0 at `eboot+0xe75890`). A message never sent before always goes.
- **known** A congestion notice from the peer (code 2) cuts the budget to 75 %, at most every 0.25 s, down to 10,240 bytes per second (`eboot+0x37d0e0`).
- **known** A game sends that notice when its delay-trend test on the trailer time stamps fires, at most every 0.1 s (`eboot+0x3795b0` at `0x37983b`).
- **not found** What exactly the delay-trend test measures (`eboot+0x37ec30`, `0x380ce0`). The decompiler output for it was unreliable.

### The answers

- **known** Does the game acknowledge and resend? Yes, on the two ordered streams.
- **known** Does it reorder? Yes: by message number within each ordered stream.
- **known** Would it accept datagrams out of order or with gaps? Yes, at the datagram level. A gap stalls an ordered stream until the missing message arrives again. A gap on the unacknowledged stream is simply lost.
- **inferred** One lost datagram should cost one resend delay, about 2 × RTT. Basis: the resend rule above.
- **observed** That is what induced drops did in T0 (the gap-free link off, one incoming datagram dropped every 1350). The session survived all 13 of them before the fatal loss: six guest→host drops on the host from 21:47:42 to 21:51:27, and seven host→guest drops on the guest from 21:47:40 to 21:51:54 (`A\stderr_t0.txt` and `B\stderr_t0.txt`, the `[p2p-test]` lines). Where a drop shows in the `[sim]` lines at all, it is one to three flips without a step and then a catch-up (for example the guest at 21:51:21 and 21:51:54).

### The fatal datagram

**not found.** What is established:

- **observed** D3: guest→host datagram 11605 went missing. The host kicked the guest 10.0 s later (`A\stderr_d3.txt`, `[p2p-ring]`, 18:43:17). No drop was induced in that run.
- **observed** T0: guest→host datagram 9226 went missing at 21:52:05.269 with no `[p2p-test]` line, so it was not an induced drop. The kick came at 21:52:14.991, 9.7 s later (`A\stderr_t0.txt` lines 4518 and 4604).
- **observed** In both runs each side's simulation then ran in 2 s cycles: about 30 flips with no step, then about 30 flips with two steps each (`[sim]` lines, both files).
- **observed** In D3 the host's datagrams changed within 100 ms of the loss. Before: 160 to 175 bytes, 30 a second. After: five-byte ones, 12 and 21-byte ones, and about 17 a second of 309 to 313 bytes. The guest's stayed near 100 bytes at 30 a second, with more 189-byte ones.
- **inferred** After those losses no keepalive from the guest came out of the host's ordered stream for 10 s. Either the stream stopped delivering or the guest stopped sending them. Basis: the kick test in (b).
- **inferred** A datagram of 309 to 313 bytes is a full 300-byte slice of an ordered stream plus headers and the trailer. So the host was sending backlog in full slices. Basis: the 300-byte cut in `eboot+0x37e150`.
- **not found** Why the stream stopped. The resend rule as read should have repaired a single loss, and it did so 13 times out of 13 when the drop was induced.
- **observed** Both fatal losses were guest→host. The six induced guest→host drops came exactly 45.000 s apart (at x.702 or x.703 s), because the guest sent a steady 30 datagrams a second. The keepalive is once a second and the sync check once per 60 frames, so those six may all have hit the same kind of datagram.
- **observed** The seven induced host→guest drops came about 44 s apart at varied points in the second (.939, .006, .272, .406, .640, .506, .273). All seven were survived. So host→guest loss at varied points was tested and held; guest→host loss at varied points was not tested.
- **not found** Which message the lost datagrams carried. The ring logs sizes and times only.

### What this means for the port's gap-free delivery (DATA3)

- **inferred** DATA3 hides loss and reordering from a layer that already handles both. Its value is that it avoids the unexplained fatal case. Basis: T1 and T2 held 10 minutes with drops.
- **inferred** A gap that DATA3 skips after 2 s reaches the game as a plain loss. Until the fatal case is understood, a skip must be counted as a possible kick.
- **inferred** While DATA3 holds later datagrams behind a gap, it also holds the keepalives and acknowledgements in them. A hold of several seconds eats into the 10 s.

## (d) Waiting

### What the code shows

- **known** Each game takes the per-frame message (type 0x0f) from **one** player: the slot at `+0x1808` of the slot table (`eboot+0x3ae640`). Per-frame messages from the slots at `+0x1800`, `+0x1810` and `+0x1820` are read and discarded when those are other players.
- **known** A taken per-frame message goes into the simulation's input queue (`eboot+0x359c50`).
- **known** The main loop (`eboot+0x9d70`, at `0xa68e`) counts each frame how many per-frame messages from that slot are waiting unread (`eboot+0x3adf50`). It keeps the last `FramesBeforeAgressiveCatchup` counts. It then reads `MaxCatchupFrames`, `MaxLagBeforeShowLoading`, `MinLagBeforeHideLoading`, `LagImprovementInflectionPoint` and `FlickerThreshold` (`0xa7d7..0xa7fd`).
- **known** The number of predicted frames is the largest count of queued input records of any player (`eboot+0x35da40`), limited to `MinPredictedFrames..MaxPredictedFrames` (`eboot+0x251240`, called from the main loop at `0xb7cc`). It is forced to 0 when a flag at `[eboot+0x130d5e8]+0x1a4` is clear.
- **known** The game asks `sceNpSignalingGetConnectionInfo` for bandwidth (code 2) and then RTT (code 1) in one place, `eboot+0x378200`, called every 5.0 s per connection from `eboot+0x37d330`.
  - The bandwidth, divided by 8, becomes the cap on that connection's send budget (`+0x11c`). If the call fails or returns 0 the cap is 16,384 bytes per second.
  - The RTT is written to a stack slot of `0x37d330` (`[rsp+0x1c]`). The function touches that slot twice: it zeroes it at `0x37d913` and passes its address at `0x37d971`. Nothing reads it (checked in assembly over the whole function).
- **known** The only other call asks for the peer's online ID (code 3, `eboot+0x377a3e`).
- **observed** The game makes those calls: 19 with code 1 and 20 with code 2 in the T0 host log.
- **known** The game's own RTT average (from acknowledgements) sets the resend time and a link level 0 to 2 (limits at 200 ms and 400 ms, `eboot+0x37d330`).
- **not found** Who reads the link level.
- **known** The bandwidth test (`sceNpBandwidthTestInitStart` and `GetStatus`) is driven by `eboot+0x90e060`, which stores two results and calls `eboot+0x3bc490`.
- **not found** Whether those results reach the netcode.

### What the logs show

- **observed** Normal play is one simulation step per flip on both sides (`[sim]` lines).
- **observed** At an RTT of about 350 ms through the relay (T2) the host still ran one step per flip, line after line (`A\stderr_t2.txt`, 22:11:12 to 22:11:20).
- **observed** After a single lost datagram the receiver has one to three flips with no step, then flips with two steps until it has caught up (T0, above).
- **observed** While the friend's game sent almost nothing for 10 s, the host's frames stayed at 33.3 ms ([rejoin_no_spawn.md](../../.scratch/lbp3-port/coop/rejoin_no_spawn.md), "What we know", item 2).

### The answers

- **inferred** The simulation waits. A game steps a frame when the per-frame message for it has come from the player in slot `+0x1808`; with none waiting it stalls, and with several waiting it runs extra steps. Basis: the reader and the backlog counter above, and the step patterns in T0.
- **observed** That player is the host. The probe prints the slot numbers at `+0x1800` and `+0x1808` first in its `roles` list. On the guest both are the host's slot (`roles 1 1`, with slot 1 Kistopawa; `B\stderr_d3.txt`, 18:42:49.940). On the host both are its own slot (`roles 0 0`; `A\stderr_d3.txt`, 18:36:45.920), so the host takes its own per-frame message, which reaches it over loopback.
- **inferred** The host does not wait for a guest's input. Basis: the host kept its frame time while a guest was silent (the rejoin note above). How the host builds the per-frame message was not read.
- **inferred** The delay between a player's input and its effect is not a setting. It is however long that player's input takes to come back inside the host's per-frame messages. The game hides it by predicting up to `MaxPredictedFrames` (20) frames. Basis: the predicted-frame count is a queue length, and no fixed delay value was found.
- **known** The delay does not follow the RTT the port reports through `sceNpSignalingGetConnectionInfo`. That value is unused.
- **known** The server settings bound it: `MinPredictedFrames`, `MaxPredictedFrames`, the catch-up fields and the loading-screen thresholds.
- **not found** What the game shows or does when the queue is longer than `MaxPredictedFrames` (20 frames is 667 ms at 30 steps a second), and what `NumberOfFramesPredictionAllowedForNonLocalPlayer` limits.

## (e) Three and four players

Nobody has run three players on this port. Everything here is from the code.

- **known** When a room update arrives (`eboot+0x375550`), the game goes through the room's four player entries. For each player who was not in the previous list it takes one of two arms, chosen by `eboot+0xe220`. That function is a thread test (is this the thread stored at `eboot+0x10cf5e8`), not a host test.
  - On that thread it calls `eboot+0x366450`, which sets up the connection object and adds a signaling entry (`eboot+0x377eb0`) unless the player is ourselves.
  - On another thread it queues a request of kind 2 for that player (`eboot+0x366040`).
- **inferred** The queued request ends in the same call. Basis: `0x366450` has other callers (`eboot+0x34a760`, the flow `0x3606f0`); the queue's consumer was not read.
- **known** The signaling update (`eboot+0x377b20`) calls `sceNpSignalingActivateConnection` for every entry in state 0.
- **known** A `PEER_ACTIVATED` event (10) adds an entry for that player in the same way (`eboot+0x3778d0`).
- **inferred** So connections are a **full mesh**: every game connects to every other player in the room, not only to the host. Basis: the loop covers all room members and neither arm tests for the host. It also fits `eboot+0x3ae640`, which reads and discards per-frame messages from players other than the host, so it expects them to arrive.
- **known** Every connection object gets the keepalive and the 10 s test (`eboot+0x3604a0` loops over all of them). The flush (`eboot+0x375f00`) also loops over all of them.
- **known** Simulation data is a **star**: each game uses the per-frame message of one player only (see (d)).
- **inferred** A guest-to-guest peer link that fails will make those two guests remove each other after 10 s, even while both still reach the host. Basis: the test is per connection and calls remove-player directly.
- **not found** What the session does after that, which messages a guest sends to another guest, and whether a guest's input goes to the host only.

## Limits and knobs reachable from the port's side

**1. The settings file, replaced locally.**
- The game fetches `network_settings.nws` over HTTP. The port already rewrites and signs replies in `FixUpReply` (`runtime/src/hle/http_online.cpp:251`).
- Reachable this way: every key in the tables in (a). The ones that bear on feel:
  - `MaxPredictedFrames` (20), `MinPredictedFrames` (0);
  - `FramesBeforeAgressiveCatchup` (30), `MaxCatchupFrames` (3);
  - `MaxLagBeforeShowLoading` (23), `MinLagBeforeHideLoading` (2);
  - the `TIMEOUT_...` values for joining and level changes.
- **inferred** Any key that changes when a game steps must be the same for every player, or the simulations may part. Basis: lockstep. Both players run the same build, so a local override applies to both.
- Not reachable this way: the 10 s kick, the 1 s keepalive, the 20 ms goodbye grace, the resend factor and the send budget's limits. None has a key.

**2. Values in the game's memory.** The port loads the image itself and already patches it (`runtime/src/core/patches.cpp`).
- `eboot+0x1061c4c`, float 10.0: the kick limit. It is in the writable data segment and only read. Also used by the 9 s test in `eboot+0x356540`.
- `eboot+0x1061c50`, float 2.0: the resend factor (× RTT).
- Read-only data, patchable before the game starts: 1.0 s keepalive period (`eboot+0xe75810`), 20 ms goodbye grace (`0xe75844`), the × 4 on the per-player timeouts (`0xe7a504`, `0xe7a4d8`), 0.8 s RTT with no sample (`0xe7586c`).
- **inferred** Raising the 10 s gives a stalled stream longer to recover. It does not repair one.

**3. Values the port reports through the PS4 calls.**
- **Bandwidth** (`sceNpSignalingGetConnectionInfo` code 2): caps the game's send budget, read every 5 s. The port's 10 Mbit/s gives a cap of 1,250,000 bytes per second. Zero or an error gives 16,384 (known). That small cap is probably behind "0 bits per second made LBP3 drop a connection that worked" in LBPPC's `np_signaling_state.cpp:404` (inferred; the link was not traced).
- **RTT** (code 1): no effect.
- **Connection status and events**: the address and port the game sends to; `DEAD` puts the entry in state 5 and the next update removes the player (`eboot+0x377b20`).

**4. The peer link's own behaviour.**
- The game repairs loss itself in about 2 × its RTT, and counts on its keepalive arriving within 10 s. How long the link holds datagrams back, and what it does when it gives up on one, decide whether those two hold.

## What a later pass needs to settle

1. **The fatal datagram.**
   - Log the bytes of game datagrams in the ring, or at least the 5-byte trailer and each message header. The layout in (c) is enough to decode them.
   - Drop single **guest→host** datagrams at a period that is not a multiple of 30, so the drops hit every kind of datagram. That direction at varied points is the untested case, and both fatal losses were in it.
   - Probe both games once a second, per connection object (the list is at `[eboot+0x130d638]+0x50`, count `+0x58`): `+0x160` (last keepalive), `+0x188` (unacknowledged count), `+0x270` (messages held out of order), `+0x50` and `+0x68` (last delivered numbers), `+0x118` (send budget), `+0xf0` (RTT in µs).
   - That tells whether the sender stopped resending, the receiver refused the resend, or the budget starved it.
2. **The congestion test.** Read `eboot+0x37ec30` and `0x380ce0` in assembly: what makes a game send code 2, and whether a burst from the port's link (many datagrams released at once after a gap) triggers it.
3. **The host's side of the per-frame message.** Read how it is built and sent (`eboot+0x3a24c0`, `0x3a4e00`, `0x3a5bb0`, `0x3eae60`), to confirm that the host never waits for a guest and to see what the host does with a late guest input.
4. **The unread settings.** Find the readers of `EnablePrediction`, `EnableKeepAlive` and the packet delay and drop fields. They may be the game's own loss and delay simulator, which would be a ready-made test rig.
5. **The 9 s test** in `eboot+0x356540` and the link level's readers.
6. **Three players.** One run with three test accounts settles the mesh, and what a failed guest-to-guest link does.
7. **The live settings file.** One look at what LBPPC's server really sends (the port logs the request; logging the reply body would do), to turn the "inferred" in (a) into "observed".
