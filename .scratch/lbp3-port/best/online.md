# Online play (opt-in), branch `feat/online`

Status: **game-server client only** (the LittleBigPlanet server: profiles, levels, comments). No PSN stand-in yet (shadNet: friends, invites, online co-op signalling); see "Not done". Off by default, and there's no server address built in.

## Turning it on

Three settings, in `play.bat` style (or as environment variables):

```
set LBP3_ONLINE=1
set LBP3_ONLINE_SERVER=http://<host>:10061      (or https://<host> for a server behind HTTPS)
set LBP3_ONLINE_NAME=<your online name>         (3-16 letters, digits, - or _)
```

With all three set, the game:
- signs in to "PSN" locally under that name;
- sends its server requests to that address instead of Sony's dead `littlebigplanetps3.online.scee.com:10061`, keeping the paths (`/LITTLEBIGPLANETPS3_XML/...`);
- signs them with the community servers' key.

Without `LBP3_ONLINE=1` and a server, nothing changes: the game plays offline exactly as before.

## Which server

That's your choice; the port never connects anywhere on its own.
- **LBPPC's public server** (`https://play.lbppc.com`): it's their service, so please ask them before pointing a different client at it. Their server expects accounts made through their tools (Refresh and shadNet with the same name).
- **Self-hosted Refresh** (https://github.com/LittleBigRefresh/Refresh, 3.7.2 or later, with PostgreSQL), as LBPPC's `docs/online.md` "Hosting" describes:
  - `UseTicketVerification false`: the port's login ticket isn't signed by Sony.
  - `EnforcePatchwork false`.
  - `ListenHost 0.0.0.0` if friends connect.
  - Then `LBP3_ONLINE_SERVER=http://127.0.0.1:10061` on the same PC.
  - Hosting it means downloading and running their server, which is your call.

## What the port does (hle/online.*, hle/http_online.cpp, core/patches.cpp)

- **Requests:** libSceHttp requests go out over WinHTTP. HTTPS uses Windows' own certificate store.
- **Login:** it carries a local PS3-style NP ticket, as LBPPC does.
- **Endpoints Refresh doesn't serve the PS4 game:** `/farc_hashes` and `/t_conf` 410 → an empty signed 200; `/npdata`'s empty reply → the friends the game posted; a co-op guest's `/match` 401 → a signed success.
- **Signing key:** `CustomServerDigest`, as 4 byte edits, each checked against the original bytes first.
- **Name resolution:** every name the game resolves points at the server.
- **The game's simulation tick is untouched:** the loopback network the Pod's local session runs on (vport 3658) stays our own code.

Credit: the approach, the ticket layout, the signed answers and the key sites come from LBPPC (emurphy04, https://github.com/emurphy04/LBPPC, `aad6a91`). The ticket and answer code is adapted from their GPL-2.0-or-later `http.cpp`.

## Tested (2026-11-13, 06:35)

- **Offline:**
  - The branch builds, and the new `onlinetest` passes 10/10: the ticket layout, HMAC-SHA1 against RFC 2202, and online staying off without a server.
  - The localhost mock server (`tools/mock_lbp_server.py`) answers the login with an MM_AUTH cookie, 410 for `farc_hashes` and `t_conf`, and a level list, all signed.
- **In game, online ON against the localhost mock** (06:18–06:25, test saves, a shader-cache copy, the 165 Hz display; game log `tmp\online\on2_stderr_live.txt`, mock log `tmp\mock2.log`):
  - **Sign-in:** the signing key was patched at all 4 sites, and NP reported "signed in" (state 2).
  - **Login:** the game POSTed `/LITTLEBIGPLANETPS3_XML/login?applicationID=21414&...&titleID=CUSA00473_00&...&client=lbp3ps4` with the 248-byte local ticket, got 200, and from then on sent the MM_AUTH cookie.
  - **Start-up calls**, all signed with CustomServerDigest: `/eula`, `/announce`, `/farc_hashes` (the mock's 410 became an empty signed 200, as designed), `/match` room updates (UpdateMyPlayerData, UpdatePlayersInRoom), `/showModerated`, `/updateUser` (with the player's DLC list), `/user/MockPlayer/playlists`, `/promotions`, and, once the Pod computer was opened, `/searches`.
  - **The game accepted the mock's signed replies:** no error dialog, no hang. It reached the Pod and the Pod computer's Home menu.
  - **Community, My Friends, Dive In, Recent Activity and Store were greyed out on Home against the mock.** The mock answers `/searches` and most calls with empty bodies, so a real Refresh server's answers are probably what enable Community. That's untested, since no real server was used.
  - **Side effect:** the system's update check (`a01.cdn.update.playstation.net`) resolves to the configured server too, so the server gets one harmless `GET /`. Follow-up: redirect only the game's own hosts.
  - An earlier run (03:46) hit the same mock but was cut off by a usage limit.
- **In game, online OFF (the merge gate), 06:25–06:34:** this branch's exe was swapped into the clone under the game lock and put back by md5 (`d5d335ad…`).
  - Introduction steps ≠ 1: 0.52% and 0.00%, both under 1%.
  - Thruster recipe: 3 of 3 OK.
  - The online code doesn't disturb the tick.
- **Evidence:** `D:\Recomps\lbp3-recompiled\analysis\online\` (the mock's request log, the online run's game log, the regression results), and the screenshots `analysis\screenshots\online_on2_now.png` (the Pod) and `online_on2_pc.png` (the Pod computer's Home).
- **Verdict:** merge-ready with the default off.

## Not done

- **shadNet** (PSN stand-in): friends lists from the server, invites, joining friends, online co-op signalling and relay. LBPPC vendors shadPS4's whole network stack for this (about 130 files). Our runtime's own network layer drives the game's tick, so it needs careful glue, not a drop-in: several sessions.
- **Lockstep exact-math patches** for online co-op between Intel and AMD CPUs.
- **Join protection.** Without shadNet there's no one to join, so it isn't needed yet.
- **Publishing levels and leaderboards.** Not done in LBPPC either.

## Real server: play.lbppc.com (2026-11-13 morning)

- **Accounts:** the human chose the online name **Kistopawa**, and LBPPC agreed to our port using their server. Both accounts were made with our `tools/create_online_account.py`: a shadNet Create over TLS to shadnet.lbppc.com:31313, then Refresh register plus allowing game logins. The password is in `D:\Recomps\lbp3-recompiled\user\config\online_account.txt`, local only.
- **Without shadNet:** the game-server client alone (`play_best.bat`, ONLINE=1) got **403** on `/login`. The account was fine: psn/rpcn auth allowed, not banned, email verified. LBPPC's own code says the unsigned local ticket is for private servers with verification off, and "public servers will need a PS4 login of their own".
- **With shadNet:** `feat/shadnet` `4d4ace8`, `D:\Recomps\lbp3-wt\shadnet\play_shadnet.bat` (play_best's settings plus `LBP3_SHADNET=1`, server play.lbppc.com).
  - shadNet signed in over TLS as Kistopawa (0 friends), and its client string "LBP3-Recompiled (lbp3-port)" was accepted.
  - `/login` then answered **200**, followed by `/eula`, `/showModerated`, `/updateUser`, `/promotions` and `/user/Kistopawa/playlists`.
  - So the public server requires a live shadNet session for the same name.
- **Next:**
  - shadNet phase (b): invites and joining;
  - the shadNet-off in-game gate (interrupted when the human wanted to play);
  - merging feat/shadnet into bestofboth.

## On the real server, play.lbppc.com (2026-11-13, 12:00-12:33)

These are short sessions on test saves, with a copy of the best build's shader cache, as Kistopawa. LBPPC agreed to our use of their server. Verbose log: `LBP3_ONLINE_LOG=1`. All fixes are on `feat/shadnet` `bec42d8`.

| Run | Change | What the game did |
|---|---|---|
| r1 | verbose log + the template's User-Agent | login -> eula -> announce -> network_settings -> farc_hashes (410) -> re-login loop. Before this (the user's sessions) it looped right after eula. |
| r2 | signed answers use the request's MM_AUTH cookie | farc_hashes and t_conf fixed up and accepted -> match, showModerated, updateUser, playlists, promotions, searches; then searches came back empty -> re-login |
| r3 | no Accept-Encoding forwarded (uncompressed replies) | searches 5568 bytes, ChallengeConfig.xml 6779 bytes, no re-login. **Dive In is available on the Pod computer.** Community, My Friends, Recent Activity and Store stay crossed out. |
| r4-r6 | parental age 18; account date of birth and language answered; resolver like DNS | no change to Community. The game never calls the account or parental functions. |
| r7 | without shadNet | **the "403" dialog**: the game's own "A problem occurred when attempting to connect to the LittleBigPlanet servers (403)". The server refuses /login when the account has no shadNet session. That's what the user saw from `play_best.bat`, which doesn't run shadNet. Screenshot `analysis\screenshots\rs_r7_40.png`. |
| r8 | experiment: the PS4 UGC flag (`IsPS4UGCRestricted__` -> net manager+0x211) forced to "not restricted" | Community still crossed out, so that flag isn't the gate. Experiment removed. |
| r9 | as the user's account | **shadNet login refused, error 8 = LoginInvalidPassword**, so /login got 403. Not retried. |

**Causes found:**
1. **No User-Agent:** WinHTTP's own agent went out instead of the game's `MM CHTTPClient LBP3 01.26`, and the server's replies didn't satisfy the game, which logged in again after /eula.
2. **The wrong cookie:** our signed answers for the 410 endpoints used the runtime's own cookie jar. It was empty, because the real server sends no Set-Cookie; the game sends MM_AUTH itself from the login body. The game took the bad signature as a failed connection.
3. **Compression:** the game asks for gzip, which libSceHttp would inflate for it, so compressed replies reached it raw (/searches, 1566 bytes) or, through WinHTTP's decompression, empty.

**Still open:** Community, My Friends, Recent Activity and Store are crossed out.
- My Friends and Recent Activity are PSN Web API features (friends list, activity feed). The game calls sceNpWebApiCreateContext but never CreateRequest, and our shadNet stand-ins answer every Web API call with 0 without filling anything in.
- Community needs more work. It isn't the UGC flag, the parental age or the account answers.
- Next leads:
  - the Home menu's own conditions (the script natives it calls while open; the native counting in r9 was lost to the refused login);
  - answering the Web API like LBPPC's shadPS4 stack does (`AnswerLocalWebApi`: empty friend and block lists, `{}` for the rest).

## The gap list implemented: the whole Pod computer online (2026-11-13, 12:50-13:10)

`feat/shadnet` `24e244c` (+ `9d5feb0`: a recvfrom diagnostic removed again), as Kistopawa (name and password from `user\config\online_account_kistopawa.txt`, the password passed only through the environment; `online_account.txt` stays the user's). Same rules: game lock, test saves, a copy of the best build's shader cache, the 165 Hz display, short runs.

**What changed** (the research/lbppc_online_deep.md gaps):
1. **PSN Web API answered locally** (`np_shadnet.cpp`, adapted from LBPPC's `AnswerLocalWebApi`, GPL-2.0-or-later):
   - positive ids for everything (library context, user context, handle, push filter, callback);
   - request ids `(userCtx << 32) | n` (shadPS4's shape); sends succeed with 200;
   - `friendList` and `blockList` come back as empty lists, everything else as `{}`;
   - `ReadData` returns the bytes copied.
   - It's registered online (`LBP3_ONLINE` or `LBP3_SHADNET`).
   - Before this, `sceNpWebApiInitialize` returned 0 and the game passed `libCtxId=0` back, and it never made a request.
   - Now it asks for its block list and friend list (`userProfile`), creates a session (`sessionInvitation /v1/sessions/`, multipart), posts its presence (`gameStatus "Pod 1/4"`, `gameData`), and reads its avatar URL.
2. **The server ping:**
   - A looked-up URL resolves its host (`https://play.lbppc.com/lbp` -> 179.236.82.47, as LBPPC's resolver does).
   - The game's raw ICMP echo (socket type 3, proto 1; 8-byte requests from 3 call sites per sequence number) goes out through `IcmpSendEcho2` on its own thread. The reply is queued on the game's socket as a raw socket delivers it: a 20-byte IPv4 header plus the echo reply with the game's id and sequence, 28 bytes.
   - The game reads them with `recvfrom` (polling, flags 0). Replies came back in ~176 ms.
   - Online only, raw ICMP sockets only, and only after the loopback path has declined the packet.
3. **Smaller items:**
   - `sceNetCtlGetNatInfo` online: STUN OK, NAT type 2, the LAN address `sceNetCtlGetInfo` reports.
   - NpLookup title contexts and requests, and signaling contexts and connections, get ids from 1; a lookup's NpId is the online ID.
   - The late-sign-in re-report was already there (the shadNet status listener queues a new notice). It is now logged, and it fired in every run: `[shadnet] signed in: the sign-in state is reported to the game again`.
4. **Download check** (ported from LBPPC's `http.cpp`, GPL-2.0-or-later, attributed): every `GET .../r/<sha1>` answered 200 must hash to its name and be a type the game itself shares (LVL PLN REC PRF QST ADS ADC CHK VOP PTG TEX MAT BEV, photos, ghosts, grief reports). Scripts (FSH), graphics materials (GMT) and mod-only types are refused as a 404.
   - `onlinetest` has 10 new checks: accepted, refused, and untouched cases. 20/20 pass.
   - In k3 two real Community downloads (23503 and 19642 bytes) passed.

**Runs:**

| Run | Build | Result |
|---|---|---|
| k1 | `24e244c` | **Home: Community, My Friends, Dive In, Recent Activity and Store all available** (none crossed out; Store too, though nothing stands behind it: sceNpCommerce is missing, so opening it is untested). `analysis\screenshots\rs_k1_110.png`, `rs_k1_130.png`. |
| k3 | `24e244c`, then Community opened | **Community works:** Recommended / Genres, Team Picks "Forested Ruins" by IOQINION with its icon (`/s/user/...`, `/genres`, `/users`, 2 `/r/` downloads passed the check). `rs_k3_170.png`. |
| k2noping | ablation: the echo send disabled (24 sends "unreachable") | **Still all available.** So the ping is not the gate. The Web API answers, the NAT info and the ids (items 1 and 3, together) un-crossed the tiles. The ping now works as on a console (the game measures the server), but Home doesn't depend on it. `rs_k2noping_130.png`. |

**Online OFF (regression), 13:11-13:20:** this branch's exe swapped into the clone under the lock and put back by md5 (`95d8af03…`). Introduction steps ≠ 1: 0.78% and 0.00% (both under 1%); Thruster recipe 3 of 3 OK. Saves equal `human_play_20261113_1153` afterwards.

**Save incident (contained, one short session's save lost):** the first k2noping attempt ran the ablation exe under another name, `lbp3_noping.exe`.
- **What went wrong:** the run script watched only `lbp3.exe`, so it thought the game had exited after 10 s. It put the human's saves back at 13:02:49 and released the lock while the game was still booting.
- **What the game did:** it loaded the human's little save, then wrote `LBPxSAVExLITTLE/00000001/littlefart1743` at 13:03:14. It was killed at 13:03:22. Nothing else was touched (guests too).
- **What I restored, and what that lost:** I put `LBPxSAVExLITTLE` back from `analysis\savedata_backups\human_play_20261113_1153` (littlefart1741). I found out later that the human's save at that point was littlefart1742, not 1741:
  - the launcher log shows the user ran the game at 12:34:24-12:34:46 (22 s, the `best` build, as the user's account);
  - that session loaded 1741 and wrote 1742;
  - the orphan's 1743 is numbered from it (the game numbers loaded + 1).
  So **that 22-second session's save (a boot-time re-save) was lost.** Its successor as the orphan wrote it is kept in `analysis\savedata_backups\orphan_write_20261113_1303`.
- **What the user played afterwards:** the user ran the game again at 13:22:37-13:23:02 (the user's account, `best` build, login 403). It loaded the restored 1741 and wrote a new 1742. That is the live save now, backed up as `analysis\savedata_backups\human_play_20261113_1330`, the new reference.
- **Fix:** the script now treats any `lbp3*` process as the game. If one survives the kill, it keeps the lock and doesn't touch the saves. The ablation was rerun as `build\noping\lbp3.exe`.
- The user is active again (12:34 and 13:22), so no more test sessions were started after m4.

**Online without shadNet (`LBP3_ONLINE=1` only, against the localhost mock), m3/m4, 13:23-13:29:** the Web API handlers are now registered there too.
- **m3 crashed at 90 s** in the game's JSON parser: `strlen(NULL)` (eboot 0x28ec0), called from 0x4f8aa0, which parses the `/update_my_pins` reply's "progress".
  - The mock answered `/update_my_pins` with an empty body.
  - A real server (play.lbppc.com) answers with the pins JSON (225 bytes in k1).
  - The game only calls `/update_my_pins` once its PSN side works, so the mock had never been asked before.
  - Fixed in the mock (`tools/mock_lbp_server.py`: it returns the posted pins).
- **m4 (`9d5feb0`): no crash in 150 s.**
  - Web API answered (14 sends).
  - Home: Community, My Friends and Recent Activity available. Dive In is crossed out, because the mock's /match answers are empty. `rs_m4_130.png`.
  - The mock's empty `/user/MockPlayer` makes the game ask again (383 times); that's the mock's limitation.
- **Note:** a server whose `network_settings.nws` names no CDN host (the mock) leaves the game pinging its default, `a01.cdn.update.playstation.net`. That's an echo to Sony's CDN, as on a console. Refresh's settings name its own host (play.lbppc.com).

Logs (no password; MM_AUTH redacted): `D:\Recomps\lbp3-recompiled\analysis\online\k1_stderr.txt`, `k3_stderr.txt`, `k2noping_stderr.txt`.

**Still open:**
- shadNet's real friends in the Web API: the friend list is still empty locally (phase b; LBPPC routes it to `api.lbppc.com`).
- Peer connections: signaling connections stay inactive (phase c).
- In-game messages: still the offline answers.
- `sceKernelClose` on a socket returns EBADF (the ICMP socket is never freed). Harmless: one socket per session.
