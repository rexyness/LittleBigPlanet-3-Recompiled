# Does today's build pass the bar's keep targets?

Type: task
Status: open
Blocked by: 07
Map: [Online co-op netcode](../map.md)

## Question

[What is the netcode bar?](07-netcode-bar.md) marks ten of its targets as keeps: things today's build does, which a rework must not make worse. Their "Today" figures come from single runs with three minutes in the Pod, which that ticket itself calls too variable to gate on (one cell gave the host 0.0% of flips with no step in three minutes and 5.1%, with a stall of 13 flips, in ten).

Run today's build (runtime branch `netcode/session-log-game`, `D:\Recomps\lbp3-wt\netdelay\build`) through the keeps as the bar defines them, on two instances on this PC under the game lock:

- the four steady cells that are keeps (an RTT of 60 and of 450 ms, each with no loss and with 5% single loss), ten minutes in the Pod, **twice each**: targets 1, 5, 6, 7 and 8;
- the two-in-a-row burst cell at 450 ms, twice: target 10;
- the cell at 1000 ms with no loss, twice: target 9;
- the hung join, once: target 14;
- three players at 60 ms, ten minutes, once: target 15.

That is 12 runs of about a quarter of an hour and two one-off runs: some three and a half hours of the rig. The tick gate (target 18) was run for [Which log lines does a session need to measure itself?](19-session-log-lines.md) and needn't be repeated.

Record pass or fail per target and per run, with the figures.

Done when every keep has two runs behind it (one for the one-off runs) and the bar is corrected where today's build fails one: the number set to what was measured with some room, or the target turned into a gain. Say which.

It runs no gain: those wait for the tickets they are provisional on.

**From** [How does the port keep the game's RTT right on a path faster than a frame?](32-rtt-on-fast-paths.md) **(2026-11-18):** the worktree `D:\Recomps\lbp3-wt\netdelay` now has the branch `netcode/rtt-floor` checked out. It is `netcode/session-log-game` plus two test settings (`LBP3_RTT_FLOOR_MS`, `LBP3_RTT_HIST`) that do nothing unless set, so its build is today's build for this ticket.
