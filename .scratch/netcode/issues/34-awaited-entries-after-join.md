# What are the entries the host thread keeps awaiting after a join?

Type: research
Status: resolved
Claimed by: a research agent fired by work-through session 4, 2026-11-17
Findings: `docs/research/netcode-awaited-entries-after-join.md`, on the branch `wayfinder/netcode-session-log` (`f2b60bd`; the agent wrote in the firing session's worktree, so there is no `research/` branch for it)
Blocked by: none
Map: [Online co-op netcode](../map.md)

## Question

The session log prints the host thread's list of who has loaded the game state (the map at `+0x2d8` of the object at `eboot+0x1322760 + 0xa10`; an entry's "loaded" byte at `+0x40`, its slot at `+0x48`). [What stops the host's simulation when a guest starts to join?](23-join-start-stall.md) read it as: one entry per PC at a join, all loaded, UNBLOCK_LOADING sent, entries removed.

The runs of [Which log lines does a session need to measure itself?](19-session-log-lines.md) show more (observed, sl3, sl4 and sl6, the `game state exchange` lines on the host):

- the first pair of entries starts as read: the host's own is loaded within 0.1 to 0.2 s, and the guest's stays awaited until the block ends;
- when the block ends the list shows a pair, host and guest, both awaited, and it stays for the rest of the session. In sl4 the list was seen empty for one sample (0.1 s) in between, so it is a new pair there; in sl3 and sl6 no empty sample was caught, so a pair whose "loaded" bytes were cleared isn't ruled out;
- a level start adds another pair (four awaited entries);
- when the guest is removed its entries go and the host's own two stay.

The simulation runs through all of it.

Find, in the eboot:

- what makes the second pair (which message, which key: the map's key is a descriptor and a state number), and what would mark it loaded;
- whether anything waits on it: a later join, a level change, a player's spawn, a timeout;
- whether the pairs pile up without end over a long session with many level changes, and what that costs.

Done when the pairs are named and it is known whether an awaited entry that stays is harmless. [Which of the game netcode's own values does the port change?](14-game-netcode-values.md) weighs a watchdog that goes by these "loaded" bytes and needs the answer.

Findings go to `docs/research/netcode-awaited-entries-after-join.md` on a `research/netcode-awaited-entries-after-join` branch, written in the firing session's worktree. Skills: `research`, `reverse-engineering`. The logs: `D:\Recomps\lbp3-wt\coop\A\stderr_sl3.txt`, `stderr_sl4.txt`, `stderr_sl6.txt`.

## Answer

Resolved 2026-11-17, from the eboot (no game was started). Full findings in the file named above.

- **The pair that stays is not a game state pair.** It belongs to the data labels message (UPDATE_DATA_LABELS, 0x68) that the host's game sends after every game state exchange and after every level load with two or more PCs in the session. The host thread makes one entry per PC for every 0x68 it takes (`eboot+0x3a6640`). A game reports "loaded" for it only when the message carries labels, and in the Pod it carries none, so nobody reports and the entries stay. Known, checked in assembly.
- **It is a new pair, not the first with its bytes cleared.** The "loaded" byte is written 0 only when an entry is made and 1 only by the handler of 0x66. The first pair is erased at the unblock and the labels pair is made a few milliseconds later, which is why two of three logs caught no empty sample.
- **Nothing waits on it.** An unloaded entry holds back only the unblock of its own key, and no game waits for the unblock of a labels message without labels. No timer, spawn, level change or later join reads the map. That a later join never meets the stale key is inferred: no log has a second join.
- **They pile up, cheaply.** 80 bytes per PC per join or level load without labels. A guest's entries go when it leaves; the host's own are never removed. A thousand level loads with four players are 320 KB and one walk per host update. Arithmetic, not measured.

**What this corrects:**

- [Can a game take part in a join and never report the game state loaded?](28-never-reports-loaded.md): its findings file says two such messages never share a key. They do: the key's number is always 0 for a 0x68.
- **The session log's `game state exchange` line reads the entries right and names them wrongly.** It prints stale labels entries as "game state ... awaited", can't print "over" again after the first join, and stops at 16 entries (one join and seven level loads with two PCs). The fix is to print each entry's type (`+0x38`) and treat type 0x42 with an all-zero hash (`+0x24`) as a labels entry. It is done on the runtime branch with the set-conditions runs; see that ticket.
- **A watchdog** ([Which of the game netcode's own values does the port change?](14-game-netcode-values.md)) must go by the "simulation running" byte first and skip labels entries with an all-zero hash. A labels entry with a real hash is a PC truly awaited.
