# What does the host do with a guest's late input?

Type: research
Status: resolved
Claimed by: research agent (work-through session, 2026-11-16; the first agent stopped on a usage limit on 2026-11-15 with nothing written)
Findings: branch `wayfinder/netcode-host-address` (`74a1d8c`, local only), file `docs/research/netcode-late-guest-input.md`. The agent couldn't write to the `research/netcode-late-guest-input` worktree, so that branch stays empty
Blocked by: none
Map: [Online co-op netcode](../map.md)

## Question

[What does the game netcode tolerate, and which of its limits can be reached?](03-game-netcode-limits.md) found that every game steps on the host's per-frame message, and inferred that the host doesn't wait for guests: a guest's input takes effect when it comes back inside the host's per-frame messages, hidden by prediction of up to `MaxPredictedFrames` (20). How the host builds that message wasn't read (`eboot+0x3a24c0`, `0x3a4e00`, `0x3a5bb0`, `0x3eae60`).

Establish:

- whether the host ever waits for a guest (confirm or refute the inference);
- what the host does with a guest's input that arrives late: uses it at the current frame, drops it, or repeats the last one;
- what a guest sees when its queue of predicted frames passes `MaxPredictedFrames`, and when the lag passes `MaxLagBeforeShowLoading` (23): is that the loading screen the friend kept seeing on 15 November?
- what `NumberOfFramesPredictionAllowedForNonLocalPlayer`, `FlickerThreshold` and `LagImprovementInflectionPoint` do;
- what makes the host show a guest as away and start the death countdown: how long without input, and whether that is a setting.

The netcode bar needs this to say which delays the game makes unavoidable at a given RTT, and the ticket on the game's own values needs it to know what each prediction, catch-up and loading key trades.

Done when each point is marked known, observed, inferred or not found, with addresses. Ghidra is allowed; see the map's Notes.

## Answer

Resolved 2026-11-16 by the research agent. Addresses and the basis of each mark are in the findings file.

**The host does wait for a guest** (known, checked in assembly). This refutes the inference in [What does the game netcode tolerate, and which of its limits can be reached?](03-game-netcode-limits.md).
- The host makes no per-frame message (COMBINED_INPUTS) while any player's PC has no input queued (`eboot+0x35c2a0`).
- It gives up once some player's queue has been two or more deep for a whole window of `FramesBeforeAgressiveCatchup` (30) samples: about one second (inferred).
- While it waits nobody steps, the host included. Afterwards it catches up with extra frames.
- **A gap sets the wait off, not the RTT.** A steady delay never empties the guest's queue. A freeze on the guest's PC does, and so does a lost datagram being sent again, or a burst the link held back.
- This is the "30 flips with no step, then 30 with two" cycle seen after the two fatal losses.

**Late input** (known). The host has no notion of late: inputs are queued per player and used in order, one per frame, and none is dropped. After a wait it repeats the missing player's last input.

**Past `MaxPredictedFrames`** (inferred from a known loop bound). The guest replays only its oldest 20 inputs the host hasn't echoed. Its own Sackboy answers late by the excess, which starts at a round trip of 667 ms. Nothing else happens.

**Past `MaxLagBeforeShowLoading`** (known logic). With the default values the catch-up loading screen comes up when 23 or more host frames are waiting at a join or level start. In the middle of play the default `LagImprovementInflectionPoint` all but rules it out. Whether it is the screen the friend kept seeing on 15 November: not found, because loading screens aren't logged.

**The three keys** (known):
- `NumberOfFramesPredictionAllowedForNonLocalPlayer` limits how many predicted steps repeat other players' last input. At 100 against 20 it never binds.
- `FlickerThreshold` is the shortest time between loading-screen changes.
- `LagImprovementInflectionPoint` is the threshold that hides or shows the screen. Its intent wasn't found.

**Away and the death countdown.**
- No "time without input" test and no setting was found.
- The countdown is most likely `OnScreenCounter` (inferred): it runs while the player's own game reports its Sackboy off its own screen. The code that shows it and kills wasn't read.
- What "shown as away" is: not found. It may be an animation.
- How 15 November fits (inferred, no log shows it): the friend's game froze on shader builds and sent no input. The host waited about a second, then went on with his last input repeated. His Sackboy was left behind and went off his screen, and the countdown began.

**Not found:** a cap on the host's queues; what resets the lag sample count; how the countdown could start while the friend's game was still frozen.

**What follows from it:**
- The netcode bar needs a limit on the length of gaps in a guest's input, not only on RTT: [What is the netcode bar?](07-netcode-bar.md). The gaps have to be logged first: [Which log lines does a session need to measure itself?](19-session-log-lines.md).
- Whether DATA3's hold behind a lost datagram makes a longer gap than the game's own resend goes to [How should the peer link deliver game datagrams?](09-datagram-delivery.md).
- What lowering `FramesBeforeAgressiveCatchup` does when a guest freezes goes to [Which of the game netcode's own values does the port change?](14-game-netcode-values.md) and to the runs in [How does today's online co-op hold up at set delays and loss?](06-coop-under-set-conditions.md).
- A new ticket: [Does a guest that is still loading make the host wait on every frame?](21-loading-guest-wait.md)
- A second new ticket: [Does the frame time the host hands out explain its slow frames?](22-host-slow-frames.md)

**Corrected on 2026-11-16** by [Does a guest that is still loading make the host wait on every frame?](21-loading-guest-wait.md): a loading guest isn't waited for; the slot state below 0xb is in the host thread's own table, not the one the sync probe prints; and the lag sample count is reset at every change in the number of players, so the catch-up loading screen can come up after every join and leave, not only at the start of a game.
