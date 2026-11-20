# LBPPC 0.1.3: what changed and what our port must do

This is a read-only study of LBPPC `aad6a91` (v0.1.2) to `951fb8d` (v0.1.3, 2026-11-13). The clone sits in the job's tmp folder; none of their code was built or run.
Our side is compared at bestofboth `01da20e`, which is the v0.1.0 release.

## Licences of the files studied

| File (at 951fb8d) | Header | Use |
|---|---|---|
| `gpu/bbnet/vendor/shadps4/shadnet/client.cpp`, `client.h` | GPL-2.0-or-later (rpcs3, shadPS4) | reusable with attribution |
| `gpu/bbnet/vendor/shadps4/shadnet/shadnet.proto` | GPL-2.0-or-later (rpcsn, shadNet) | reusable |
| `gpu/bbnet/vendor/shadps4/core/libraries/network/http.cpp`, `netctl.cpp` | GPL-2.0-or-later (shadPS4) | reusable |
| `gpu/bbnet/vendor/shadps4/core/libraries/np/np_handler/np_handler_friends.cpp`, `.h` | GPL-2.0-or-later | reusable |
| `gpu/bbnet/vendor/shadps4/core/libraries/np/signaling_handler.cpp` | GPL-2.0-or-later | reusable |
| `gpu/bbnet/vendor/shadps4/core/net/p2p_codec.cpp`, `p2p_transport.cpp`, `guest_net.cpp` | GPL-2.0-or-later | reusable |
| `gpu/shadps4/video_core/renderer_vulkan/vk_pipeline_cache.cpp` | GPL-2.0-or-later (shadPS4) | reusable |
| `server/linux/**` (patches, README, security scripts), `launcher/`, `scripts/`, `docs/` | no header; the repo's LICENSE is GPL-3.0 | **ideas and protocol facts only** |
| shadNet upstream `src/cmd_account.cpp`, `src/auth_code_store.*` (our `_ref/shadNet` b38cf17) | GPL-2.0-or-later | reusable |

## 1. Sign-in codes: online breaks once LBPPC enforces them

### The protocol, byte-exact

1. **The client asks shadNet for a code.** This is shadNet's existing command `GetAuthorizationCode = 42`.
   - Upstream shadNet already has it (`_ref/shadNet` `src/protocol.h:52`, `src/cmd_account.cpp:390-436`).
   - LBPPC adds it to its client: `client.h:98`, `client.cpp:788-845`, dispatched at `client.cpp:936`.
2. **The request** uses the usual packet header (type u8 = 0 request, command u16 = 42, size u32, packet id u64, all little-endian). The payload is a u32LE blob size followed by:
   ```
   message GetAuthorizationCodeRequest { string client_id = 1; string scope = 2; }   // "lbppc-refresh", "login"
   ```
   The proto is at `shadnet.proto:577`.
3. **The reply** carries the same command and the same packet id. Its payload is an ErrorType u8, then a u32LE blob size, then:
   ```
   message GetAuthorizationCodeReply { string code = 1; uint32 expires_in = 2; }
   ```
   - The code is 6 characters and lives 300 s (`auth_code_store.cpp:12`, `.h:21`).
   - It is single use.
   - Errors:
     - `Unauthorized` if the session isn't registered online yet (client version pending);
     - `TooSoon` if the account holds too many unused codes;
     - `InvalidInput` if `client_id` is empty or over 128 characters, or `scope` is over 1024.
4. **When it's fetched:** once per game login, at the moment the game POSTs `/LITTLEBIGPLANETPS3_XML/login?...`.
   - LBPPC fetches it inside `sceHttpSendRequest` (`http.cpp:2086-2100`) and waits up to 5 s (`np_handler_friends.cpp:122-135`).
   - It is never fetched on the shadNet reader thread, which would deadlock.
5. **How it's presented:** in the login ticket's **serial** field (type 8, length 0x14), as `lbppc-code:` + the code.
   - That's 17 of the 20 bytes, NUL-padded (`http.cpp:466-467`).
   - Without a code the serial stays `lbppc-local-ticket`. Our serial is `lbp3-local-ticket`.
   - Nothing else in the ticket changes: same body, same footer, signature identifier "LBPC", no signature.
6. **The server-side check** is in `server/linux/refresh/lbppc-refresh-security.patch`, which is ideas only. Refresh `AuthenticationEndpoints` reads `ticket.SerialId.TrimEnd('\0')`:
   - **It starts with `lbppc-code:`:** Refresh calls shadNet's `GET /admin/v1/authcodes/redeem?code=&npid=`. The code is valid only if:
     - it was issued to that account's session;
     - its client id is `lbppc-refresh`;
     - it isn't used or expired.

     A bad code is refused, with no fallback.
   - **It doesn't, and `LBPPC_REQUIRE_LOGIN_CODE=1`:** the login is refused.
   - **It doesn't, otherwise:** the old **address check** applies (`/admin/v1/sessions/check`): the account must be signed in to shadNet from the same public IP.
7. **Today:** `refresh-login-check.env.example:9` ships `LBPPC_REQUIRE_LOGIN_CODE=0`. Their release notes say older versions "will stop being able to go online once everyone has had time to move to 0.1.3". So there's a grace period of unknown length.

### Our flow and the gap

- `runtime/src/hle/http_online.cpp:153-209` (`BuildLocalLoginTicket`) writes serial `lbp3-local-ticket`.
  - `Send()` at `:449-453` replaces the game's login body with it.
  - We pass today only through the address check: we sign in to shadNet from the same PC before the game logs in.
- Our shadNet client (`runtime/src/net/shadnet/client.*`) has no command 42 and no proto messages for it.
- **Consequence:** the day play.lbppc.com sets `LBPPC_REQUIRE_LOGIN_CODE=1`, every v0.1.0 login is refused. The game shows the "(403)" dialog and online is gone.
- Also, with shadNet off (`LBP3_SHADNET` unset but online on), the address check already fails: the server sees no shadNet session.

### What to change (v0.1.1, high priority)

1. `runtime/src/net/shadnet/shadnet.proto`: add the two messages, field numbers as above, from shadNet's GPL-2.0-or-later proto.
2. `client.h`/`client.cpp`:
   - add `CommandType::GetAuthorizationCode = 42`;
   - add `std::optional<std::string> RequestAuthorizationCode(client_id, scope, timeout)`. Register a waiter by packet id before sending, then wait on a condition variable. The reply handler parses ErrorType, then the blob, then the proto. Return empty on the reader thread, when not authenticated, on an error, or on a timeout.
   - LBPPC's implementation (`client.cpp:788-845`) is GPL-2.0-or-later and may be adapted with attribution.
3. `net/shadnet_service`: add `std::optional<std::string> LoginCode()`, which calls `RequestAuthorizationCode("lbppc-refresh", "login", 5 s)` while signed in.
4. `hle/http_online.cpp`:
   - `BuildLocalLoginTicket(name, code)`: serial = `"lbppc-code:" + code` if there is one, else the current `lbp3-local-ticket`.
   - Fetch a **fresh** code for every `/login?` POST (single use, 300 s).
   - Log `[online] login: ... with a shadNet code` or `... no shadNet code (not signed in to shadNet)`.
5. The launcher and `play_best.bat` already turn shadNet on with online. The launcher's Online check should warn that online without shadNet won't sign in.
6. Tests:
   - `shadnettest`: the mock answers command 42 with a code, and the client returns it; check the error and timeout paths.
   - `onlinetest`: the ticket serial bytes with and without a code; the 0x14 length is kept.
   - **Real check:** one sign-in as Kistopawa, with `/login` 200 and the log saying "with a shadNet code". This can't prove enforcement (the server is still at `=0`), but a malformed code would be refused, so a 200 with a code is meaningful.
7. Ship it promptly as v0.1.1 through the updater, so friends move before LBPPC flips the switch.

## 2. A verified connection to the level server

- **LBPPC before 0.1.3:** the game loads its own CA list for Sony, and shadPS4 has no libSceSsl. That turned verification **off** for every request, LBPPC's own servers included.
- **LBPPC 0.1.3** (`http.cpp:1050-1075`): every host except Sony's (`*.playstation.net|.com`, `*.scee.com`, `*.sonyentertainmentnetwork.com`) is verified against Windows' trusted roots with Windows' chain check. This is normal validation, no pinning. `BB_DISABLE_HTTPS=1` still turns it off for test servers.
- **Ours:**
  - `hle/http_online.cpp` (`WinHttpOpen` `:87`, `WinHttpOpenRequest(..., WINHTTP_FLAG_SECURE)` `:462`) and `net/web_request.cpp` (`:20`, `:68`) use **WinHTTP with default security flags**. No `WINHTTP_OPTION_SECURITY_FLAGS` or `SECURITY_FLAG_IGNORE_*` appears anywhere in runtime/ or launcher/.
  - So we already verify play.lbppc.com and api.lbppc.com fully: the Windows chain engine, its own root update, name and date checks.
  - **Nothing to change.** It's worth one test against a host with a bad certificate (e.g. `expired.badssl.com`, through a test-only server override) to prove refusal, but only with the user's OK to contact a public test host. Otherwise a local test server with a self-signed certificate does it.

## 3. "Connecting 30 s faster"

- **LBPPC's cause:** `sceNetCtlGetNatInfo` left `nat_type = 0` ("not determined yet"). LBP3 reads it once after logging in to the game server (game `0x33d710`) and waits until 30 s pass while it is 0.
- **LBPPC's fix:** `nat_type = 2` when nothing determined it (`netctl.cpp:288-294`).
- **Ours:** we already answer `stunStatus 2, natType 2` when online (`runtime/src/hle/np_offline.cpp:456-457` at 01da20e). feat/shadnet3 0f60ffb fills in the STUN-mapped address.
- **Log check** (analysis\play_best_log.txt, Kistopawa): `GET /announce -> 200`, then at once `[np] sceNetCtlGetNatInfo: STUN OK, NAT type 2`, then `GET /lbp/network_settings.nws` on the next line. **No 30 s wait. Nothing to change.**

## 4. The certificates the game carries

- **LBPPC 0.1.3**, in its shadNet client (`client.cpp:78-160`), loads three sets of trusted roots:
  - Windows ROOT+CA certificates, skipping expired ones (the 0.1.2 fix for the expired cross-signed ISRG Root X2);
  - **two bundled Let's Encrypt roots** (ISRG Root X1, valid to 2035, and X2, to 2040);
  - `BB_SHADNET_CA`.

  Peer and host are still verified.
- **Ours** (`runtime/src/net/shadnet/client.cpp:91-122`, `:146-157`): it skips expired Windows certificates, as theirs does, with `SSL_VERIFY_PEER` and `SSL_set1_host`. But we **don't bundle the LE roots**.
  - **Gap:** OpenSSL only sees roots already in the Windows store. Windows adds roots lazily, when a CryptoAPI chain build needs one, which OpenSSL never triggers. So a fresh PC that never visited a Let's Encrypt site can lack ISRG Root X1. shadNet's TLS then fails ("unable to get local issuer certificate"), shadNet sign-in fails, and the game shows 403.
  - This affects the game and the launcher (launcher_net uses the same client).
  - **Fix:** add the two ISRG roots (public certificates, PEM from letsencrypt.org/certificates; data, not code) to `LoadTrustedRoots`.
- **The HTTPS paths (Refresh, the Web API)** use WinHTTP, so the Windows chain engine fetches missing roots on demand. Equivalent; nothing to change.

## 5. Background shader builds (`BB_ASYNC_PIPELINES`, on by default in 0.1.3)

- **The outline** (`vk_pipeline_cache.cpp:285-320`, `:403-412`, `:600-640`; GPL-2.0-or-later, shadPS4 with lbppc changes):
  - A new graphics pipeline is created "deferred" and queued to 1–4 worker threads (hardware threads / 4, clamped). `GetGraphicsPipeline` returns `nullptr`, so the draw is skipped until it's built.
  - Finished builds are picked up at the next lookup (`FinishAsyncBuilds`).
  - Effect: no first-sight hitch (their worst case went from 4.5 s to 0.3 s). New things appear about a second late.
  - Their launcher shows "Shaders: preparing/building N of M" progress.
- **Fit with ours:** we have parallel preload at boot, the persistent driver VkPipelineCache, the shared 1440p cache and the warm-up tour. The remaining first-visit pauses are what this would remove. Risks to check before taking it:
  - a skipped draw in a target that is **accumulated** rather than redrawn each frame, which leaves lasting artifacts;
  - frame generation's motion vectors and HUD-less hook (a skipped draw in the velocity target means wrong warps for those frames);
  - the 1440p rescaled targets.
- **Recommendation:** port it as an opt-in setting (default off), measure on the Introduction hitch list and Diner, then decide the default.

## 6. Co-op transport

- **LBPPC's P2P transport** (`core/net/p2p_codec.cpp`, `p2p_transport.cpp`, GPL-2.0-or-later):
  - a framing codec (marker, flags, src/dst vport, communication id), direct UDP, or shadNet's **SMRL relay** on UDP 31314 (`p2p_transport.cpp:277-280`);
  - **no sequence numbers, retransmits or ordering**;
  - their own comment at `p2p_transport.cpp:389`: "a dropped game message can split a co-op session".

  So LBPPC has the same weakness we found (a single lost datagram means the host stops accepting the guest, and the 10 s kick follows). Our planned DATA3 reliability layer (sequence, redundancy, NACK, negotiated) has no equivalent there.
- **Signaling timers** (`signaling_handler.cpp:38-44`): Matching2 timeout 30 s, handshake connect timeout 30 s, **relay after 4 s** without an answer (same as ours), handshake retransmit every 500 ms, keepalive every 45 s.
- **The known issue "returning to your pod with a friend can occasionally leave you both stuck":** "We know what triggers it" (docs/releases/0.1.3.md:32). The trigger isn't described in the repo, the issues (#2–#6, none about co-op) or the docs. A dropped datagram on the way back is a plausible match for our finding, but unconfirmed.
- **Already matched on our side:** the co-op guest's `/match` 401 from Refresh is answered as a signed success (LBPPC docs/online.md:80; ours `http_online.cpp:255`). The 16:9 world step doesn't apply: we always render 16:9.

## Other server-side changes that touch our launcher

From `lbppc-shadnet-security.patch` and `lbppc-refresh-security.patch` (ideas only):

- **Refresh `/api/v3/register` now requires that a shadNet account with that name exists** and that the name isn't on a blocked list. Otherwise it answers "Accounts are made in the LBPPC launcher...".
  - Our order (shadNet Create first, then Refresh register, then the PATCH) already satisfies this, both in the launcher and in `tools/create_online_account.py`.
- **shadNet Create:**
  - blocked names give `InvalidInput (3)`;
  - a password under 8 characters gives `3`;
  - **3 new accounts per address per 24 h**, then `TooSoon (4)`.
- **shadNet Login:** after too many wrong passwords (10 per account per address, 20 per address, 50 per account in 15 minutes), `TooSoon (4)` for up to 15 minutes.
- **Our launcher's messages** (`launcher/launcher_net.cpp:298-303`, `launcher_friends.cpp:97-121`):
  - Create 3: "didn't accept the name, password or e-mail". Should add "or the name isn't allowed".
  - Create 4: "too many attempts; try again in a minute". Should be "too many new accounts from this network today; try again tomorrow".
  - Login 4 has no specific message. It should be "too many wrong passwords: sign-in is locked for up to 15 minutes".
  - Our generated passwords are random and longer than 8, but a typed password needs an 8-character minimum.
- **A risk to keep in mind:** shadNet supports client-version rules (`ClientVersionRulesFile`). A session whose version is pending can't get codes or show online. We report `LBP3-Recompiled (lbp3-port)` (`client.cpp:788`). If LBPPC enables version rules, our port could be held out. Asking the LBPPC team to allow that string is the user's call.

## Plan, in order

1. **v0.1.1, must-have:**
   - the sign-in code (section 1);
   - the bundled ISRG roots for shadNet TLS (section 4);
   - the launcher's error messages and the password minimum.

   Small, shadNet-only changes, testable offline with the mock and once for real.
2. **With them, if ready:** the DATA3 co-op link (in progress on feat/shadnet3).
3. **Later, opt-in:** background pipeline builds (section 5).
