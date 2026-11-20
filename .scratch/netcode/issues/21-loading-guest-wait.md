# Does a guest that is still loading make the host wait on every frame?

Type: research
Status: resolved
Claimed by: research agent (work-through session, 2026-11-16)
Findings: branch `wayfinder/netcode-host-address`, file `docs/research/netcode-loading-guest-wait.md`
Blocked by: none
Map: [Online co-op netcode](../map.md)

## Question

[What does the host do with a guest's late input?](17-late-guest-input.md) found that the host makes no per-frame message while any player's PC has no input queued, and gives up only after about a second (`eboot+0x35c2a0`, the test at `0x35cfc2..0x35cffc`). It also found that the host ignores the INPUTS of a guest whose slot is in a state below 0xb, which is a guest still joining (`eboot+0x39e7b0`).

Put together, a guest that is loading may count as a PC with no input, round after round. Every join and level change would then stall the other players in one-second rounds for as long as the load lasts.

Establish:

- when a joining guest enters the set of players the builder looks at: before or after its slot reaches state 0xb;
- whether a guest already in the game goes below 0xb again at a level change;
- what the session logs show: the `[sim]` step lines on the host during the joins and level changes of 15 November and of the two-instance runs (`D:\Recomps\lbp3-wt\coop`);
- what resets the lag sample count behind the catch-up loading screen, which the earlier ticket didn't find.

Done when each point is marked known, observed, inferred or not found, with addresses and log lines. The findings file of the earlier ticket has the builder's steps and the queue layout. Ghidra is allowed; see the map's Notes.

## Answer

Resolved 2026-11-16 by the research agent. Addresses, log lines and the basis of each mark are in the findings file.

**No: a loading guest isn't waited for** (inferred from known code).
- The builder only looks at players whose room item is no longer marked pending. A joining guest's items are created pending and cleared by the host's handler of the guest's request to add its players, which also brings the guest's slot to state 0xb before the builder next runs. So the guest enters the builder's set and reaches 0xb in the same host update.
- **The question mixed two tables** (known). The test on INPUTS reads the host thread's own slots (`eboot+0x1322760`), not the table the sync probe prints, where the host shows a guest at "state 1" all session.

**At a level change the guest doesn't go below 0xb** (inferred). Nothing found lowers the state or sets the pending mark again. The guest stays in the builder's set, so the ordinary rule applies: the host waits about a second per gap in the guest's input.

**What the logs show** (observed):
- **Two-instance runs:** every join on the host is one block with no step (23 to 28 flips, 71 or 72 in two runs), then one short stall of 3 to 13 flips while the guest's first INPUTS is awaited. No rounds of thirty. No run with `[sim]` lines has a level change.
- **15 November:** no `[sim]` lines. The frame generator's counts show many flips without exactly one step through each level change (10 to 29 of 30, for 10 to 35 s). That can't separate the host's own load from a wait for the guest.

**What resets the lag sample count** (known). The constructor, add-player and remove-player all clear it, and the last two run on every PC at each change in the number of players. So the catch-up loading screen's start-of-game rule is live for up to 33 frames after every join and every leave.

**Not found:** whether a guest's game sends INPUTS while it loads a level; what stops the host's steps in the block at the start of a join; how a game handles the level change message.

**Corrections to the findings of [What does the host do with a guest's late input?](17-late-guest-input.md):** "a state below 0xb" is about the host thread's own table, and the sample count is reset by every player change, not only by a new game object.

**What follows from it:**
- For loads, the open question is now whether a guest's game sends INPUTS while it loads a level, and how long the longest gap is. It needs a two-instance run with a level change: it goes to [How does today's online co-op hold up at set delays and loss?](06-coop-under-set-conditions.md).
- The log lines should print the host thread's own slot states, since the probe prints the other table: [Which log lines does a session need to measure itself?](19-session-log-lines.md).
- A new ticket: [What stops the host's simulation when a guest starts to join?](23-join-start-stall.md)

**Corrected later by** [What stops the host's simulation when a guest starts to join?](23-join-start-stall.md)**:** the host's main loop isn't running in the block at the start of a join, the two long runs were long because of the relay's RTT, and the world is replaced on every PC at every join.
