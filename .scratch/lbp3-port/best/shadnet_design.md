# shadNet layer (online friends, joining, online co-op): design

Asked by the human on 2026-11-13: "go ahead and implement that shadNet layer". The server is LBPPC's `play.lbppc.com`; the human says LBPPC agreed. This is a read-only study and design. Nothing was built, no server was contacted, and no third-party code was run.

Sources:
- our port: `bestofboth` runtime `04f6de1` (`hle/np_offline.cpp`, `hle/net_offline.cpp`, `hle/online.*`, `hle/http_online.cpp`);
- `analysis\eboot_imports.json`, with names from `tools/nid_names.txt` via `tools/ps4_nid.py`;
- LBPPC's read-only clone `D:\Recomps\lbp3-wt\_ref\LBPPC` at `aad6a91` (`docs/online.md`, `gpu/bbnet/**`, `server/linux/shadnet/patches/lbppc-shadnet.patch`, `src/probe.c`, `src/runtime_extra.c`);
- our shadPS4 submodule (`fd3f9cbb`, upstream base #5317), which already contains upstream's shadNet client and network stack.

## 1. What LBP3 calls (eboot imports, CUSA00473 1.28)

**There is no `libSceNpMatching2` import at all.** LBP3 doesn't use PSN rooms: its rooms and matchmaking go through the LBP game server (`/match`: `UpdateMyPlayerData`, `UpdatePlayersInRoom`, …), which our online client already sends to Refresh. A shadNet room layer (Matching2) isn't needed. `libSceInvitationDialog` is a needed module, but no function of it is imported.

| Area | Imports | What we answer today |
|---|---|---|
| **Sign-in, identity** | `libSceNpManager` (27): `sceNpGetState`, `sceNpRegisterStateCallback`, `sceNpCheckCallback`, `sceNpGetNpId`, `sceNpGetOnlineId`, `sceNpGetAccountId`, `sceNpCheckNpAvailability`, `sceNpCheckPlus`, `sceNpCreate(Async)Request`, `sceNpPollAsync`, `sceNpSetNpTitleId`, …; `libSceNpAuth` (3): `sceNpAuthGetAuthorizationCode`; `libSceNpCommon`: `sceNpCmpOnlineId` | Ours (`np_offline.cpp`): signed out, or signed in locally under `LBP3_ONLINE_NAME` when online |
| **Friends and presence** | `libSceNpWebApi` (20): context, handle and request create/send/read; `RegisterPushEventCallback` and `RegisterServicePushEventCallback` with filters; `UtilityParseNpId`. `libSceNpUtility` (8): `sceNpLookupNpId`, `sceNpLookupCreateTitleCtx`/`Request`, plus the bandwidth test (3) | **Not registered** (generic stubs), except the bandwidth test |
| **Peer connection (signaling)** | `libSceNpSignaling` (7): `Initialize`, `CreateContext`, `DeleteContext`, `ActivateConnection`, `TerminateConnection`, `GetConnectionStatus`, `GetConnectionInfo` | **Not registered** |
| **In-game messages** (invites and join requests between players, inferred) | `sceNpInGameMessageInitialize`, `CreateHandle`, `Prepare`, `SendData`, `DeleteHandle`, `Terminate` | Ours: stubs that accept and drop |
| **Game traffic** | The game's own P2P UDP: POSIX `socket`, `bind`, `sendto`, `recvfrom` with `SceNetSockaddrIn.sin_vport` (P2P datagram sockets). `libSceNet` (24): epoll, `sceNetSend`/`Recv`, `sceNetConnect`/`Listen`/`Accept`, the resolver. `libSceNetCtl` (6): state, info, NAT info | Ours (`net_offline.cpp`): loopback only. The Pod's local session talks host↔client over `127.0.0.1:3658`, and the game host thread's `sceNetEpollWait` (33.3 ms timeout) **drives the simulation tick** (tickets 06/09) |
| **Voice** | `libSceVoice` (10): ports, `ReadFromOPort`, connect I→O, plus `FMODVoIPMixer.prx` | Stubs. The game would carry voice in its own P2P stream; microphone capture is separate work (optional phase f) |

## 2. How LBPPC wires shadNet in

**What it is:**
- shadPS4 v0.20.0's network libraries vendored nearly unchanged in `gpu/bbnet/vendor/shadps4` (136 files, all `GPL-2.0-or-later`): `libraries/net` (with `net_p2p`), `network` (http, ssl, netctl), `np` (manager, signaling, web_api/web_api2, matching2, utility, handler), `core/net` (`p2p_transport`, `p2p_codec`, `p2p_tcp`, `p2p_crypto`, `host_net`, `guest_net`), and `shadnet` (`client.cpp`, `shadnet.proto`; rpcs3/rpcsn/shadNet copyright, `GPL-2.0-or-later`).
- Their glue: `bbnet_glue.cpp` (`GPL-2.0-or-later`; reusable) implements what those libraries need from the rest of shadPS4 (threads, mutexes, user service, settings, system events) on their runtime. Their shim headers in `gpu/bbnet/include/` have no SPDX line, which means GPL-3.0: ideas only.

**How the game reaches it:**
- `bbnet_register` registers the **whole** shadPS4 `libSceNet` plus every NP library, so the game's sockets *and* epoll go through shadPS4's net layer.
- Their runtime (`runtime_extra.c`, untagged, so ideas only) keeps a host-loopback path for ordinary sockets. P2P sockets go to shadPS4's net. Because shadPS4's P2P port isn't 3658, they rewrite the game's self-sends to `127.0.0.1:3658` onto the P2P port they bound.

**Threads:** the shadNet client thread (TLS TCP to the server: login, friends, notifications), the P2P transport's UDP receive thread, and the signaling handler's thread.

**P2P traffic:**
- `Core::Net::P2P::Transport` has one host UDP socket for P2P datagrams, P2P TCP streams and signaling. A codec frames each packet with the source and destination vport, and optional crypto and signature.
- Peers connect **directly** when they can (UPnP via miniupnp; the public address comes from shadNet's STUN on UDP 31314).
- Otherwise the **relay** is used: "SMRL" frames (kind 2 = send, 3 = deliver, IPv4 address and port, then the packet) through the server's UDP 31314. That relay is LBPPC's server-side patch to shadNet (`lbppc-shadnet.patch`: `stun_server.cpp` relays only between signed-in players' endpoints). `play.lbppc.com` runs it, so we need nothing on the server side.

**Server patch, part two:** LBP3 players count as being in the same game. The NP Communication ID `NPWR07221_00` is inferred from the title (`CUSA00473_00`), which makes friends' in-game presence (the Pod's friends page, join and invite) work. That's server-side only.

**Account creation:** their `scripts/online_accounts.py` and `tools/shadnet_account.py`, both untagged, so ideas only.
- **shadNet:** the account is made over the client protocol (`CommandType::Create`), plus friend requests (`AddFriend`, `RemoveFriend`).
- **Refresh:** `POST /api/v3/register` (username, email, `passwordSha512`), then `PATCH /api/v3/users/me` `{psnAuthenticationAllowed, rpcnAuthenticationAllowed: true}` with the returned token. Same name on both.

## 3. The critical constraint: keep our tick

Our `net_offline.cpp` loopback with precise epoll timeouts is what keeps steps ≠ 1 under 1% (ticket 09). LBPPC registers shadPS4's whole `libSceNet`, which **we must not do**: the game host thread's `sceNetEpollWait` and the `127.0.0.1:3658` loopback would move into code without our timing fixes.

**Design: our net layer stays in front; remote peers plug in behind it.**
- **The loopback path is unchanged:** `bind`, `sendto` to 127.x, the inbox, epoll readiness and precise timeouts, and the "pads n" local co-op (one process, several users) all stay as they are.
- **A P2P datagram socket** (bound with a vport) gets an optional remote side. A `sendto` to a non-loopback address hands the payload to a `RemoteP2P` adaptor, which wraps shadPS4's `Core::Net::P2P::Transport`, using our socket's vport as `src_vport` and the peer's `sin_vport` as `dst_vport`.
- **Incoming remote datagrams:** the transport's receive thread decodes `(from address, src_vport, dst_vport, payload)` and pushes them into **our** socket's inbox (the same `Datagram` struct, with the real peer address in `from`). It then signals the same epoll and recv wake-up our loopback delivery uses, so `sceNetEpollWait` returns exactly as it does for a local datagram. No timeout behaviour changes; only the readiness source widens.
- **Self-sends** to `127.0.0.1:3658` never touch the transport, so the port rewrite LBPPC needed doesn't arise.
- **With shadNet off,** or signed in but alone, the adaptor is never created and the code path is byte-for-byte today's. The gate is the same as the online lane's: steps ≠ 1 under 1% (2 Introduction runs) and the Thruster recipe 3/3, with `LBP3_SHADNET=1` set but no peer.
- **NetCtl when online:** report the real NAT type and IP from the transport's STUN result, as `sceNetCtlGetNatInfo` and `GetInfo` (the game may refuse online co-op on a "NAT type 3" answer).

## 4. Exact math (lockstep between CPU makers)

- **The problem:** LBP3's online co-op runs both players' worlds in lockstep and checks them ("The game diverged"). `rcpps` and `rsqrtps` are approximations whose results differ between Intel and AMD, and LBPPC treats `dpps` the same way. A PS4 never saw the problem because every console has the same CPU.
- **LBPPC's fix** (`src/probe.c`, untagged, so ideas only): **3,373 sites** in the eboot and `spu.prx` (`exact_math_sites.inc`, generated by `tools/exact_math_sites.py`, also untagged). Each site jumps to a stub that computes exactly 1/x or 1/√x; `dpps` becomes a fixed sequence of additions. Every site is checked first, and nothing is patched on a mismatch. It's on when online, and `LBPPC_EXACT_MATH=0/1` overrides it.
- **For us:**
  - The game's code runs natively here too, so the problem applies.
  - The user has an AMD 5900X (Zen 3); their friend's CPU is unknown. Between two AMD Zen machines the approximations very likely agree (inferred); against Intel they don't.
  - So it's needed for general online co-op. Reimplement it: our own scanner (Zydis is already in shadPS4's externals) over the eboot and over `spu.prx` at load, checked trampolines in `core/patches.cpp` style, on when `LBP3_SHADNET=1`.
  - Also pin the MXCSR state (FTZ/DAZ) on the game's threads to what the PS4 uses, if that differs. To check.

## 5. Join protection

- **Why:** force-join exploits (a forged invite) closed the official servers.
- **LBPPC's rule,** in the signaling handler: accept a signaling or P2P connection request only from a shadNet friend, a name in an allow-list (`LBPPC_JOIN_ALLOW`, for tests), or a peer we connected to first. Everyone else is dropped, with a log line. That handler code is in shadPS4's `signaling_handler.cpp`, where LBPPC edited it (`GPL-2.0-or-later`): reuse it with attribution.
- **Ours:** `LBP3_JOIN_ALLOW`, default friends-only.

## 6. Phased plan (each phase behind `LBP3_SHADNET=1`, default off, testable on its own)

**Reuse versus reimplement:**
- **Reuse, all `GPL-2.0-or-later`, with attribution:**
  - **Already in our shadPS4 submodule:** `src/shadnet/*`, `src/core/net/*`, `src/core/libraries/np/*`, `src/core/libraries/net/*`, and the `protobuf`, `libressl` and `miniupnp` externals.
  - **From LBPPC:** `bbnet_glue.cpp`'s shims as a model, the signaling-handler join check, and their marked `bbport` deltas (`vendor-changes.diff`) where needed. LBPPC's client is v0.20.0; ours is newer, so diff before taking anything.
- **Reimplement:** the account and friend tools, the exact-math scanner and stubs, all runtime glue (LBPPC's `include/` shims are untagged), and the socket bridge (§3).

### (a) shadNet client: sign-in, friends, presence (about 2 sessions; the main risk is the build)

1. **Build** shadPS4's `shadnet` and `np` libraries plus the `core/net` transport into a new static library in our CMake (`runtime/third_party_net`), with `protoc` from the protobuf external. **Main risk:** protobuf, LibreSSL and miniupnp with clang-cl, and the shadPS4 core headers these files include. Do it with stub shims first, the way `bbnet_glue.cpp` does.
2. **Glue:** threads, mutexes and settings, and the user (one user, id 1), on our runtime.
3. **Login:** `shadnet.lbppc.com:31313` over TLS (LibreSSL, Windows trust store), `SetClientVersion` naming our port, `LBP3_SHADNET_NAME`/`_PASSWORD`, kept in `user\config\online_account.txt`.
4. **Answer the NP calls from it:**
   - `sceNpGetState` reports signed in once logged in; our existing local sign-in stays as the fallback;
   - NpId and OnlineId are the account name;
   - **NpWebApi:** register shadPS4's implementation for the paths LBP3 uses, which phase (a) logs first (expected: `userProfile` friendList and presence, and push events for friend status);
   - **NpUtility** `sceNpLookupNpId` maps to shadNet `LookupOnlineId`.
5. **Account tool** (ours): create the shadNet and Refresh accounts with the same name, and add, accept and remove friends.

**Test (one instance):**
- log in, see a friend's presence (needs a second account);
- the Pod's Friends page lists LBP3 friends;
- with shadNet off, nothing changes (the tick gate).

### (b) Invites and joining (about 1–2 sessions)

- **No Matching2.** Rooms come from the LBP server through our existing online client.
- **Log first,** in phase (a), the WebApi requests and the `sceNpInGameMessage*` traffic when the Pod's friends page offers join or invite. Then answer them from shadNet:
  - session and invitation endpoints, and push events (`sessionInvitation`, game intent);
  - in-game messages sent through shadNet's messaging, if the game uses them for join requests (inferred).
- **Prompts:** an invite prompt on our ImGui overlay ("X invited you", accept or decline), like LBPPC's `invitation_prompt_layer`. That's shadPS4's file, `GPL-2.0-or-later`; adapt it to our overlay host.

**Test (two instances):** A invites B, B accepts, and both end up in the same LBP server room.

### (c) Signaling and P2P into our socket layer (about 2–3 sessions; the main risk is the tick and the threading)

- **NpSignaling:** register shadPS4's implementation, wired to shadNet `RequestSignalingInfos` and the transport. Connection status callbacks go through `sceNpCheckCallback`.
- **The socket bridge from §3:** a remote side for P2P datagram sockets, with the transport's receive thread feeding our inboxes and epoll.
- **Connectivity:** UPnP; then direct; then the relay through UDP 31314.
- **NetCtl:** NAT info from STUN.

**Test:**
- two instances: B joins A's level, both Sackboys move, and the session holds for 10 minutes, with steps ≠ 1 measured on both;
- force relay (block direct) and repeat;
- the tick gate with shadNet on but alone.

### (d) Join protection (about 0.5 session)

Friends-only by default, plus `LBP3_JOIN_ALLOW`, with a log line on each refusal.

**Test:** a non-friend's join is refused; a friend's is accepted.

### (e) Exact math (about 1–1.5 sessions)

- **Scanner** (ours, Zydis) for `rcpps`, `rsqrtps` and `dpps` in the eboot (offline, checked into a table) and in `spu.prx` at load.
- **Checked trampolines** to exact stubs, on with shadNet. Compare our site count against LBPPC's 3,373 as a sanity check.

**Test:**
- the offline regression with it on (visuals unchanged except for last-bit precision; screenshot A/B);
- two-instance co-op for 10 or more minutes without "The game diverged". A real AMD-against-Intel check needs the friend's PC.

### (f, optional) Voice chat

`libSceVoice` microphone capture to the game's output ports, and playback through FMOD's VoIP mixer. Separate work; not needed for co-op.

**Total: about 7–9 sessions.** Phases (a) and (c) carry most of the risk.

## 7. Testing setup

**Two instances on one PC is plausible:**
- A second `lbp3.exe` from its own working directory (its own `user\` saves and profile, `analysis\` and remote input), with the game folder via `game_dir.txt` or `LBP3_GAME_DIR`.
- Its own P2P UDP port, a setting.
- GPU memory is about 2 GB per instance on a 24 GB card. Separate windows; both play to the default audio device.
- Drive both with remote input or virtual pads.
- Both share a public IP, so direct connection won't be offered and the server's relay (§2) carries the traffic. That tests the relay first, which is fine.
- To check in phase (c): the game's fixed load address and our process-wide reservations are per process, so they don't clash (inferred).

**A second PC** (the friend's, with their own account) is the real test: a different NAT, and possibly an Intel CPU for phase (e). Parsec isn't involved, since each player runs the port locally.

**Accounts on `play.lbppc.com`:**
- the user's (one name, shadNet plus Refresh);
- one **test account** for the second instance, e.g. `<name>-test`, friends with the user's.
Creating accounts on LBPPC's server is an outward action that needs the user's go, and it would be polite to tell LBPPC about the test account.

**Self-hosting instead:** shadNet (Qt, with LBPPC's patch), Refresh (.NET) and PostgreSQL on this PC. That means downloading and running their server code, which needs the user's explicit permission. It's only worth it for heavy automated testing without touching the public server.

## What we need from the user

1. The online name, and the go to create the account on `play.lbppc.com` (shadNet plus Refresh, same name).
2. The go to create one test account there for two-instance testing (or a friend with their own account).
3. Later: a session with the friend on their PC for the real NAT test and the Intel check.

## First phase to start

**(a), step 1: the build spike.**
- Build shadPS4's `shadnet`, `np` and `core/net` sources plus protobuf, LibreSSL and miniupnp into our runtime with clang-cl, behind `LBP3_SHADNET`.
- Answer `sceNpGetState` and the NpId from a real shadNet login.
- Log every WebApi request the game makes.

It needs no game time until login works. Its first in-game test needs the user's account, or the test account.

## Status: phase (a) (2026-11-13)

Branch `feat/shadnet`: runtime `4d4ace8` on `bestofboth` `04f6de1`, worktree `D:\Recomps\lbp3-wt\shadnet`; shadPS4 `feat/shadnet` = `24e4e494`, unchanged. Both are pushed to the forks; nothing is merged.

**Built:**
- **The client:** shadPS4's shadNet client as LBPPC vendors it (their TLS support, keepalive and newer notifications; GPL-2.0-or-later), copied into `runtime/src/net/shadnet/` with marked `lbp3:` changes:
  - the title id and name come from us ("CUSA00473", not `Common::ElfInfo`);
  - TLS is switched on from our settings;
  - the client version is reported as "LBP3-Recompiled (lbp3-port)".
  `NOTICE.md` records the origin. abseil, protobuf (plus `protoc`) and LibreSSL build from shadPS4's externals with clang-cl (`cmake/shadnet.cmake`).
- **The service** (`net/shadnet_service.*`):
  - **Settings:** `LBP3_SHADNET=1`; `LBP3_SHADNET_SERVER` (`host:port`, `tls://host:port`, or `play.lbppc.com`, which means `tls://shadnet.lbppc.com:31313`); `LBP3_SHADNET_NAME` (else `LBP3_ONLINE_NAME`); `LBP3_SHADNET_PASSWORD`. Either may instead come from `user\config\online_account.txt` (`name=` / `password=` lines).
  - **Status** is Off, Connecting, SignedIn or Failed, with the reason logged. It keeps the friends list from the login reply and the friend notifications.
  - **Off** means no client, no thread, no socket, and the account file isn't read.
- **NP** (`hle/np_offline.cpp`):
  - signed in once the login succeeds; the online ID is the shadNet name; the LAN reports up;
  - user 1's sign-in notice waits for the login's answer, up to 20 s, and a later answer queues a new notice;
  - if both online names are set and differ, that's logged.
- **Logged with their arguments** (`hle/np_shadnet.cpp`, `[shadnet]` lines, first 40 calls each, then every 1000th): every libSceNpWebApi (20), NpUtility lookup (5), libSceNpSignaling (7) and in-game message (6) call. They're answered as before (the generic stub's 0, or the offline in-game message functions), so phase (b) is written against the requests LBP3 really makes. Our loopback and epoll path is untouched.

**Tested offline:**
- `shadnettest` 55/0, in under 3 s:
  - the settings cases;
  - off: no client, no new thread, NP still signed out;
  - a login against an in-process mock shadNet server on 127.0.0.1 (ServerInfo, then Login: npid, password and title "CUSA00473" received; two friends with presence; GetToken and SetClientVersion follow);
  - a refused login (wrong password) gives Failed;
  - `Stop()` while connecting returns at once and leaves no `ShadNet:*` thread.
- `--slow` (56/0, 48 s) also waits out the full give-up on an unreachable server.
- The other 15 offline programs pass (`padtest` needs a controller). The build is clean.

**Not done in phase (a):**
- **The friends list and presence don't reach the game yet.** LBP3 asks for them through NpWebApi requests whose paths we haven't seen; they're logged now. Their answers (and `sceNpLookupNpId` via shadNet `LookupOnlineId`) are phase (b)'s first step.
- **No reconnect after a dropped connection:** the status turns Failed.
- **The in-game gate** with shadNet OFF (Introduction steps ≠ 1 under 1% ×2, Thruster recipe 3/3): waiting for game time; the human was playing.

**The first real-server test needs:**
1. **The user's online name and password:** a shadNet account on `play.lbppc.com`, the same name as their Refresh account.
2. **LBPPC's server accepting this client's version string.** Their shadNet can refuse unknown versions (`clientversions.cfg`, notification 19). If it does, the log says `[shadnet] status: failed - the server rejected this client ...`, and LBPPC would need to allow "LBP3-Recompiled (lbp3-port)".
3. **Running with** `LBP3_SHADNET=1 LBP3_SHADNET_SERVER=play.lbppc.com LBP3_SHADNET_NAME=<name> LBP3_SHADNET_PASSWORD=<pw>` (plus `LBP3_ONLINE=1` and the same name for the game server), then reading the `[shadnet]` lines: the sign-in, the friends, and the WebApi paths the Pod's friends page requests.

## Status: phases (b), (c) and (d) (2026-11-13)

Branch `feat/shadnet2`, from `bestofboth` `481589c` (which already holds phase (a) and the online lane's fixes), worktree `D:\Recomps\lbp3-wt\shadnet2`, shadPS4 junction `D:\Recomps\lbp3-wt\best-shadps4`. Commits: `4b0c7b4` (b), `51a2818` (b3), `30e581c` (c, d). All are pushed to the fork (`rexyness`); nothing is merged. Everything is behind `LBP3_SHADNET=1`.

**Offline-tested only.** No real-server run with this branch yet: the user was playing as Kistopawa, and a second account is pending.

### What changed from the plan

The friend list, presence, sessions and invitations all live on shadNet's own **Web API server**, not in the TCP client:
- LBPPC's server: `https://api.lbppc.com`; any other shadNet serves it on port 31315.
- Source: `github.com/shadps4-emu/shadNet`, `src/webapi`.
- **Why a local friend list isn't enough:** the server's `friendList` answers in-game presence only for friends in the same game. LBPPC's server patch keys that on the title the client signs in with; our `CUSA00473` maps to `NPWR07221_00`. A list built from the login's friends alone can't carry presence or invitations.
- **So:** the game's Web API requests are forwarded to that server with the session token, as LBPPC's shadPS4 does.
- **The local answers stay as the fallback** (no token, server unreachable, `LBP3_ONLINE` only). The local friend list now holds shadNet's real friends instead of an empty list.

### (b) The Web API, push events and invitations (`hle/np_shadnet.cpp`, `net/web_request.*`)

- **Forwarding:**
  - `Authorization: Bearer <GetToken>` goes with every request; the token is never logged (`shadnettest` checks this).
  - The game's method (GET/POST/PUT/DELETE/PATCH), its content type (from the content parameter) and its body are passed on.
  - The real HTTP status and body come back through `GetHttpStatusCode` and `ReadData`.
  - The slash before `?` is dropped (LBPPC: shadNet routes `.../invitations/<id>?fields=` only without it).
  - `LBP3_SHADNET_WEBAPI=<url>|local` overrides the server.
- **Cache:** `profile`, `blockList` and `friendList` GETs are cached for 30, 30 and 5 s. The online runs showed the game asking for its own avatar about once a frame (~4000 requests in k1) while `{}` had no `avatarUrl`.
- **Push events** (shadNet notification 17):
  - queued on the client's thread and delivered to the game's push callbacks from `sceNpCheckCallback`, on the game's thread;
  - matched on service and data type; layouts and matching from LBPPC's GPL-2.0-or-later shadPS4.
- **Other calls:**
  - `sceNpWebApiUtilityParseNpId` decodes a friend's base64 `npId` (`<name>@`).
  - In-game messages are accepted while signed in. Nothing carries them, and the game imports nothing to receive them; LBPPC does the same, because LBP3 waits at "Connecting" if they fail.
- **`/npdata`:** no change. The existing fix-up echoes the friends the game posts, which are now real.
- **Receiving an invitation:**
  - A `sessionInvitation` push is kept.
  - LBP3's own prompt opens the game custom data dialog in mode 2; with shadNet on, our dialog asks "<friend> invited you … Join?".
  - "Join" raises SESSION_INVITATION through `sceSystemServiceGetStatus` (`eventNum`) and `sceSystemServiceReceiveEvent`. Both are registered only with shadNet on; offline the game never sees an event, as before. The event layout is shadPS4's `OrbisNpSessionInvitationEventParam` (176 bytes).
- **Sending an invitation:**
  - **Through the Web API (b):** LBP3's own `POST /v1/sessions/<id>/invitations` is simply forwarded.
  - **Through the dialog's mode 1 (b3):**
    - The mode data is laid out as LBP3 fills it at `eboot.bin+0xadeda0`:
      - texts at +0x00, +0x08 and +0x20;
      - thumbnail and data as pointer/size pairs at +0x38/+0x40 and +0x48/+0x50 (whichever is an image is the thumbnail);
      - `"PS4"` as the one platform;
      - the players the game names at +0x78/+0x80.
    - The dialog copies all of it during Open (it lives on the game's stack).
    - It then asks "Send an invitation to <name>?", or offers a list of shadNet friends when the game names none.
    - It posts `POST /v1/gameCustomData/items` as multipart: the `message` JSON with `toAccountIds` and the item, `game-data-binary-0`, and `game-data-thumbnail-0`.
    - The field meanings come from the call site and haven't been seen in a run, so every Open logs the fields and the raw mode data.

### (c) Peer connections (`net/p2p_link.*`, `hle/net_remote.h`, the signaling section of `np_shadnet.cpp`)

- **One UDP socket per player** (any free port; `LBP3_P2P_PORT` to pin it) carries the handshake, keepalives and the game's P2P datagrams. Our own framing ("L3P1": HELLO, ACK, DATA with the two virtual ports, PING/PONG, BYE).
- **The server's UDP service:** the shadNet server's address with its TCP port + 1 (31314), as LBPPC does; `LBP3_SHADNET_UDP=host:port|off` overrides.
  - **STUN pings** every 5 s: `FF 83 FF FF FF FF`, `01`, the online ID, the LAN IPv4. The reply says where the server sees us, and the server uses that address to answer `RequestSignalingInfos` and to relay.
  - **The relay:** LBPPC's server patch. `"SMRL" 02 <dest IPv4/port> <packet>` comes out at the destination as `"SMRL" 03 <sender IPv4/port> <packet>`, only between endpoints that STUN-ping.
- **A connection:**
  1. `ActivateConnection(peer)` asks the server where the peer is (`RequestSignalingInfos`, cmd 105, added to the vendored client as `lbp3:` code).
  2. It sends HELLO every 500 ms to every address it knows: the server's, and the LAN address the peer reports. After 4 s it also sends through the relay.
  3. The first ACK establishes the connection.
- **Routing and teardown:** traffic goes directly while packets come in directly (within 5 s), and through the relay otherwise; the direct addresses keep being tried. 15 s of silence or a BYE ends a connection.
- **The peer's address, as the game sees it:** the address the server sees for the peer (they report it in HELLO). `GetConnectionStatus` and `GetConnectionInfo(PEER_ADDR)` return it, in network order. If the game sends to that IP on another port and only one peer has that IP, it's still that peer.
- **Signaling events** reach the game's callback from `sceNpCheckCallback`:
  - ESTABLISHED for a connection the game activated;
  - PEER_ACTIVATED, to every context, when another player connects to us (LBPPC found that LBP3's host learns of a joining player only so);
  - MUTUAL_ACTIVATED when both sides activated;
  - DEAD with TIMEOUT, TERMINATED_BY_PEER or PEER_UNREACHABLE.
  - RTT comes from the keepalives. Bandwidth reports 10 Mbit/s: LBPPC found that 0 made LBP3 drop a working connection.
- **The bridge into our socket layer** (`net_offline.cpp`):
  - A `sendto` that the loopback path declined (not 127.x) goes to the link when it's set.
  - The link's datagrams are queued on the socket bound to the destination virtual port (LBP3's P2P socket: type 6, `0.0.0.0:3658` vport 3658), as from the peer's game address and virtual port. `recvfrom` and `sceNetEpollWait` see them as they see loopback datagrams.
  - The loopback path, and with it the Pod's tick, is unchanged: the hook is null unless shadNet is on.

### (d) Join protection

- **Who may connect to us:** a HELLO from a player we didn't activate is answered only if they're a shadNet friend, or named in `LBP3_JOIN_ALLOW` (comma separated, for tests).
- **Anyone else** is dropped and logged once: `[p2p] refused a connection from <name> (not a friend)`. Their side times out (PEER_UNREACHABLE).
- **We can still connect to anyone.**
- **Limit:** the HELLO's online ID isn't authenticated; the server's STUN ping isn't either. Someone who knows a friend's name and the server's address for us could pose as that friend. LBP3's own session join on top (Refresh `/match`) is a second gate. A token handshake would need server support.

### Tests (offline, all pass)

- **`shadnettest` 142/0:**
  - settings, now including the Web API and UDP addresses;
  - against a mock shadNet (token, a `sessionInvitation` push) and a mock Web API on 127.0.0.1: forwarding with the bearer, POST with content type and body, PUT, 404, the cache;
  - local answers with shadNet's friends;
  - push callbacks;
  - the invitation's SESSION_INVITATION event, field by field;
  - `ParseNpId`;
  - a mode 1 send;
  - the token never in the log.
- **`p2ptest` 66/0 (new):** two players in-process with a mock UDP service.
  - Direct connection: events on both sides, datagrams with virtual ports both ways, MUTUAL, RTT.
  - Join protection: Carol refused, Bob never told.
  - Relay only: the server-reported addresses reach nobody, as across two NATs; handshake and datagrams go through `SMRL`.
  - Leaving: BYE, and silence ends it.
  - The bridge: `recvfrom` gets a peer's datagram with its address and virtual port; `sendto` to a peer goes to the link; loopback never does.
- **The rest:** the other 17 programs pass (`padtest` needs a controller).

### Gate (shadNet off, online off; game lock, test saves, a shader-cache copy)

| Run | Exe | Introduction steps ≠ 1 | Thruster recipe | Bunkum | Create sky |
|---|---|---|---|---|---|
| 14:51–15:05 | `4b0c7b4` | run 1 not counted, run 2 0.52% | 3/3 | 16/16 voice lines, Needlepoint loaded | cyan |
| 15:10–15:14 | `51a2818` | 0.00%, 0.13% | | | |
| 15:25–15:43 | `30e581c` | 0.13%, 0.13% (rerun 15:39–15:43) | 3/3 | 16/16, Needlepoint loaded | cyan |

Run 1 wasn't counted because the scripted presses came before the game was up. Its pipeline preload read the cache copy cold, in 48 s instead of 5 s, while another lane was compiling.

After each run: the lock was free, the human's saves were back with no marker or kept copy, and the clone's exe was restored (md5 `95d8af03`).

`30e581c`, first attempts: the 15:16 gate stopped after its Introduction run 1 (0.13%), when another lane started LBP3Launcher.exe. The watchdog now ignores launchers under `lbp3-wt\launcher*` and the job's tmp, as the coordinator allowed. The 15:25 gate's two Introduction runs were spoiled by load from outside the game: one ran 14%, with frame work around 34 ms; the other stalled at boot on a lock the main thread held. The same exe gave 0.13% twice at 15:39, so the table counts those two runs.

### Smoke test on the real server (2026-11-13 15:23, `30e581c`, one instance, as Kistopawa, about 80 s)

Run under the game lock, with test saves and a shader-cache copy. A watchdog would have aborted it if another game or the user's launcher had started. Log: `analysis\shadnet2_smoke\stderr.txt`.

- **Sign-in:** OK (0 friends).
- **STUN:** the reply arrived.
- **Signaling:** a context was created.
- **Answers from `api.lbppc.com`:**
  - `blockList` 200 and `friendList` 200, both empty;
  - the session POST 200 with a `sessionId`;
  - the presence PUTs (`gameStatus`, `gameData`) 204.
- **Thread:** the Web API requests come from the game's `JobManagerWorker2` thread, not the frame thread.
- **The avatar:** the game no longer asks for its own avatar every frame. It now asks every frame for the profiles of two creators shown on Home, Refresh names that shadNet doesn't know (404). The cache answers those, with one server request per name in the run.
- **Screenshots** (`analysis\screenshots\`):
  - `sn2_smoke_home.png` / `sn2_smoke_nav5.png`: Home online, with Community, My Friends, Dive In and Recent Activity available;
  - `sn2_smoke_friends.png`: My Friends opens Social ("Your friends list is empty", Friends online 0).

## Status: two-instance co-op, the drop and the gap-free link (2026-11-13, `feat/shadnet3`)

Two instances on this PC (Kistopawa hosting, Kistopawa2 joining; `coop_*` scripts in the job's tmp),
both signed in to `play.lbppc.com`.

### What works
- Friends with presence, LBP3's own invitation (`POST /v1/sessions/<id>/invitations`, 204), Accept on the
  guest (after the NpId fix `cd0b48c`), the P2P link (direct or through shadNet's relay), both Sackboys in
  the Pod, the world-sync checks identical (`LBP3_SYNC_PROBE`).
- Each player has their own network identity (`0f60ffb`); the link keeps the LAN path when both players
  are behind one public address and never flips paths (`c7a7f3a`).

### The drop (fixed by DATA3, `344e81c`)
- Symptom (v0.1.0, `0f60ffb`): 1.5–4 min after joining, both games stall in 2-s cycles (30 flips with no
  simulation step, then 30 at 2 steps) and the host ends the connection.
- Cause: certain single lost datagrams. After one, LBP3's host stops accepting the other player's traffic,
  which keeps arriving and is read by the game (the bridge delivers everything; checked both ways), and the
  peer is never asked to send again. The host kicks the peer when nothing has been accepted for 10 s
  (`eboot+0x3604a0` → `0x3788a0` → `0x37b3f0`: the constant at `0x1061c4c`; a second test uses 20 s,
  `0xe75844`). The only `sceNpSignalingTerminateConnection` call is in the wrapper at `0x378120`, reached
  from remove-player `0x34e9f0`.
- Evidence: with sequence numbers (`LBP3_P2P_DIAG=1`) D1 and D2 lost nothing and held; D3 lost B→A seq
  11605 and was kicked exactly 10.0 s later. With an induced loss about every 45 s and the gap-free link off
  (T0), the kick came after about 4 minutes (a host→guest loss, the 20-s test).
- Not the cause (checked): CPU load, MXCSR, world divergence, the account id, MAC/IP, our clocks (every
  game-visible clock advances 1.000 s per second within 0.004%, `clocktest`), the per-player join-state
  timeouts (the guest's slot stays in state 1), the socket layer (no lost wakeups, no full queues, nothing
  cut short; the game reads with `MSG_DONTWAIT` into 1600 bytes).

### DATA3: gap-free, in-order delivery (`net/p2p_link.*`)
- Negotiated: HELLO/ACK carry a capability byte and a link epoch (older links ignore the extra bytes and keep
  plain DATA). Both players need v0.1.1 or later for it.
- Each DATA3 carries a sequence number and the previous two payloads; the receiver hands the game datagrams
  strictly in order, none missing and none twice. A gap asks the sender again (NACK, from its last 256); a
  gap open 2 s is skipped and logged; the last datagram goes once more after 100 ms idle.
- Test settings: `LBP3_P2P_TEST_LOSS=N[,burst]`, `LBP3_P2P_TEST_LATE=N`, `LBP3_P2P_TEST_DUP=N` (incoming game
  datagrams); `LBP3_P2P_DATA3=0` turns it off; `LBP3_P2P_PATH=auto|lan|public|relay|any`.
- p2ptest (150 checks): loss, bursts past the copies, late (overtaken), doubled, through the relay, an older
  peer, all both ways at once.
- Acceptance on this PC (`analysis` logs in `D:\Recomps\lbp3-wt\coop\{A,B}\stderr_t*.txt`):
  - T0, gap-free off, a loss every ~45 s: kicked after ~4 min (the control).
  - T1, gap-free, the router's hairpin path, 3 lost in a row every ~45 s: held 10 min; ~42 test drops a
    side, 31 filled from copies, 14 sent again, 0 skipped; no stall (no run of 5 flips without a step).
  - T2, gap-free, the relay forced (RTT ~360 ms), one lost every ~45 s: held 10 min; ~15 drops a side, all
    filled or sent again, 0 skipped; one 5-flip hiccup, no stall.

### Diagnostics added (all off by default)
- `LBP3_P2P_DIAG=1`: DATA2 (sequence and QPC send time) when the peer lacks DATA3; gaps, late, sender pauses,
  one-way delay per 5 s; the last 20 s of game datagrams logged when a connection ends.
- `LBP3_SYNC_PROBE=1`: the world-sync checks and the game's player slots (`[slots]`: state, seconds in it,
  strikes; the server's network settings, mode 4 with timeouts 60/60/30/60/50 ×4 on LBPPC).
- `LBP3_CLOCK_THREADS=1`: which thread reads which clock (the game's network code runs on "game host thread"
  and reads only the monotonic clock). `LBP3_WAIT_LOG=1`: how late timed waits wake.
- `LBP3_P2P_TEST_CONNECT=<name>`: the link connects to that player without the game asking (the join
  protection, live).

### Join protection, live (2026-11-13 22:17)
- Kistopawa3 (no friends) with `LBP3_P2P_TEST_CONNECT=Kistopawa`: the server gave it Kistopawa's address
  (`RequestSignalingInfos` answers non-friends too; worth raising with LBPPC), its HELLOs reached A, and A
  logged `refused a connection from Kistopawa3 (not a friend)`; no connection, "no answer in 30 s" on C.
  In the game itself a non-friend has no way in: invitations are friend-only and `/match` always answers
  the empty 20-byte reply on this server (Dive In never pairs anyone).

### Gate on `344e81c` (shadNet off, online off; 22:17-22:31)
- Introduction steps != 1: 0.00% and 0.13%; Thruster 3/3; Bunkum 16/16 voice lines, no watchdog; sky cyan.
- The `[sim]` lines now carry the time; `sim_steps_level.py`, `intro_stalls.py` and
  `intro_stalls_table.py` accept it (and older logs without it).

### Still to do

1. ~~**One instance, as Kistopawa, against `play.lbppc.com`**~~ (done above; repeat with a friend once there's a second account) (when the game isn't running):
   - the friend list and avatar from `api.lbppc.com`, and that the per-frame avatar requests stop;
   - the session POST's answer;
   - the STUN reply (`[p2p] the server sees us at ...`).
2. ~~**Two instances on this PC**~~ (done above, Kistopawa and Kistopawa2): invite, join, the link direct
   and relayed, 10 minutes with induced losses on both paths with DATA3.
   - Still to see: two PCs on different networks (a real internet path).
   - Dropped (the user, 2026-11-15): no backwards compatibility. Co-op players run the same version, and
     since v0.1.3 an older build is refused at connect anyway (its HELLO lacks the exact-maths bit,
     `p2p_link.cpp` ~529). Mixed-version tests and fallbacks for older builds are out of scope.
3. **From those logs:** correct the mode 1 field meanings if needed, and see which invitation path LBP3 really uses.
4. **Not built:** reconnecting after a dropped shadNet connection; UPnP; voice chat.
