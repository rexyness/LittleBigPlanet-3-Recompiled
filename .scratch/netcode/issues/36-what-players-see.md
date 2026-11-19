# What does a player see about their path?

> **Moved to GitHub:** https://github.com/rexyness/LittleBigPlanet-3-Recompiled/issues/13 (#13) on 2026-11-19. This file is the archive as of the move; work on the issue.

Type: grilling
Status: moved to #13 (2026-11-19); was: open
Blocked by: 07
Map: [Online co-op netcode](../map.md)

## Question

[What is the netcode bar?](07-netcode-bar.md) sets a target (its number 17): each player can see, for each other player, whether the path is direct or relay and the RTT, without opening a log, and is warned when the path is outside what the bar covers. The thresholds for the warning are this ticket's to set. They need room above the 450 ms RTT the bar counts as passing: the friend's pair sat at a median of 455 ms with a 95th percentile of 590, and a warning that never goes off tells nobody anything.

Decide:

- where it is shown: the game's own in-game menu, an overlay the port draws, the launcher, or more than one;
- the words and figures a player reads, and what the warning says and when it goes away;
- whether the game's own quality figure of 0 to 3 per player (message 0x7e) is put to use. Who shows it wasn't found; the link level only feeds it for display ([Do the game's own packet delay and drop settings work, and what do the other unread settings do?](16-unread-settings.md));
- what a player is told when a connection ends: a kick, the other player leaving, the online server dropping;
- what a friend's report has to carry so that a session can be scored against the bar from it. [Keep the day's game logs for a friend's report](33-report-keeps-days-logs.md) covers keeping the logs; this is what else the report needs, if anything.

"How should it look" may want a rough mock-up to react to before it is settled; the user judges that by eye.

It doesn't wait on [How do two players get a direct path, and what carries them when they can't?](08-path-ladder.md): the readout names a path as direct or relay whatever rungs the ladder ends up with.
