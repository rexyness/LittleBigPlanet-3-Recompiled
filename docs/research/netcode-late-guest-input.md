# What the host does with a guest's late input

Research for the [online co-op netcode map](../../.scratch/netcode/map.md), ticket 17. Written 2026-11-16. It builds on [What the game netcode tolerates](netcode-game-netcode-limits.md) (ticket 03, branch `research/netcode-game-netcode-limits`) and corrects one of its inferences.

## Summary

- **The host does wait for a guest.** Ticket 03 inferred that it doesn't. The code says otherwise: the host makes no per-frame message while any player's PC has no input queued. It gives up waiting only once some player's queue has held two or more inputs for a whole window of `FramesBeforeAgressiveCatchup` (30) samples, about one second. While the host waits, nobody's simulation steps, the host's own included.
- **The wait is set off by gaps, not by RTT.** A steady delay never empties the guest's queue on the host. A gap does: a freeze on the guest's PC, a lost datagram being sent again, a burst held back by the link.
- **The host has no notion of "late".** An input that arrives is queued and used in order, one per frame. Nothing is dropped. When the host has given up waiting, it repeats the missing player's last input.
- **A guest's delay is hidden up to 20 frames.** The guest replays its own inputs that the host hasn't echoed yet, at most `MaxPredictedFrames` of them. Past that its own Sackboy answers late by the excess.
- **The catch-up loading screen is a start-of-game screen with the default values.** It comes up when 23 or more per-frame messages are waiting and the game object is new. In the middle of play the default `LagImprovementInflectionPoint` (-1.0) all but rules it out.
- **The death countdown is not a network timer.** It is a counter in the simulation that runs while the player's own game reports its Sackboy off its screen. No "time without input" test was found. What "away" is was not found.

## How this was read

- **Binary:** `eboot.bin`, CUSA00473 1.28. Addresses are module offsets, written `eboot+0x...`.
- **Ghidra:** a private copy of the analysed project `D:\Recomps\lbp3-recompiled\analysis\ghidra\lbp3.gpr`, opened read-only, headless, at low priority. No analysis was run. Most decompiler and disassembly dumps were made on 15 November by an earlier run of this ticket that was cut off; they were read and checked in this session, and more were made on the same copy. Constants were read straight from `eboot_etexec.elf`.
- **Labels:**
  - **known**: read in this session, in the decompiler or the disassembly, at the address given. "Checked in assembly" is said where the instructions themselves were read.
  - **observed**: seen in a log; here always quoted from the two earlier findings files.
  - **inferred**: reasoned from known or observed facts; the basis is given.
  - **not found**: looked for and not settled.
- **Names.** The message type names come from the game's own table (`eboot+0x1076e48`, one entry of 0x10 bytes per type): 0x0e is `E_MESSAGE_TYPE_INPUTS`, 0x0f is `E_MESSAGE_TYPE_COMBINED_INPUTS`, 0x37 is `E_MESSAGE_TYPE_ON_SCREEN_STATUS` (known). Ticket 03's "per-frame message" is COMBINED_INPUTS.
- **Addresses the ticket named.** None of them decides when a per-frame message is made. `eboot+0x3a24c0` is a send fan-out over the host's player slots, picked by message type; the sender of the built frame, `eboot+0x39e9f0`, calls it (at `0x39ed81`), and which of its arms the per-frame message takes was not read. `0x3a4e00` and `0x3a5bb0` send messages of types 4 and 10 from the joining flow. `0x3eae60` sends nothing; what it updates was not determined. The builder is `eboot+0x35c2a0`.

## How input travels

- **known** Each frame, every game reads its pads and sends them to the host as one INPUTS message (`eboot+0x358180`, called from the main loop at `0xc6f2`). It also appends a copy to a pending list in the input manager (`[eboot+0x130d5c8]+8`, count at `+0x10`, records of 0x2d0 bytes). The host does the same: it sends INPUTS to itself.
- **known** The host runs its side on a thread of its own. The loop in `eboot+0x39c290` makes a wait call (`eboot+0x8d9cd0`, given descriptor sets and a timeout of 0 s and 33,333 µs) unless messages are already waiting, then calls the host update `eboot+0x3a0900`. So the update runs at least 30 times a second, and again whenever a message arrives.
- **known** A received INPUTS message goes through `eboot+0x39e7b0` into `eboot+0x35ba60`, which appends each record to that player's queue. The queues are four entries of 0x38 bytes at `[0x130d5c8]+0x4a40`: player number at `+0`, queue at `+8`, its length at `+0x10`, a history of the length at `+0x18`.
- **known** `0x35ba60` appends without any test on the record's frame number or on the queue's length. The append helper `eboot+0xb9cb10` was not read, so a cap inside it is not ruled out.
- **known** Three things are dropped before a queue, none of them for being late:
  - a record for a player who is not one of the sender's own (`0x39e7b0`);
  - the whole message, when the sender's slot on the host is in a state below 0xb, which is a player still joining (`0x39e7b0`, the test before the call to `0x3a1f40`);
  - a record for a fifth player when all four queue entries are taken (`0x35ba60`).
- **not found** Whether a guest who is already in the host's player list but still below state 0xb counts as a PC with no input in (a). If it does, the host goes through the wait for as long as that guest loads.
- **known** The host update calls the builder `eboot+0x35c2a0` through `0x39ee60`, up to five times per update (`0x3a0900`, the loop around the calls at `0x3a0a16` and `0x3a0a40`). The first call of an update passes 1 as its last argument, later calls pass 0. Each call that produces a frame sends it to every player (`0x39e9f0`).
- **known** A game steps its simulation only on a COMBINED_INPUTS message from the host: the step loop in the main loop ends when `eboot+0x3ae640` (called at `0xb021`) has no message to give. Ticket 03 had this as inferred. On taking one, it removes from its pending list every input the message has caught up with (`eboot+0x359a50`: each player's record carries the number of the last input used).

## (a) Does the host ever wait for a guest?

**Yes. The inference in ticket 03 is refuted.**

What the builder does on each call (`eboot+0x35c2a0`):

1. **known** For each player in the game it takes the first record in that player's queue, if there is one. A player with an empty queue gets no record yet.
2. **known** If no player at all has a record, it produces nothing.
3. **known** It notes whether any PC (online ID) has no record this call.
4. **known** For each player it takes two minimums over the history of that player's queue length: one over the whole history, one over its newest half. The history holds up to `FramesBeforeAgressiveCatchup` samples (30; `eboot+0x35bce0`), so the half is 15 (`0x35cb1b`: the setting shifted right by one; 15 with no settings).
   - If the minimum over the whole history is 2 or more, the player is "backed up".
5. **known, checked in assembly** `0x35cfc2..0x35cffc`: if this is the first call of the update, and some PC has no record, and no player is backed up, the builder empties its output and returns false. Nothing is sent. The fifth argument is stored at `[rbp-0x160]` (`0x35c2f1`) and compared with 1 at `0x35cfc2`.
6. **known** Otherwise it produces a frame. It returns true when some player is backed up, and the update then calls it again.

So:

- **known** The host waits whenever any PC's queue is empty at the start of an update. It stops waiting when a player's queue has been two or more deep through the whole history. With a silent guest that player is the host itself: its own inputs keep arriving and pile up.
- **inferred** That takes about one second. Basis: a sample is added in each update for which `0x3a0770`, the function that reads the players' messages, returns a flag (`0x3a0900` then calls `0x35bce0`), and the host's own inputs arrive 30 times a second. What exactly sets that flag is not known (`eboot+0x39f0e0` was not read); "an INPUTS message was read" is the assumption. With a guest also sending, updates and samples come more often, so the window can be shorter than a second. It is also shorter when the history holds fewer than 30 samples.
- **known** While the host waits, no game steps: every game, the host's too, steps only on the host's frames.
- **known** After the wait the host makes extra frames to work off its backlog. After each extra frame it lowers every sample in every history by one (`eboot+0x35be90`), so the extra frames stop when the minimum falls under 2.
- **observed** This is the pattern ticket 03 recorded after the two fatal losses, on both PCs: "about 30 flips with no step, then about 30 flips with two steps each", in 2 s cycles (its section (c), "The fatal datagram", from the D3 and T0 `[sim]` lines). Thirty flips with no step is the wait. The guest's inputs had stopped reaching the host.

How this fits what ticket 03 saw:

- "The host's frames stayed at 33.3 ms while the friend's game sent almost nothing" was about frames shown, not simulation steps. A waiting host still shows frames.
- "One step per flip at an RTT of about 350 ms" fits too. A steady delay only shifts the guest's inputs in time. The queue empties when inputs stop coming for longer than the queue is deep.

**For the netcode bar:** what makes the host stall everyone is a **gap** in a guest's inputs, not the RTT. Sources of gaps: a freeze on the guest's PC (no INPUTS sent), a lost datagram (INPUTS travel on an ordered stream, so everything behind the loss waits for the resend, about 2 × the game's RTT), and anything in the link that holds datagrams and releases them together.

## (b) What the host does with a guest's input that arrives late

- **known** It has no test for lateness. Every input that arrives is appended to its player's queue and used in arrival order, one per frame the host makes (`0x35ba60`, and the queue pop at `0x35d090..`).
- **known** Nothing is dropped for being late (the three drops that exist are listed under "How input travels"), and nothing is used "at the current frame" out of turn. An input that arrives after a gap is used after all the ones before it.
- **known** When the host makes a frame for which a player has no input (only after the wait in (a)), it copies that player's **last used input** whole and stamps it with the new frame number (`0x35d130..0x35d671`). The last inputs are kept in a list at `[0x130d5c8]+0x33b0`, updated by `eboot+0x35a600`. A player with no last input gets an all-zero record.
- **inferred** So a guest who was holding the stick keeps running on every PC while their inputs are missing, and one who wasn't stands still. Basis: the whole record is repeated, sticks and buttons included.
- **known** A queue that stays deep is worked off: while a player's queue has been two or more deep through the whole history, the host makes extra frames in the same update, up to five calls per update. That is what "aggressive catch-up" means on the host.
- **inferred** The queue is therefore a jitter buffer that sizes itself. Every time it runs empty the host stalls one update and the queue ends one deeper; it is trimmed only when it has stayed two or more deep for the whole window. The guest's inputs take effect later by the queue's depth. Basis: steps 5 and 6 in (a).
- **known** The host also tells each player a frame time. Each input record carries one byte for it (`eboot+0x3b0d60` and `0x3b0d90`: 8.33 to 83.3 in steps of 75/255). The host keeps a smoothed value at `[0x130d5c8]+0x4b40`, starting at 33.33 (`eboot+0x358090`), moving toward the largest value the players report (by 0.1 of the difference downward, 0.01 upward; `eboot+0xe75798`, `0xe7579c`). A player whose queue was two or more deep through the newest half of the history is told that value plus 3.0 (`eboot+0xe757a0`), unless every player is in that state (`0x35d001..0x35d033`).
- **inferred** That is a clock correction: a guest whose inputs pile up on the host is told to run each frame 3 ms longer. Basis: the range fits milliseconds per frame, and the main loop multiplies the value by 1000.0 (`eboot+0xe49ca0`) before storing it for timing. Who consumes it was not read.

## (c) What a guest sees past `MaxPredictedFrames` and past `MaxLagBeforeShowLoading`

### The predicted frames

- **known** The predicted count is the number of a player's own inputs still in the pending list, the largest over the local players (`eboot+0x35da40`). Those are the inputs sent but not yet echoed in a frame from the host. It is the round trip in frames: RTT, plus the depth of the queue on the host, plus the frame boundaries at each end.
- **known** `eboot+0x251240` limits it to `MinPredictedFrames..MaxPredictedFrames` (0 and 20). `eboot+0x2514d0` then runs that many predicted steps on top of the last confirmed state. Step *i* uses each local player's *i*-th pending input (`eboot+0x251340` calling `0x35a9f0`).
- **known** So with more than 20 pending, only the oldest 20 are replayed. The newest ones are not shown until the host's frames catch up.
- **inferred** Past 20 the guest's own Sackboy answers late by the excess: at 25 pending, 5 frames (167 ms) late. Nothing else happens at that point: no screen, no message. Basis: the limit is only a loop bound, and no other reader of `MaxPredictedFrames` was found.
- 20 frames is 667 ms. At the 455 ms relayed RTT of 15 November the count is about 14 plus the queue depth, so prediction still covers it, with little room.

### How fast a guest catches up

Read in the main loop `eboot+0x9d70`, `0xa68e..0xae2f`:

- **known** Each frame the game counts the host frames waiting unread (`eboot+0x3adf50`) and keeps the last `FramesBeforeAgressiveCatchup` counts.
- **known** Steps allowed this frame = the smallest count in that history, at least 1, at most `MaxCatchupFrames` (3). So a guest takes extra steps only when it has been behind for the whole window.
- **known** One exception (a build constant at `eboot+0xe1d2ec`, set to 1): with 3 or more waiting and at least one zero in the history, at least 2 steps are allowed. That is the "two steps per flip" after a stall.
- **known** While the loading flag below is set, the allowance is `MaxCatchupFrames` every frame.

### The loading screen

The flag is the byte at `eboot+0x12c9454`. While it is set the main loop skips prediction and the rest of its normal frame work (the test at `0xb711`) and reports progress to `eboot+0x517150` as 1 − waiting ÷ the count when the flag was set. The test is at `0xaab8..0xabaf`, checked in assembly. With `EnableCatchUpScreenNew` true (the default):

- *pred* is the predicted count before limiting (`0x35da40`). *waiting* is the number of host frames unread.
- *avg* is the average of *pred* over samples taken every 33rd frame while the flag is clear, in a ring of 100 (`[world]+0x3ec`, sum at `+0x4c0`, count at `+0x4b4`, ring size at `+0x4bc`). "World" is the object at `[eboot+0x12ecc80]`. With no sample yet, *avg* is the value at `+0x4c4`, which the constructor zeroes.
- **known** Set the flag when *waiting* ≥ `MaxLagBeforeShowLoading` (23) **and no sample has been taken yet** (`0xab4a..0xab6d`).
- **known** Otherwise clear it when *pred* ≤ `MinLagBeforeHideLoading` (2), or when *pred* − *avg* ≥ `LagImprovementInflectionPoint` (`0xab6f..0xab83`).
- **known** Otherwise set it when *pred* ≥ `MaxLagBeforeShowLoading` (`0xab85..0xab93`). At this point *pred* − *avg* is already known to be below the inflection point.
- **known** A change of the flag is undone unless `FlickerThreshold` seconds have passed since the last accepted change (`0xabc0..0xac10`; the clock `eboot+0x1b3f0` is in seconds).
- **known** With `EnableCatchUpScreenNew` false the rule is simpler: clear below 2 waiting, set at `MaxLagBeforeShowLoading` waiting (`0xab12..0xab45`).

What that means with the defaults (23, 2, −1.0, 2.0):

- **inferred** The screen comes up when a game finds 23 or more host frames waiting before its first sample, which is a join or a level start. It stays until fewer than 23 are waiting, and for at least 2 s. Basis: the first rule, and the count at `+0x4b4` is zeroed by the constructor `eboot+0x2472b0` (a `memset` of `+0x3ec..+0x4c7`).
- **inferred** In the middle of play it takes *pred* ≥ 23 while *pred* is more than 1 **below** its own average, so an average round trip above 24 frames (800 ms). A sudden spike gives *pred* above average and clears the flag instead. Basis: the second and third rules with −1.0.
- **not found** What else resets the sample count at `[world]+0x4b4`, and whether the world object is rebuilt for each level. If something resets it in play, the first rule could fire then.
- **not found** That the loading screens the friend kept seeing on 15 November were this screen. The logs don't record loading screens (session measurements, "What the logs can't show"). The code allows it at joins and level starts, where 23 waiting frames are easy to reach. It does not explain a loading screen during a mid-level stall. The game has other loading screens that were not read.
- **known** Two more things a guest can see. With no step for more than 3.0 s (`eboot+0xe49ca4`) the main loop raises notice 0x12 with text key 0x88440cf0 each frame (after its step loop, through `eboot+0x5279c0`; the time of the last step is at `eboot+0x130d90c`), unless `eboot+0x33be70` returns true. What `0x33be70` tests was not read. And with no host slot for more than 40.0 s (`eboot+0xe7b634`), `eboot+0x3acef0` raises the same notice and then calls `eboot+0x3602b0` with 3. The notice's text was not looked up.

## (d) `NumberOfFramesPredictionAllowedForNonLocalPlayer`, `FlickerThreshold`, `LagImprovementInflectionPoint`

- **`NumberOfFramesPredictionAllowedForNonLocalPlayer`** (100; 1000 with no settings).
  - **known** In a predicted step, players who are not local have no pending input. `eboot+0x35a9f0` gives them their last confirmed input again (`eboot+0x357b80` copies the record and recomputes two button bits), but only while the step's number is at most this setting (`0x35ad92`).
  - **known** Past it, those players get no record for that predicted step. What the step does with a player who has no record was not read.
  - **inferred** With the defaults it never bites: the step number can't pass `MaxPredictedFrames` (20). It would matter only if set below `MaxPredictedFrames`.
- **`FlickerThreshold`** (2.0).
  - **known** The shortest time, in seconds, between two changes of the loading flag. See (c).
- **`LagImprovementInflectionPoint`** (−1.0).
  - **known** The value *pred* − *avg* is compared with: at or above it the loading flag is cleared, below it (and with *pred* ≥ 23) the flag is set. See (c).
  - **not found** What the designers meant by it. Read literally, the screen shows when the lag is high but below its usual level, which only makes sense as "lag is improving, cover the catch-up". The reading of the comparisons is certain; the intent is not.

For the ticket on the game's own values, what each key trades:

- `FramesBeforeAgressiveCatchup` is three things at once: how long the host waits for a silent guest, how long a guest's queue on the host must stay deep before it is trimmed, and how long a guest must be behind before it takes extra steps. Lower means shorter stalls and more stutter from catch-up.
- `MaxPredictedFrames` is how much round trip is hidden from the guest's own Sackboy. Higher hides more and costs more simulation steps per frame on the guest.
- `MaxCatchupFrames` is how fast a guest that is behind catches up.
- `MaxLagBeforeShowLoading`, `MinLagBeforeHideLoading`, `FlickerThreshold` and `LagImprovementInflectionPoint` only decide when the catch-up screen covers the game.

## (e) Away and the death countdown

- **known** Each player's Sackboy has a part with `PlayerNumber` at `+0x10`, `OnScreenCounter` (a float) at `+0x30` and `OnScreenStatus` (a byte) at `+0x34`. The names are from the game's own field table (`eboot+0xd1c890`).
- **known** `eboot+0x217060` updates it each simulation frame:
  - For a **local** player it projects the Sackboy onto this game's own screen. When "on screen" changes it sends ON_SCREEN_STATUS with the new value (`0x21733c`).
  - For a player who is **not** local it never decides the status. It only sends a second form of the message when that Sackboy is off this game's screen while its owner says it is on, and then sets a latch at `+0x36` so it doesn't send again. The handler does nothing with that form except clear the latch. What it is for was not read.
  - While `OnScreenStatus` is 0 the counter goes down by 1/30 per frame (`eboot+0xe656c8`); otherwise it is 0.
- **known** The handler `eboot+0xb4b020` writes the received status into `+0x34`.
- **inferred** The death countdown is this counter. It runs while the player's **own** game says its Sackboy is off its own screen. It is the game's rule for a player left behind, and it runs in the simulation, the same on every PC. Basis: the field names and what drives the counter. The code that shows the countdown and kills at its end was not read, so another counter is not ruled out.
- **known** `0x217060` reads no field of the settings object.
- **inferred** So no setting and no network timer starts the countdown. Basis: the identification above.
- **not found** A test of the form "no input from this player for N seconds". The host's only reaction to missing input is the wait in (a) and the repeated input in (b). The other per-player timers on the host are the joining timeouts of ticket 03 (`eboot+0x3abee0`), which INPUTS messages restart (`0x39e7b0` calling `0x3abeb0` with 10 and 11).
- **not found** What "shown as away" is. No string or message with that meaning was found (searched: away, afk, idle, inactive, asleep, absent, unresponsive). The earlier note says "his character stops, looks away" ([rejoin_no_spawn.md](../../.scratch/lbp3-port/coop/rejoin_no_spawn.md)), so it may be an animation and not a status.
- **inferred** How 15 November fits. The friend's game froze for up to 3.5 s at a time on shader builds (session measurements, "Freezes"). During a freeze it sent no inputs, so the host waited about a second, during which every Sackboy stood still on every screen, and then went on with his last input repeated. If the others walked on, his Sackboy was left behind, went off his own screen once his game ran again, and the countdown began. Basis: (a), (b) and the function above. No log shows it.
- **not found** A way for the countdown to start **while** his game is frozen. The earlier note puts it "during a freeze", but a frozen game can't send the status that starts the counter. Either the countdown began just after each freeze, or something not read here also drives it. The order of events was not logged.
- **known** A counter of another kind sits in `eboot+0x355700`: when a game has had no pad assigned for 1800 frames (0x708, one minute) in some game modes, it calls `eboot+0x2691d0` and raises notice 0x4b. It is about a game with no controller, not about a guest's input. What it does was not read.

## Which delays are unavoidable at a given RTT

- **Between a guest's input and its effect on the other PCs:** the trip to the host, plus the depth of the guest's queue there (at least one frame, more after gaps), plus the trip of the host's frame out. Known from (a) and (b).
- **Between a guest's input and its effect on its own screen:** nothing, while the round trip is under 20 frames (667 ms). Past that, the excess. Known from (c).
- **For the host's own input:** the depth of the host's own queue. It is 1 when things run smoothly and grows by every update the host spent waiting, until the catch-up works it off.
- **For everyone, on each gap in a guest's inputs longer than its queue is deep:** a stall as long as the gap, capped at about one second per round by the wait rule, then catch-up at up to twice speed (or three times under the loading screen). Inferred from (a) and observed in ticket 03's D3 and T0 logs.

## Not found, and new questions

Not found, listed in the sections above: a cap on the host's queues (`eboot+0xb9cb10`); who consumes the frame time; what resets the sample count behind the loading screen; whether the friend's loading screens were the catch-up screen; what "away" is; the code that shows the countdown and kills; the text of notice 0x12; what a predicted step does with a player who has no record.

Questions this raises for the map:

1. **How long are the gaps in a guest's INPUTS as they reach the host's game, on the relay path and on a direct one?** The host stalls every player for the length of each gap beyond the queue's depth. The netcode bar should carry a limit on gap length, not only on RTT. Nothing logged on 15 November shows inter-arrival gaps at the game's socket.
2. **Does the peer link's gap-free delivery (DATA3) turn one lost datagram into a gap for the game that is longer than the game's own resend would make?** The game resends after 2 × its RTT; the link holds everything behind a gap for up to 2 s.
3. **What does lowering `FramesBeforeAgressiveCatchup` do to a session where a guest freezes?** It should shorten each stall for the other players in proportion. Its cost on the three things it controls ((d)) needs a two-instance run with an induced freeze.
4. **Does a guest that is loading (in the host's player list, slot state below 0xb) make the host go through the wait on every frame?** Its INPUTS are ignored in that state. If so, every join and level change stalls the other players in one-second rounds, which bears on the map's "Loads" item.
5. **Does the frame time the host hands out explain the host's slow frames in connection 3?** The host follows the largest frame time any player reports ((b)); the session measurements found the host at 22 to 28 frames per second there with no cause in the log.
6. **The map's decision line for ticket 03 says "the host doesn't wait for them". This file contradicts it** and the line should be corrected when this ticket is closed.
