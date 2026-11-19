# Does a shorter catch-up window hold under loss and bursts?

Type: task
Status: open
Blocked by: none
Map: [Online co-op netcode](../map.md)

## Question

[How does today's online co-op hold up at set delays and loss?](06-coop-under-set-conditions.md) found that with `FramesBeforeAgressiveCatchup` at 10 instead of 30, a freeze of the guest stalls everyone for a third of a second instead of a second (four freezes each at 60 and 450 ms RTT, no loss). The same value is also how long a guest's queue on the host must stay deep before it is trimmed and how long a guest must be behind before it takes extra steps ([What does the host do with a guest's late input?](17-late-guest-input.md)), so a lower one may cost stutter where the default is smooth.

Run, with [tools/rig3/cell.sh](../tools/rig3/cell.sh) and `LBP3_TEST_CATCHUP_FRAMES` on both games:

- the window at 10 and at 5, against 30, at 60 and 450 ms RTT;
- each with no loss, with 5% single loss (`LBP3_P2P_TEST_LOSS=19`) and with bursts (`101,4`);
- with the freezes of the first ticket (`CELL_FREEZE="1000 3500 1000 3500"`) in the no-loss cells.

Record the flips with no step on both PCs, the longest run, the host's waits and catch-up frames, and the loading screen over the guest's play.

Done when it is known whether a lower window is better or worse in each cell, and which value [Which of the game netcode's own values does the port change?](14-game-netcode-values.md) should take.

**From** [What is the netcode bar?](07-netcode-bar.md)**:** the bar's target for another player's freeze is a stall of no more than 12 flips on the host after a freeze of 1 s and of 3.5 s, at 60 and at 450 ms, and it is provisional on this ticket. A shorter window passes here only if the steady cells and the two-in-a-row burst cell at 450 ms still meet the playable line with it (no stall of 15 flips or more, under 10% of flips with no step, no catch-up loading screen over play). If no window does both, put the stall the best one gives into the bar.
