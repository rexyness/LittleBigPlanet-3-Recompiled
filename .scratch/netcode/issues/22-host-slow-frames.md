# Does the frame time the host hands out explain its slow frames?

> **Moved to GitHub:** https://github.com/rexyness/LittleBigPlanet-3-Recompiled/issues/8 (#8) on 2026-11-19. This file is the archive as of the move; work on the issue.

Type: task
Status: moved to #8 (2026-11-19); was: open
Blocked by: 19
Map: [Online co-op netcode](../map.md)

## Question

In connection 3 on 15 November the host ran at 22 to 28 frames per second with no cause in its log ([What did the 15 November session with the friend measure?](01-session-measurements.md)). [What does the host do with a guest's late input?](17-late-guest-input.md) found that the host follows the largest frame time any player reports. A guest whose game runs slowly would then slow the host's too.

With the log lines from [Which log lines does a session need to measure itself?](19-session-log-lines.md) (add the frame time each player reports, and the one the host hands out, to its per-window line on the host):

- run two instances on this PC with the guest's frame rate held down, and see whether the host's follows;
- say whether that fits connection 3.

Done when the host's slow frames are tied to the guest's reported frame time, or that is ruled out. If it is the cause, the netcode bar and the ticket on the game's own values need to know whether the port can cap it.

**Before starting:** the game lock applies to every run.

**From** [Which log lines does a session need to measure itself?](19-session-log-lines.md)**:** the host's per-window line exists (`[ses-win]`, with the frames the host made and each PC's INPUTS), on the runtime branch `netcode/session-log-game`. It doesn't carry the frame times yet: add them there (the code is `SessionLogThread` in `runtime/src/core/sync_probe.cpp`). [tools/rig3/sl_run.sh](../tools/rig3/sl_run.sh) runs a join, the Pod, a level start and a dropped guest in one go.
