# What does a session do when the path between two guests dies in play?

Type: task
Status: open
Blocked by: none
Map: [Online co-op netcode](../map.md)

## Question

[What does a third player do to a session?](18-third-player.md) ran three players with a full mesh, and with a guest pair that never connected (the third player never got in). It didn't run the case the keepalive test is about: a guest-to-guest connection that is up and then stops carrying datagrams while both guests still reach the host.

With three instances on this PC ([tools/rig3](../tools/rig3/); all three test accounts are friends now):

- **Cut one pair in play.** Add a test setting to the link that drops every packet from one named player from a chosen moment (on the runtime branch `netcode/session-log`, which has the other test settings). After five minutes in the Pod, cut Kistopawa2 and Kistopawa3 from each other. Record what each of the three games does: who removes whom, after how long, what the players see, and whether the session goes on for the rest.
- **Overlapping joins.** The third player starts joining while the second is still in its join's block. Expected from [Can the host's snapshot go away while a guest is still fetching it?](30-snapshot-replaced-mid-join.md): the host's game takes the second request only after its first "unblock".
- **A leave during a join's block.**

Run with `LBP3_SYNC_PROBE=2`, `LBP3_MSG_PROBE=1` and `LBP3_TRACE_SIM=1` on all three, on the LAN path, under the game lock.

Done when each of the three is described from the logs. The path ladder then knows what a pair without a path costs a session that is already running.
