# Should join protection let two guests of one host connect?

Type: grilling
Status: open
Blocked by: 18, 31
Map: [Online co-op netcode](../map.md)

## Question

The peer link refuses a connection from any player who isn't a shadNet friend of this player (`MayConnect` in `np_shadnet.cpp`; refused live on 2026-11-13). The game connects every player in a room to every other one. So two guests who are each a friend of the host, but not of each other, may have their pair refused, and the game would then make them remove each other after 10 s. It is a guess: the check is skipped for a player this side's own game asked to connect to, and each guest game may ask for the other. If the run shows the pair connects, close this ticket as answered by it. [What does a third player do to a session?](18-third-player.md) runs it.

Decide, once that run has shown what the session does:

- whether a player the host has let in is accepted by the other guests' links, and how a link learns who the host let in (the room's member list, which the game already holds, or a word from the host's link);
- whether that weakens what join protection is for: nobody reaches a player's link unless that player, or the host they joined, has agreed;
- what the players are told when a pair can't connect.

Done when the rule is written for the netcode spec's section on three and four players.

**From** [What does a third player do to a session?](18-third-player.md)**:** the pair between two guests who aren't friends fails before join protection is ever asked: the online server gives a player's address to friends only (error 26), so neither link has anything to try, and the third player never gets into the session. That server is LBPPC's and out of this map's reach. So the questions here become:

- how a guest's link learns the other guest's address without the server's lookup. The host's link knows both: it could pass each the other's address (and a word that it let them in), or carry their datagrams itself;
- then, whether the port's own friend check lets that player in, which is the question above;
- what the third player and the host are told when the pair can't be made. Today nobody is told anything.
