# Assemble the netcode spec

Type: task
Status: open
Blocked by: 07, 08, 09, 14, 18, 26, 32, 36, 37, 38
Map: [Online co-op netcode](../map.md)

## Question

Write the **netcode spec**, the destination of this map, at `.scratch/netcode/spec.md`, from the closed tickets' answers. It covers:

- the netcode bar, with how each target is measured;
- the order of paths, and what carries pairs who can't go direct;
- how game datagrams are delivered;
- which of the game netcode's own values the port changes;
- what holds for three and four players;
- what the `fix/coop-online` lane settled about joining, leaving and rejoining;
- the test rig: two instances on this PC under set conditions, and the session with the friend;
- a build ladder whose first step a build session can start.

It collects decisions already made and decides nothing new. If writing it turns up a gap, open a ticket for the gap instead of filling it in here.

Done when the spec exists, links each section to the ticket that decided it, and build tickets can be cut from its first step.

**From** [What is the netcode bar?](07-netcode-bar.md)**:** the bar is set (18 targets, each a keep or a gain, with its cell and pass rule). The build ladder should carry a script that runs the bar on the rig and prints pass or fail per target: 8 cells run twice and 6 one-off runs, about four and a half hours. Gains still marked provisional there get their final numbers from the tickets named beside them; copy the numbers as they stand when this is written.

**From** [How does the port keep the game's RTT right on a path faster than a frame?](32-rtt-on-fast-paths.md)**:** two steps for the build ladder, neither in the peer link's files: the one-frame floor on the game's RTT sample, then one clock for the game's connection code (with the tick gate). Their pass rules are in that ticket's answer. [Does the one-frame floor hold through the bar's cell and between 0 and 30 ms of delay?](38-floor-between-0-and-30ms.md) checks the floor at the bar's length first.
