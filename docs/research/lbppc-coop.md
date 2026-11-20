# Online co-op with LBPPC players (the LBPPC co-op mode)

**Decision (the user, 2026-11-20):** make this port able to play online co-op with players on LBPPC's own
client (github.com/emurphy04/LBPPC), as an opt-in launcher switch ("Play with LBPPC players", off by
default, applied at the next start). It reverses two decisions of the netcode map (#1, 2026-11-15):
"LBPPC's client is out of scope" and, for this mode only, "one protocol". Players of this port who
leave the switch off are unaffected.

LBPPC paths below are at `cb4cbe7` (just past v0.1.5), under `gpu/bbnet/vendor/shadps4/` unless
noted. Tags: **read** (seen in code), **inferred**.

## What the two ports already share

Both sign in to play.lbppc.com: shadNet (friends, presence, Web API invitations), its STUN pings and
the "SMRL" relay on UDP 31314, `RequestSignalingInfos` for a peer's address, and Refresh for levels.
So friends lists and community levels were shared before this work. **read**

## The peer protocol (what this mode adds)

Full layout and sequence: [`runtime/src/net/p2p_lbppc.h`](../../runtime/src/net/p2p_lbppc.h). In short:

- One UDP socket. Every packet is a PS4 P2P datagram frame `FF 83 <src vport BE16> <dst vport BE16>`
  (`core/net/p2p_codec.h`/`.cpp`). **read**
- No encryption, signing or digest: the keys and the communication ID are only set by
  `sceNetIoctl` commands 0xc9..0xd8 (`core/libraries/net/net_p2p_ioctl.cpp`). LBP3 imports no
  `sceNetIoctl`, and LBPPC's own NP code only calls 0xca (read the default key). **read**
- The game's datagrams carry nothing else; the receiver knows the sender by the source address.
  **read** (`p2p_transport.cpp` HandleDatagram)
- Signaling: vports FFFF/FFFF, then "SHAD" packets, packed little-endian, by type and exact size:
  echo 17 B, handshake 62 B, control 50 B (`core/libraries/np/np_signaling/np_signaling_state.h`).
  **read**
- Sequence (`core/libraries/np/signaling_handler.cpp`): the joiner sends ActivationRequest, then
  Offers every 500 ms; the host checks the join (friends, `LBPPC_JOIN_ALLOW`, or one it asked for),
  answers ActivationAck, and both sides answer Offer→Accept, Accept→Check, Check→CheckAck; a side
  whose Check is answered is connected and sends Established. Relay after 4 s of silence; once
  anything comes through the relay from an IP, everything to that IP goes through it. Echo pings
  every second, RTT only; only Close ends a connection. **read**
- Loss: none handled (no sequence, resend, order). **read** A mixed session runs without gap-free
  delivery; the game's own resends cover single losses (#1, ticket 03).

Our side: in the mode, a connection we start sends our HELLO and LBPPC's opening together; whoever
answers in LBPPC's protocol is an LBPPC player from then on. Our ActivationRequest carries a marker in
`reason` (LBPPC never reads it there), so two of our players in the mode always use our protocol.
HELLO capability 4 says "in the mode": our players in different modes refuse each other.

Tests: `p2ptest` section 9 runs our link against a mock LBPPC player built from LBPPC's own packed
structs and handler logic (not from our header): joins both ways, a stranger refused, Close both
ways, the relay, two of ours keep our protocol, a mode mismatch refused, the mode off ignores LBPPC.

## Simulating alike (lockstep)

LBP3's online co-op runs every player's world in lockstep and ends the session when they differ.

| Item | This port | LBPPC | Status |
|---|---|---|---|
| Exact maths (rcpps/rsqrtps/dpps) | 3,085 eboot + 288 spu.prx sites | the same 3,373 sites and stubs | identical **read** (diffed) |
| FP control state on game threads | MXCSR 0x9fc0 (PS4: FTZ+DAZ), x87 0x037f (`hle/kernel_pthread.cpp`, `hle/fiber.cpp`) | **measured** on a running LBPPC 0.1.5 (2026-11-20): every game-created thread MXCSR 0x1f80 (no FTZ/DAZ), x87 0x037f; none at 0x9fc0 | **differs** in FTZ/DAZ only; the mode runs 0x1f80/0x037f (`LBP3_FP_STATE=lbppc`, checked with the same sampler on ours) |
| HasLBP2 | the game's answer | forced true for every part, online too (`src/probe.c` ~1493): the script native HasLBP2 (0x822bc0) | the mode forces it too (`LBP3_HAS_LBP2=all|game`) |
| "The game diverged" | ends at the first | tolerates two in a row (`src/probe.c` ~500) | differs; add only if mixed runs need it |
| 16:9 simulation camera online | yes | yes | same |
| Game build | US CUSA00473 1.28 | US and EU 1.28 ("the EU 1.28 has the US 1.28's very eboot.bin", LBPPC `run.py`) | same code; EU players untested (their NP title ID differs) |

The FP state changes simulation results here (it was the cause of the Legend of Bunkum white-screen
ending, `input/game_state.cpp`), so the mode runs LBPPC's; in the mode the Bunkum ending can come back
(`LBP3_CUTSCENE_AUTOSKIP=on` is the fallback). Measured with a thread sampler (each thread suspended
for a moment and its MXCSR and x87 control word read): on ours, game threads 0x9fc0/0x037f by default
and 0x1f80/0x037f with `LBP3_FP_STATE=lbppc`; on LBPPC 0.1.5 (offline, a test data folder),
0x1f80/0x037f on every game-created thread.

## Plan and status

Branch `feat/lbppc-coop` = the netcode chain (`netcode/rtt-floor`, which holds `fix/coop-online`)
merged with v0.1.8 `main`. Landing it lands that chain too.

1. The peer protocol in the link + mock tests. **Done** (`p2ptest` 308/308).
2. The FP-state switch (`LBP3_FP_STATE`), and measuring LBPPC's real per-thread state. **Done.**
3. The mode's plumbing: `LBP3_LBPPC_COOP=1` → link dialect, FP state, HasLBP2; launcher switch. **Done.**
4. A live mixed session on this PC (ours against the installed LBPPC, test accounts, muted, under
   the game lock): join, Pod, a level; the sync checks agree; then the tick gate with the mode off.

Acceptance: a mixed session holds with the sync checks agreeing. If matched FP state and HasLBP2
still diverge, stop and report.

## Costs and open questions

- Mixed sessions get LBPPC's link: no gap-free delivery, a sticky relay, no silence timeout.
- Any LBPPC release can change its protocol or simulation: check each one.
- LBPPC's accounts design (`docs/accounts/DESIGN.md`) ends name-and-password sign-in for old
  clients 3 months after its Phase 1; this port signs in that way. Independent of this mode, but it
  needs a word with LBPPC (the user's call), as does telling them about cross-play.
