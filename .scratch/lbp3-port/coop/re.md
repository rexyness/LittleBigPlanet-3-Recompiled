# Local co-op: what LBP3 does when users 2–4 log in (static analysis)

Ticket [13](../issues/13-local-co-op.md). From the eboot (US 01.28) with Ghidra (`tools/ghidra_decomp.sh`) and two quick scanners (direct callers of the import thunks, approximate RIP-relative data refs). Nothing was run. 2026-11-11.

## The flow in one paragraph

The game keeps its own table of local users and their pads, fed only by `sceUserServiceGetEvent`. A **login event opens that user's pad at once** (no "press to join" at this level). The game's pad thread then polls every opened pad about every 12 ms. A pad that is connected or has been pressed becomes a live pad. A "new controller" screen in the game's scripts (`ModalOverlayStateNewController`) is how a player is then added. Separately, when the users change, the main loop rebuilds a list of up to 4 local users from `sceUserServiceGetLoginUserIdList`, with each user's name and NpId. A logout closes the pads and frees the slot.

## Functions

| Where | What it does |
|---|---|
| `eboot+0x638d0` | Pad init, once: `scePadInit`, initial user → `0x1060a84` (via `0x6ffb0`, `sceUserServiceGetInitialUser`), drains the user events (`0x63a20`), starts the pad thread `0x65940` on a 12 ms kernel timer (`sceKernelAddTimerEvent` 0x2ee0 µs). |
| `eboot+0x65940` | The pad thread, every ~12 ms: `0x63a20` (user events), then `0x64f10` (poll all pads). |
| `eboot+0x63a20` | Drains `sceUserServiceGetEvent` until `0x80960007` (no event). **Login (type 0):** the initial user gets entry 0 (`0x1060a90`: user, standard handle, special handle); any other user takes the first free of 6 more entries (`0x1060a9c`, 12 bytes each, user −1 = free). Each opens **two** pads: `scePadOpen(user, 0, 0)` (standard) and `scePadOpen(user, 2, 0)` (special); then calls `0x3ba260(netmgr, user, -1)`. **Logout (type 1):** `scePadClose` on both handles and frees the entry. Either sets the "users changed" flag `0x1060aec`. Unknown users in a logout are ignored. A 7th extra user would be dropped silently. |
| `eboot+0x64f10` | Polls the 7 entries: `scePadReadState` on the standard handle if ≥ 0, otherwise on the special one if ≥ 0; converts the state into the game's pad slot `i` (`0x129f0d4 + i*0x710`); `scePadGetControllerInformation` on the standard handle; marks the pad's first press in `0x129f098[i]`. A negative handle is simply skipped. |
| `eboot+0x63c00` | Per frame, 7 pads: copies the polled state into the game's input structures; a pad counts as live when connected (flags & 6) or pressed once. |
| `eboot+0x63840` | Light bar: `scePadSetLightBar` with each channel clamped to at least 13. |
| `eboot+0x70000` | Main loop (`eboot+0xa100`), when `0x1060aec` is set: rebuilds the local user list (`0x12a2330`, 4 × 0x278 bytes) from `sceUserServiceGetLoginUserIdList` (4 entries, −1 = none). For each new user: `sceUserServiceGetUserName(user, buf, 17)` and `sceNpGetNpId(user, …)` (failure leaves the NpId zero). Users no longer listed are removed. `0x12a2d10` = count, `0x12a2d14` = index of the initial user. Deferred while some network object's +0x30 is set. |
| `eboot+0x3ba260` / `0x3ba3d0` | On login, only in network state 3: `sceNpGetNpId(user)`. A valid id goes to `0x3ba3d0`, which makes an account-info record keyed by NpId (`sceNpGetAccountCountry`, `…DateOfBirth`, `…AccountId`: age and content checks). An empty id parks the user in a 4-entry retry list. This is not the session join. |
| `eboot+0x3f55c0` | Audio thread: for each logged-in user (login list, 4 entries) opens a VOICE port, `sceAudioOutOpen(user, 2, 0, len, 48000, 3)`, closes it when the user leaves. Users 2–4 add a port each (the runtime has 32). |
| `eboot+0x3f5ef0` | Voice chat thread: per logged-in user, `sceVoice*` ports (online voice chat). |
| `eboot+0x62860` | PS Move: `sceMoveOpen(user, 0, 0)` for each logged-in user, retried while it fails; logs "Move cannot be opened". Out of scope; harmless. |
| `eboot+0x4bdd30` | `sceImeKeyboardOpen` for the initial user only. |
| `eboot+0x52220` | Save-data path: mounts are per save slot, not per local user; the login list is only consulted after one mount error. No sign that users 2–4 get saves of their own. |

Script natives near the join (strings): `ModalOverlayStateNewController`, `GetModalOverlayNewControllerPadIndex`, `GetModalOverlayNewControllerPlayerNumber`, `IsModalOverlayNewControllerPadConnected`, `OnModalOverlayNewControllerScreenDone(bool,bool,bool)`, `PressedStartButton`, `MOEP_NEW_CONTROLLER_6AXIS` / `_MOVE`, `E_MESSAGE_TYPE_ASSIGN_CONTROLLER_REQUESTED`, `E_MESSAGE_TYPE_SET_NUM_PLAYERS`, `TIMEOUT_WAIT_FOR_ADD_PLAYERS_TO_TAKE`. So a new pad raises a "new controller" event and the scripts show a screen that assigns it a player number. The local session then adds players by message (upstream's `NOTES.md`: the Pod runs as a loopback session with 4 player slots, 0xb0 each, keyed by player ID).

## What the runtime must do

1. **(Must)** `sceUserServiceGetEvent`: a login event `{0, user}` when a controller takes slot N ≥ 1 (user N+1), a logout `{1, user}` when it goes; any thread, polled about every 12 ms from the game's pad thread. Keep the existing one-time login for user 1. `0x80960007` when empty (already so).
2. **(Must)** `sceUserServiceGetLoginUserIdList`: the logged-in users, 4 entries, −1 for empty, user 1 always present. It must agree with the events at the time the game reads it (`0x70000` runs on the main loop after the flag).
3. **(Must)** `scePadOpen(user, 0, 0)` for users 2–4 must succeed and give a handle that reads that player's controller. `scePadOpen(user, 2, 0)` may fail (it does for user 1 today: ALREADY_OPENED); a negative handle is skipped. `scePadClose` on a negative handle must just return an error. `scePadReadState` for an unplugged pad: return OK with `connected = false`, as now.
4. **(Must)** `sceUserServiceGetUserName(user, buf, 17)`: a name of at most 16 characters for users 2–4 ("Player 2"…), NUL-terminated within 17 bytes. Truncate a long Windows user name for user 1 too.
5. **(Should, decide by test)** NP identity of users 2–4. Today `sceNpGetNpId` / `sceNpGetOnlineId` ignore the user and answer "OfflinePlayer" for everyone, so every local user would get player 1's identity in the local user list and anywhere the session keys players by ID. Two options:
   - (a) Console-faithful: users 2–4 get `SIGNED_OUT` (0x80550006) from GetNpId/GetOnlineId, as a signed-out PS4 user would. The user list then holds a zero NpId for them, and `0x3ba260` parks them. User 1 keeps "OfflinePlayer", which upstream needed for its own session check (`0x36c150`).
   - (b) A distinct placeholder per user ("OfflinePlayer2"…).
   Start with (a). If player 2's pad works in the Pod but the player never appears, or a level hangs loading with two players (watch for the `0x36c150` "is the local player in the session?" 60 s timeout, error 0x6b3bbc4e), try (b).
6. **(Should)** `sceAudioOutOpen(user, 2 /*VOICE*/, …)` for users 2–4 must succeed (or fail cleanly); a port each, silent. Fine with 32 ports; rate-limit underrun logs per port.
7. **(Nice)** Light bar: the game sets each player's colour via `scePadSetLightBar` on that user's handle; the runtime already routes it by slot.
8. **(Not needed)** Saves, IME keyboard and Move per user: the game doesn't use them for users 2–4 (saves and the keyboard belong to the initial user; Move is out of scope).

## Not established

- Exactly what makes the "new controller" screen appear (pad connected vs. a press, and in which game states: Pod, story, Create), and whether a joined player needs Start/Options. Script code in the game's .farc files, not the eboot, decides it. The first in-game test with a second pad will show it.
- How the local session adds players 2–4 and whether duplicate IDs break it (option 5a/b above).
- Whether anything limits co-op in story mode (no such check was seen in the code read here).

## Correction (2026-11-11, from the game)

"Not needed: saves" was wrong. In the game, player 2 mounts and creates its own `LBPxSAVE` and `LBPxSAVExLITTLE` (15 mounts in one run, with set param and save icon): each local user has their own profile, as on a PS4. The runtime's per-user save path (`user\savedata\user<N>\`) handles it. See ticket 13.
