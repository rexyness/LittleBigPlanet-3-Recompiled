# Has the joining-and-leaving lane landed?

Type: task
Status: resolved
Claimed by: work-through session 2, 2026-11-16
Blocked by: none
Map: [Online co-op netcode](../map.md)

## Question

Three tickets change `p2p_link.*`, `np_shadnet.cpp` or `sync_probe.cpp`, and each says to build on the `fix/coop-online` lane's head once it has landed: [Put a set delay and jitter on the peer link between two local instances](05-delay-and-jitter-settings.md), [Which lost datagram ends a session?](13-fatal-datagram.md) and [Which log lines does a session need to measure itself?](19-session-log-lines.md). That condition was only in their text, so they showed as takeable when they weren't. This ticket is the edge.

On 2026-11-16 at 20:20 the lane hadn't landed: the runtime's `main` was `f12cc8e`, and the lane (`D:\Recomps\lbp3-wt\res`) was at `54c538f`, four commits past v0.1.5, with a clean tree.

Nothing to do here but look. This map doesn't land that lane; its own session or the user does.

Done when `fix/coop-online` is on the runtime's `main`, or the user says the three tickets may build on the lane's head as it is. The answer records the commit to build on.

## Answer

Resolved 2026-11-16 by the user: "build on the lane's head as it is".

The lane has not landed. The three tickets build on `fix/coop-online` at `54c538f` (clean tree at the time), each on its own branch cut from that commit in its own worktree, not in the lane's worktree `D:\Recomps\lbp3-wt\res`. If the lane moves on, rebase onto it; the lane's session still owns its own files' direction, so keep changes to `p2p_link.*` small and additive.
