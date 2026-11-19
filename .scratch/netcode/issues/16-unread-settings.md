# Do the game's own packet delay and drop settings work, and what do the other unread settings do?

Type: research
Status: resolved
Claimed by: research agent (work-through session, 2026-11-16; the first agent stopped on a usage limit on 2026-11-15 with nothing written)
Findings: branch `wayfinder/netcode-host-address` (`e7393d4`, local only), file `docs/research/netcode-unread-settings.md`. The agent couldn't write to the `research/netcode-unread-settings` worktree, so that branch stays empty
Blocked by: none
Map: [Online co-op netcode](../map.md)

## Question

[What does the game netcode tolerate, and which of its limits can be reached?](03-game-netcode-limits.md) tabled the network settings keys and found a reader for most. For some it found none: `EnablePrediction`, `EnableKeepAlive`, and the packet delay and drop fields. If the delay and drop fields are a working simulator in this retail build, the port can set them through the settings file it already rewrites locally, and that is a test rig that needs no change to the peer link.

Establish:

- for each key without a known reader: its field, who reads it, and what it does;
- for the delay and drop fields: where in the send or receive path they act (above or below the game's own reliability layer), their units, whether they act per direction, and whether the code is live in this build or compiled out;
- how the port would set them for one instance (the reply rewrite in `runtime/src/hle/http_online.cpp`), and whether both players need the same values;
- what the 9 s test in `eboot+0x356540` guards;
- who reads the link level (0 to 2, from the game's own RTT at 200 and 400 ms).

Done when each point is marked known, inferred or not found, with addresses, and it says plainly whether the game's own settings can stand in for [Put a set delay and jitter on the peer link between two local instances](05-delay-and-jitter-settings.md). Ghidra is allowed; see the map's Notes.

## Answer

Resolved 2026-11-16 by the research agent. Addresses and the basis of each mark are in the findings file.

**The game's own settings can't stand in for [Put a set delay and jitter on the peer link between two local instances](05-delay-and-jitter-settings.md).** The delay, the jitter and the one-off hold all have to be added to the peer link.

**The packet delay and drop fields** (four floats at `+0x48` to `+0x54`, and `EnableFakeConditionsForLoopback` at `+0x5c`):
- no reader was found by six scan routes, and neither the send path (`eboot+0x37de30`, `0x37e610`, `sendto`) nor the receive path (`0x378b60`, `0x3795b0`) has a branch that drops or holds a packet (known);
- so the simulator is compiled out of this build (inferred);
- where it acted and whether per direction: not found, since no code is left. The units rest on the key names alone.

**The other keys without a known reader:**
- `EnableKeepAlive` is the HTTP keep-alive flag, handed to `sceHttpCreateConnection`. The netcode's keepalive has no switch in the settings (known).
- `EnablePrediction` has no reader: all 23 instructions with that offset were classified (known).
- `GameStateUpdateRateWithConsumers` is read, at `eboot+0x9f9d2` (known).
- The five unread `TIMEOUT_` keys have no reader (known).
- The earlier table lacked `AllowGameRendCameraSplit` (`+0x5e`), a camera setting.

**Setting keys from the port.** `FixUpReply` in `http_online.cpp` has no branch for `network_settings.nws` today. `AnswerSigned` could carry one, switched by an environment variable (known). This matters for the keys that are read; for the packet fields it is moot.

**The 9 s test** (`eboot+0x356540`). It decides whether to ignore a session message by its sender. Once the simulation hasn't stepped for 9 s, any player holding a slot is accepted (inferred). The 9 s is the kick limit minus 1 s, from the same float (`eboot+0x1061c4c`), so changing the kick limit in memory moves this threshold too.

**The link level** (0 to 2, on the connection at `+0xc8`). One reader was found, `eboot+0x382fd0`, which turns it into a quality figure of 0 to 3 per player and sends it on in game message 0x7e (known). It feeds a display, not the netcode (inferred).

**Not found:** who reads the quality figure and which of its three measures is in use; the key name of the field at `+0x6c`; two branches of the 9 s function; what the second packet layer (`eboot+0x3c0e80` to `0x3c2980`) carries.

**A correction to the earlier findings:** errno 0xa3 on the game's local-delivery queue is 163, which shadPS4's header names `EINACTIVEDISABLED`. It isn't a "returned" error.

**What follows from it:**
- [Put a set delay and jitter on the peer link between two local instances](05-delay-and-jitter-settings.md) is no longer blocked, and builds all of it in the link. It also takes the one new question: whether the host's datagrams to itself ever bypass the socket.
- The kick limit's second use and the keys with no reader go to [Which of the game netcode's own values does the port change?](14-game-netcode-values.md).
- The quality figure in message 0x7e may be something the game can already show a player. That belongs to "What the players see" on the map.
