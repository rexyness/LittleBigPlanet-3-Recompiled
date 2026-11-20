# Keep the day's game logs for a friend's report

> **Moved to GitHub:** https://github.com/rexyness/LittleBigPlanet-3-Recompiled/issues/11 (#11) on 2026-11-19. This file is the archive as of the move; work on the issue.

Type: task
Status: moved to #11 (2026-11-19); was: open
Blocked by: none
Map: [Online co-op netcode](../map.md)

## Question

The launcher keeps the game logs of two launches only, so the friend's report of 15 November held 19:10 to 19:26 and missed four of the six connections ([What did the 15 November session with the friend measure?](01-session-measurements.md)). The session log lines now put everything a session measures into the game log ([Which log lines does a session need to measure itself?](19-session-log-lines.md)), so a report that drops logs drops the measurements.

Make the report carry every game log of the day it is sent on: either the launcher keeps the day's logs, or it keeps more than two and the report takes those of the last 24 hours. Mind the size: a session writes about one session-log line a second on top of what it wrote before, and the report is a zip.

This was split from the session-log ticket: it is launcher work, apart from the runtime's lines. Until it is built, both players send their report right after each session, as the checklist of [Does an inbound Windows Firewall rule on both PCs open a direct path to the friend?](20-firewall-rule-test.md) says.

Done when a report made after three launches in one day holds the logs of all three, checked on this PC.
