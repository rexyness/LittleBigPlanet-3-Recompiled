# Which of the game netcode's own values does the port change?

> **Moved to GitHub:** https://github.com/rexyness/LittleBigPlanet-3-Recompiled/issues/6 (#6) on 2026-11-19. This file is the archive as of the move; work on the issue.

Type: grilling
Status: moved to #6 (2026-11-19); was: open
Blocked by: 03, 06, 16, 17, 34, 35
Map: [Online co-op netcode](../map.md)

## Question

[What does the game netcode tolerate, and which of its limits can be reached?](03-game-netcode-limits.md) found three kinds of values the port can reach:

- **Keys of the network settings file,** replaced locally where the port already rewrites the server's replies: `MinPredictedFrames` and `MaxPredictedFrames` (0 and 20), `FramesBeforeAgressiveCatchup` and `MaxCatchupFrames` (30 and 3), `MaxLagBeforeShowLoading` and `MinLagBeforeHideLoading` (23 and 2), and the `TIMEOUT_...` keys for joining and level changes.
- **Values in the game's memory:** the kick limit (10 s), the resend factor (2.0 × the game's RTT), the keepalive period (1 s).
- **The bandwidth the port reports,** which caps the game's send budget.

Decide for each whether the port changes it, to what, and when (always online, or only on the relay path).

What to weigh:

- **Corrected on 2026-11-16:** [What does the host do with a guest's late input?](17-late-guest-input.md) found that with the default values the catch-up loading screen comes up at a join or level start and is all but ruled out in the middle of play. So the 23-frame threshold is an unlikely cause of what the next bullet describes, unless those screens were all at joins and level starts.
- On 15 November the friend saw constant loading screens at about 450 ms RTT. The 23-frame threshold (767 ms) is a candidate cause. That is a guess from the numbers until [How does today's online co-op hold up at set delays and loss?](06-coop-under-set-conditions.md) shows it.
- The kick was the cliff on 15 November: three connections ended on it, and a 9.4 s delay came within a second of it. Raising the limit gives a stalled stream longer to recover. It doesn't repair one.
- A value that changes when a game steps must be the same for every player. Both run the same version, so a local override applies to both.
- What each prediction, catch-up and loading key trades, from [What does the host do with a guest's late input?](17-late-guest-input.md), and what the unread keys turn out to do, from [Do the game's own packet delay and drop settings work, and what do the other unread settings do?](16-unread-settings.md).

Try values in the two-instance rig where the choice isn't clear from reading.
- [What does the host do with a guest's late input?](17-late-guest-input.md) found that `FramesBeforeAgressiveCatchup` also sets how long the host waits for a guest with no input queued: about a second at 30. Lowering it should shorten each stall when a guest freezes. Its cost on catch-up needs the induced-freeze runs.
- [Do the game's own packet delay and drop settings work, and what do the other unread settings do?](16-unread-settings.md) found:
  - the kick limit's float also sets a 9 s threshold (the limit minus 1 s) after which the game accepts session messages from any player holding a slot, so changing the limit moves both;
  - `EnablePrediction`, the packet delay and drop fields and five `TIMEOUT_` keys have no reader, so they aren't candidates;
  - the port's reply rewrite has no branch for the settings file yet. One switched by an environment variable would fit in `AnswerSigned`.
- [What makes the game netcode cut its send budget, and does the peer link trigger it?](15-send-budget-cuts.md) found that the send budget starts at 16,384 bytes a second whatever bandwidth the port reports, never grows in steady play, and doesn't recover from a cut on a guest. Its start value, floor (10,240) and growth rule are values in the game's memory the port could change.
- [Does a guest that is still loading make the host wait on every frame?](21-loading-guest-wait.md) found that the catch-up loading screen's start-of-game rule is live for up to 33 frames after every join and leave. That bears on `MaxLagBeforeShowLoading`.

**From** [How long can a joining guest that hangs keep everyone from stepping?](27-hung-join-blocks-all.md)**:** the port can end a wait at a join by saying DEAD on the host's side. Decide whether it does so for a player whose link answers but whose game is silent, and after how long; before 10 s it also drops guests who would have thawed.

**From** [Does a game that thaws after more than 10 s remove every player before it reads what arrived?](29-thaw-removes-everyone.md)**:** a freeze of more than 10 s on one PC ends the session from the other PCs' side, not from its own. The port can hold their kick back: raise the 10.0 everywhere, or refresh a connection's `+0x160` for a player whose link answers while their game is silent. It is the reverse of saying DEAD sooner; decide both as one rule.

**From** [Can a game take part in a join and never report the game state loaded?](28-never-reports-loaded.md)**:** a join can hang for 600 s or more when a resource fetch goes quiet while the connection stays up. If the port gets a watchdog for that, it has to go by progress (no resource byte to the waiting guest for some seconds while no PC steps; 30 s was proposed, unmeasured), not by a fixed length, since an honest join into a large level can take minutes at a cut send budget.

**From** [Can the host's snapshot go away while a guest is still fetching it?](30-snapshot-replaced-mid-join.md)**:** no honest way into a join that hangs without end was found. A watchdog would be a safety net, not a fix for a known case.

**From** [Which lost datagram ends a session?](13-fatal-datagram.md)**:** the game's RTT sample at an acknowledgement (`eboot+0x37f100`) is one more value the port may have to set, because the frame clock makes it negative or 0 on a fast path. That choice is made in [How does the port keep the game's RTT right on a path faster than a frame?](32-rtt-on-fast-paths.md), not here. The same run showed the game cutting its own send budget to the floor when its RTT reads too low: a point for the question of holding the budget up.

**From** [Which log lines does a session need to measure itself?](19-session-log-lines.md)**:** an awaited entry in the host thread's list does not by itself mean a join block. After every join and level start the list keeps a pair of entries that nobody reports loaded, while the simulation runs. A watchdog has to go by the "simulation running" byte (the `join block` lines) and use the entries only to name who is awaited while that byte is clear. What the leftover pairs are: [What are the entries the host thread keeps awaiting after a join?](34-awaited-entries-after-join.md).

**From** [How does today's online co-op hold up at set delays and loss?](06-coop-under-set-conditions.md)**:** two levers are measured once each. The catch-up window at 10 cuts the stall from a guest's freeze from a second to a third (`LBP3_TEST_CATCHUP_FRAMES`, the settings object in memory); whether it holds under loss is [Does a shorter catch-up window hold under loss and bursts?](35-catchup-window-under-loss.md). The kick limit at 30 s let a 12 s freeze of the guest pass (`LBP3_TEST_KICK_SECONDS`, the float at `eboot+0x1061c4c`); the host cut its send budget three times and both went on. A 10 s one-way hold is a kick at the default.

**From** [What is the netcode bar?](07-netcode-bar.md)**:** three targets land here. Another player's freeze stalls the rest for no more than 12 flips (today 15 to 30; the catch-up window at 10 gave 8 to 10). A session survives 12 s of one player's silence in play, as a freeze and as a one-way hold at 450 ms (today a kick at 10 s; the hold has never been run with a raised limit: run it). With a joining guest hung as the join block begins, the host steps again within 12 s (today 10.6 s). The last two pull against each other: a raised kick limit alone would let a hung joining guest hold everyone as long. So the rule has to tell play from a join block. If 12 s can't be had without breaking the third, put the number that can into the bar.
