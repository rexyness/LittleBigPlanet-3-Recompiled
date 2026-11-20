# LBPPC's online path, compared with ours (2026-11-13)

This is a read-only study of LBPPC at `aad6a91`, the latest (there's nothing newer upstream; PR #1 is only a RecompLauncher badge). Theirs is cited as `<file>:<line>`, relative to `D:\Recomps\lbp3-wt\_ref\LBPPC`. Ours is the shadnet worktree `D:\Recomps\lbp3-wt\shadnet` at `bec42d8`, which includes the online lane's fixes: the template User-Agent, signed fix-ups using the request's MM_AUTH cookie, no Accept-Encoding, and the resolver answering like DNS.

The symptoms on our side:
- **(A)** "403" at boot;
- **(B)** logins succeed but the game stays offline (fixed by `bec42d8` for Kistopawa);
- **(C)** Community, My Friends, Recent Activity and Store crossed out in the Pod computer (Dive In works since `bec42d8`);
- **(D)** "Play Online" crossed out in the pause menu (Plus fixed in `23e2b44`).

## Ranked gaps (most likely first)

1. **The user's shadNet password is refused.**
   - Run r9: `[shadnet] status: failed - login refused (error 8)`. Error 8 is `LoginInvalidPassword` (`gpu/bbnet/vendor/shadps4/shadnet/client.h:156`; `scripts/online_accounts.py:158`, "Wrong password.").
   - Without a shadNet session the public server answers `/login` with 403, so this IS the "403 at boot" (A) for the user's account.
   - The user needs the right password; tests use Kistopawa meanwhile.
   - **Resolved 2026-11-14:** the user's working password went into our `user\config\online_account.txt`, and a shadNet sign-in as the user now works.
   - Explains (A).
2. **PSN Web API: ours is a log-only stub; theirs answers.**
   - **Theirs:**
     - with shadNet, every Web API call goes to shadNet's Web API server (`BB_SHADNET_WEBAPI`, `https://api.lbppc.com` for the official server; `scripts/online_accounts.py`'s OFFICIAL, `launcher/lbppc_launcher.py:1822`, `np_web_api_internal.cpp:780`);
     - signed in locally, `http.cpp:494-516` (AnswerLocalWebApi) answers it: `friendList` and `blockList` as empty lists, everything else `{}`, status 200;
     - a path's trailing slash before its query is dropped (`include/core/emulator_settings.h` GetShadNetWebApiUrl: "LBP3 reads an invitation at …/invitations/<id>/?fields=…").
   - **Ours** (`runtime/src/hle/np_shadnet.cpp:82-170`): every Web API call returns 0. CreateContext and CreateHandle return id 0, and CreateRequest never writes `*requestId`. SendRequest, ReadData and GetHttpStatusCode return nothing; the push-event filters return id 0.
   - **Observed (r8):** the game initialised Web API, made 3 contexts and 3 handles, a presence push filter and 3 service push filters (gameCustomData, inGamePresence, sessionInvitation), then never made a single CreateRequest. Most likely the zero ids, or the empty results, made it give up on PSN features.
   - **Most likely explains (C):** My Friends and Recent Activity (both PSN features), and possibly Community's gate.
   - **Fix:** positive ids. Real request objects whose HTTP status is set, with data that ReadData can read. Route them to `https://api.lbppc.com` with the shadNet session (as LBPPC does), or at least answer locally with AnswerLocalWebApi's bodies.
3. **The resolver: URLs must resolve to their host.**
   - **Theirs:** `core/libraries/net/net_resolver.cpp:68-75`: "Refresh's network_settings.nws gives its config URL as CDNHostName and TelemetryServer, which LBP3 looks up as names for its server ping ("https://play.lbppc.com/lbp"). A URL resolves its host."
   - **Ours:** `runtime/src/hle/net_offline.cpp:371-393` (since `bec42d8`): anything with `/` or `:` FAILS at once.
   - **Observed (r8):** `[net] resolve https://play.lbppc.com/lbp` ×4, then `posix socket(65) family 2 type 3 proto 1` (a raw ICMP socket) and `posix sendto(65) to 0.0.0.0:0 … -> unreachable` ×24.
   - So the game's server ping goes to 0.0.0.0 and fails.
   - **Likely explains (C)** for Community (the server "unreachable"), and maybe (D).
   - **Fix:** strip `scheme://` and anything from the first `/`, `:`, `?` or `#`, then resolve normally: for play.lbppc.com that's DNS.
4. **The ICMP ping itself.**
   - **Theirs:** raw sockets go to the host OS (`net_translate.cpp:70-72`, `net.cpp:2245-2262`).
   - **Ours:** `sendto` on anything that isn't loopback fails "unreachable" (`net_offline.cpp`), and `sceNetConnect` and `sceNetSend` always do (`:282-291`).
   - **Fix:** for a raw ICMP socket (type 3, proto 1) to a non-loopback address, send the echo with Windows' `IcmpSendEcho2`, which needs no admin rights unlike a raw socket, and queue the echo reply for the game's `recvfrom`. Keep the loopback tick path as it is.
   - Probably needed together with gap 3 for (C) Community.
5. **sceNetCtlGetNatInfo.**
   - **Theirs:** `core/libraries/network/netctl.cpp:270-288` answers it: stun_status 1 with the mapped address when shadNet knows the external IP; 0 or 2 otherwise, with type 2 when signed in locally.
   - **Ours:** `runtime/src/hle/np_offline.cpp:395` always returns NOT_CONNECTED (0x80412108).
   - **Could explain (D) Play Online** and the matchmaking readiness. Dive In works without it, so it's lower than gaps 2–4.
   - **Fix:** online, answer stun_status 2, nat_type 2, mapped_addr the LAN IP; later shadNet's external IP.
6. **Zero ids elsewhere:** `sceNpLookupCreateTitleCtx` and `sceNpSignalingCreateContext` (ctxId never written) in `np_shadnet.cpp:175-208`. The InGameMessage handles return 0.
   - **Theirs:** shadPS4's real implementations, ids ≥ 1.
   - A zero or unwritten id can make the game treat the library as failed.
   - Part of (C)/(D) (friends lookups, invites).
7. **The NP sign-in event's timing.**
   - **Theirs:** `core/libraries/np/np_manager.cpp:1009-1016` re-queues SignedIn when the game registers its state callback after startup: "Without it LBP3 never learns the player's online ID; its parental-controls check then fails and joining an invitation stops at the 'user-generated content' notice."
   - **Ours:** `np_offline.cpp:72-82` queues user 1 on registration, which is fine.
   - **Check:** when shadNet signs in AFTER the callback registers (a slow TLS login), does the game hear a SignedIn then? `np_shadnet` passes a late answer on "as a new notice"; confirm in a log.
   - Low.
8. **Parental controls for OTHER players.**
   - **Theirs:** `np_manager.cpp:323-333`: a player not on this PC gets age 18 and no restrictions. "LBP3's parental-controls check takes a failure as a restriction (its 'user-generated content' notice, then no join)."
   - **Ours:** `np_offline.cpp:245-250` answers only for a signed-in local user (age now 18 per `bec42d8`).
   - **Check:** the online-id argument. Answer every id (age 18) rather than SignedOut.
   - Affects co-op joins later. Low for (C).
9. **Client version.**
   - **Theirs:** `run.py:233-234`: `BB_CLIENT_VERSION="LBPPC <version>"`, "What the server's clientversions.cfg checks (it can turn old LBPPC builds away)".
   - **Ours:** "LBP3-Recompiled (lbp3-port)", which play.lbppc.com currently accepts (Kistopawa signed in).
   - If LBPPC tightens clientversions.cfg, our client could be refused (shadNet error), and they'd need to allow it.
   - Not a current cause.
10. **Downloaded-asset safety** (not a symptom; a security gap).
    - **Theirs:** `http.cpp:867-930`: every `GET …/r/<sha1>` download (levels, objects, photos, textures) must hash to its name, and must be a type the game itself makes. Scripts or "Dangerous/Modded" assets are refused, because "on PC the game's code is not in the PS4's sandbox".
    - **Ours:** missing; we pass whatever the server sends.
    - Worth porting before playing community levels widely. The file is GPL-2.0-or-later (shadPS4-derived), so it's reusable with credit.

Not a gap: Store is crossed out in LBPPC too (no PS Store / sceNpCommerce behind it). Expected.

## The full checklist

### 1. HTTP (libSceHttp)
| Item | LBPPC | Ours | Notes |
|---|---|---|---|
| User-Agent | template UA, or a libhttp-style one (`http.cpp:1298-1304`, `:2046-2052`) | template UA (`http_online.cpp:397`) since `bec42d8` | fixed the eula loop |
| Host header | the game's Host is dropped; the connection's own is sent (`:1066-1076`: a proxy picking the site by Host dropped the connection) | dropped (`http_online.cpp:386`) | ok |
| Cookie | the game's own Cookie header passes through; no jar | the game's passes; a jar for Set-Cookie (`:307-320`) | the real server sets no cookie; ok |
| Accept-Encoding | gzip added unless the game set it; httplib inflates (`:1078-1080`) | not forwarded since `bec42d8` (WinHTTP's decompression gave empty bodies) | ok |
| Request signing (X-Digest-B) | the game signs with its patched key (4 eboot edits) | same 4 edits (`core/patches.cpp`, checked) | ok |
| Reply signatures (X-Digest-A) | the game checks; fix-ups are signed over body + MM_AUTH + path (`:1953-1984`) | same (`http_online.cpp:211-235`), using the request's cookie since `bec42d8` | ok |
| 410 farc_hashes/t_conf → signed empty 200 | `:1876-1899` | `:242-244` | ok |
| /npdata empty → the friends posted | `:1901-1931` | `:245-251` | ok (friends come from shadNet; ours has 0 friends) |
| The guest's /match 401 → signed success | `:1933-1951` | `:252-255` | ok |
| Login body → a local PS3-style ticket | `:435-492`, `:2064-2072` (serial "lbppc-local-ticket") | `http_online.cpp:176-192` (serial "lbp3-local-ticket") | the public server also needs the live shadNet session (gap 1) |
| Host overrides | `run.py:246-253`: littlebigplanetps3.online.scee.com and res0..resf.* → the server; https → port 443 (`:425-427`) | every request's scheme, host and port become the server's | ok |
| TLS roots | system stores; 0.1.2 fixed an expired root that Windows keeps | WinHTTP, Windows store | watch for that 0.1.2 issue on other PCs |
| Downloaded-asset checks | `:867-930` | missing | gap 10 |
| Offline transport failure | `:2075-2085` (resolver ENODNS) | n/a | — |
| Diagnostics | `LBPPC_HTTP_TRACE=1` (`:2093-2104`) | `LBP3_ONLINE_LOG=1` (`bec42d8`) | ok |

### 2. DNS and resolver
| Item | LBPPC | Ours |
|---|---|---|
| URL as a name | resolves its host (`net_resolver.cpp:68-75`) | fails (`net_offline.cpp:378-392`): gap 3 |
| The game's own host | via host overrides plus DNS | `*.scee.com` → the server IP; ok |
| Other names | DNS (`getaddrinfo`) | DNS (`online::ResolveIPv4`); ok |
| localhost | loopback | n/a |

### 3. NP
| Item | LBPPC | Ours |
|---|---|---|
| Sign-in state | shadNet or local; SignedIn re-queued on late callback registration (`np_manager.cpp:1009-1016`) | state 2 once shadNet is signed in; user 1 pending at registration (`np_offline.cpp:72-82`); check the late case (gap 7) |
| Online ID / account ID | shadNet NPID or local; account id from the name (`np_handler.cpp:129-170`) | shadNet name; FNV in the ticket; ok |
| Authorization code | "AUTHEN" (`np_auth.cpp:124-125`); the HTTP layer replaces the login body with the ticket | the login body is replaced; the auth code is whatever ours returns; fine |
| Plus | shadPS4 answers authorized | authorized when online (`23e2b44`) |
| Parental controls | others: age 18 (`np_manager.cpp:323-333`) | local user only (gap 8) |
| Date of birth / language | shadPS4 | adult / en when signed in (`bec42d8`) |
| Web API | shadNet webapi or local answers | stub (gap 2) |
| Lookup / Signaling / InGameMessage | shadPS4 implementations | zero ids (gap 6) |
| Bandwidth test | finishes at once, 100 Mbps (`np_utility.cpp:459-480`) | finishes at once, 20/100 Mbps (`np_offline.cpp:407-432`); ok |
| Commerce / Store | none (Store crossed out there too) | none; fine |
| Signaling bandwidth | non-zero (`np_signaling_state.cpp:404`: 0 bps made LBP3 drop a working connection) | not reached yet (co-op) |
| Join consent | friends, LBPPC_JOIN_ALLOW, or ones you joined (`signaling_handler.cpp:974-1040`) | not yet (shadNet phase d) |

### 4. NetCtl
| Item | LBPPC | Ours |
|---|---|---|
| State | connected when BB_ONLINE (`netctl.cpp:165-183`) | IP obtained when online (`np_offline.cpp:377-382`); ok |
| Info | real IP and MAC | fake MAC, IP 192.168.1.50, link up (`:384-393`); ok |
| NAT info | answered (`netctl.cpp:270-288`) | NOT_CONNECTED (`:395`): gap 5 |
| Events | net_ctl_obj connection events (`net_ctl_obj.cpp:49-61`) | callback registered, never fires (`:397-403`); ok while the state never changes |

### 5. Eboot patches touching online
- The 4 digest sites: ours checks the original bytes (`core/patches.cpp`). Same as theirs.
- The connect-timeout skip at eboot+0x360b5b: ours (upstream patch 2). LBPPC doesn't need it (real network).
- LBPPC's 3,373 exact-math sites (`scripts/cpu_compat.py`, `src/probe.c`): co-op between Intel and AMD only (shadNet phase e).
- The language unlock: `0x2a0706`, in our bestofboth UX lane. No online effect.

### 6. shadNet glue
- `bbnet_glue.cpp` installs the shadNet client and the network kernel hooks (online state, the P2P port, UPnP forwarding, the public address; `net_kernel.cpp:84-110`).
- `run.py` sets `BB_P2P_PORT=0` (LBP3 binds 3658 itself; signaling takes another port) and `BB_UPNP=1`.
- Ours: phase (a) client only; no P2P transport or UPnP yet (phases b–c).

### 7. Startup ordering
- LBPPC raises SignedIn at startup, and again when the callback registers.
- The friends list and NPID come from shadNet before the game's first NP check.
- Ours waits up to 20 s for the shadNet login before answering the first sign-in check, and passes a late answer on as a notice. Fine, if gap 7's case holds.

### 8. Their docs and release notes
- `docs/online.md:75-82`: the list of online changes; all have equivalents here except the Web API (gap 2) and the asset check (gap 10).
- 0.1.0: online, Refresh plus shadNet; co-op with relay; join protection; friends in the launcher and the Pod.
- 0.1.1: the friends list stuck on "Signing in..." when the server was slow; online co-op off for copies with a separate `Sc0`.
- 0.1.2: certificates on every PC: an expired root copy in Windows broke shadNet TLS and the friends list.
