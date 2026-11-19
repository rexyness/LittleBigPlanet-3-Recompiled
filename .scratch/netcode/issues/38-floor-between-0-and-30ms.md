# Does the one-frame floor hold through the bar's cell and between 0 and 30 ms of delay?

Type: task
Status: open
Blocked by: 32
Map: [Online co-op netcode](../map.md)

## Question

[How does the port keep the game's RTT right on a path faster than a frame?](32-rtt-on-fast-paths.md) chose a floor of one frame on the game's RTT sample, from one six-minute run at no added delay in the Pod with gap-free delivery off. Two things that choice rests on weren't run:

- **The bar's own cell.** [What is the netcode bar?](07-netcode-bar.md), target 8 at no added delay: ten minutes in the Pod with 5% single loss, a level start and a minute in the level, twice, with gap-free delivery as shipped.
- **The delays between 0 and 30 ms each way.** A direct path between two homes sits there, and that path is what the path ladder is for. The fault needs an acknowledgement inside the same frame, so it should fade somewhere under 8 ms each way; nobody has seen where, or what the floor does on a path whose true round trip is 10 to 40 ms.

Run, on two instances under the game lock, with the runtime branch `netcode/rtt-floor` (`D:\Recomps\lbp3-wt\netdelay\build`) and `LBP3_RTT_FLOOR_MS=33.4` and `LBP3_RTT_HIST=1` on both:

- the bar's cell with [tools/rig3/cell.sh](../tools/rig3/cell.sh): 600 s in the Pod, `LBP3_P2P_TEST_LOSS=19`, twice;
- [tools/rig3/rf_run.sh](../tools/rig3/rf_run.sh) (gap-free delivery off, 5% loss) for six minutes at `LBP3_P2P_TEST_DELAY` 2, 5, 10 and 20, and each of those once more without the floor, to see from where the fault stops by itself.

Record per run: a kick or none, the send budget's cuts, the game's RTT beside the link's, the spread of the raw samples, the length of the join's block and the longest run of flips with no step in it (in the first run the guest's was 128 flips under this loss), and for the bar's cell the row [cell_sum.py](../tools/rig3/cell_sum.py) prints.

Done when the floor has passed the bar's cell twice, or the run that failed is described, and when it is known from which delay up the fault no longer shows. If the floor fails anywhere, say whether a higher floor or real-time samples would have passed, and reopen the choice.

It is ten runs: about an hour and a half of the rig.
