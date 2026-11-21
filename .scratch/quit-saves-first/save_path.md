# LBP3 (CUSA00473 v1.28): how the game saves the profile, and how Quit can ask it to

All addresses are eboot module offsets: code from Ghidra and objdump, data the same, so
`core::EbootBase() + off` works for both. The vtables were found by parsing the
R_X86_64_RELATIVE relocations in the dynlib segment of `ghidra/eboot_etexec.elf`. Helper
scripts and the raw Ghidra outputs were in a scratch folder and weren't kept; rerun the probes
(`.scratch/lbp3-port/tools/ghidra_probe.sh`) to see them again. The fix built on this is on
`feat/quit-saves-first` (`runtime/src/hle/quit.cpp`); it uses the "B" request below, not the quit
flag.

Tags: **[V]** means verified in the decompiled code or the disassembly. **[I]** means
inferred: consistent with the evidence but not proven. **[L]** means it needs a live run.

## 0. Correcting the premise: the 13:42 session never left the Pod

`analysis/play_log.txt` (13:42–13:44) has no level load. The Pod music (`i_pod3.fsb`) keeps
streaming up to the quit at line 2627.

- The save at lines 1689–1702 (`bigfart54`, then `littlefart1928`) is followed immediately by
  `POST /stream` (line ~1710), then `/searches`, `/s/user/...` and `/users?u=...`. That traffic
  is the Pod computer's community pages, so the save coincides with **opening the Pod computer
  (or something done in it)**, not with entering a level.
- About 10 s (≈300 frames) after that first save comes `[p2p] connecting to <another player>`,
  and 30 s later `no answer in 30 s`. That was probably the user's "went into a level": an
  attempt to join someone that never completed. No level load follows it. What triggered the
  save is [L].
- `littlefart1929` (lines 1951–1955) comes about 6 s (≈185 frames) later, during the same
  browsing.
- Both sessions also save `littlefart` once at boot: play_log.txt lines 1004–1019
  (`littlefart1927`) and play_log.1.txt lines 1004–1015 (`littlefart1926`).
- play_log.1.txt (13:38–13:39) writes nothing between that boot save and the quit (line 1784).
- The file mtimes the user quoted (13:43:33 and 13:43:39) can't be checked any more. Every file
  under `user/savedata/CUSA00473` now shows 13:47:31 (they were copied or restored at 13:47).
  **Unverified.**

## 1. Who writes the profile (walking up from the imports)

| Layer | Address | What it does | Tag |
|---|---|---|---|
| import | e0ecc0 `sceSaveDataMount`, e0ec90 `Umount`, e0ed50 `SetParam`, e0ed40 `SaveIcon` | | V |
| mount with retry | `FUN_00051500` | Retries while `DAT_012a2308` is set. Gives up if `DAT_0118ff38` (quit committed) is set. Offers a backup restore. | V |
| Mount(user, mode, dirName, out) | `FUN_00051890` | Changes '_' to 'x' in the name (`LBP_SAVE` becomes `LBPxSAVE`). Keeps 8 ref-counted mount slots at `011dafa0`. | V |
| Umount | `FUN_00050fe0` | | V |
| fart file open/close | `FUN_004ef820` / `FUN_004ef670` | `FUN_004ef710` picks the directory: `littlefart` goes to LBPxSAVExLITTLE, `playfart_a_b` to LBPxSAVExAxB, anything else to LBPxSAVE. | V |
| save op base (CSaveGameOp) | ctor `FUN_004f2cd0`, vtable `01079e50` | Slots: +0x10 Run (job), +0x18 Finish, +0x50 OnQueued, +0x58 Begin, +0x60 Update(poll), +0x68 priority | V |
| StartOp | `FUN_004f2d30` | Submits `op->Run` to the job manager `DAT_01190e28` with prio 700 and name "SaveGameOp". `DAT_01190e28` is the "JobManager" (4 workers; `FUN_000ab8d0`), so Run executes on a **JobManagerWorker** thread. | V |
| **profile save op** | ctor `FUN_009393c0(op, user, &res, flag, 0)`, vtable `010b26a0` | Begin `FUN_00939780` and prepare `FUN_009395f0` run on the main thread. Run `FUN_00939a30` runs on the worker: mount, write the fart, read it back to verify, rotate versions, write ICON0.PNG for "little"-type saves. Finish `FUN_0093a180` runs on the main thread. | V |
| other op classes | vtables 01079cd0/d50/dd0/ed0/f50, 010ace90/f10/fa0, 010b2620 | Load, import and level saves. Their Run functions are 4f0d60, 4f2040, 90a570, 9c710, 908e60, 9096d0, 90c4d0, 97e490. | V (roles I) |
| SaveGameUtil singleton | `DAT_013a4e50` (0xb0 bytes, created by `FUN_004f2eb0`) | +0 recursive mutex, +0x28 op array, **+0x30 queue count**, +0x34 capacity, +0x39 "saving suppressed" byte (set while the save-data dialog is broken) | V (+0x39 meaning I) |
| Enqueue | `FUN_004f3380(util, op)` | Inserts by priority, then calls `op->OnQueued` | V |
| Pump | `FUN_004f37b0(util)` | Handles the head op only. State 0: Begin. State 1: poll. State 3: pop it and post message 0x48. | V |
| **Update(isPodMask, autosave)** | `FUN_004f3620` | Pumps the queue. If `autosave` is set and the game is not quitting, it calls QueueProfileSaveIfDirty for local users 0–3 with mask `arg \| 2`. | V |
| **QueueProfileSaveIfDirty** | `FUN_004f3140(util, slot, user, mask, maskIfBit8, flag, little, big)` | For the local profile (`*slot`) and for its big profile (`*(local+0x6a8)`), queues a profile save op if `(flags & 4) == 0` and `flags & mask` is nonzero. The local profile is queued first. | V |
| **FlushAllSaves** | `FUN_004f3a70` | Blocking. Uses re-entry guard `DAT_013a4e58`. For local users 0–3 it calls QueueProfileSaveIfDirty(mask 0xc1, alt 0, little=1, big=1), then loops `pump; sleep 5` until the queue count is 0. | V |
| ProcessDirty | `FUN_004f30c0` (script native `ProcessDirty__` at 0x8230e0, via jmp 7d9070) | For the 7 profile slots: if `flags & 0xc1`, set `flags \|= 2`. It does this for the local profile and its big profile. | V |

### Profile slots and data globals

- Profile slots are at `013a49f0 + i*0x10` for i = 0..6: `{int32 userId; Resource* localProfile at +8}` [V].
- The local profile is type 0x20, written to `littlefart` in LBPxSAVExLITTLE. Its big profile
  (`local+0x6a8`) is type 0x1c, written to `bigfart` in LBPxSAVE. [I] This uses the LBP
  resource-type numbering; Run's "type 0x20 goes to LITTLE + ICON0.PNG" branch is [V].
- The Pod level is stored in the local profile as a descriptor at +0x4e8..+0x503 (hash and
  type 9). The string "PodLevel" is a serialised field name. [V for the writes, I for the field
  name]
- RGame is `*DAT_012ecc80`. Its +0x4f5 byte is `is_pod`, set in `FUN_0025c1f0` with the debug
  variable "RGAME_is_pod" [V]. Its +0x70 is the current level.

### Resource flags byte at res+0x48 (all writers found)

| Bit | Meaning | Tag |
|---|---|---|
| 0x01 | profile changed (many `or ...,1` sites) | V |
| 0x40 | **Pod changed**. `FUN_002601b0` and `FUN_00269390` set it on the local profile, guarded by "in the Pod, own Pod, loaded". The guard is V. That placing a sticker reaches these setters is **L**: their callers (6e4fb0, 733ee0, 75af50, 75cc20, 75d8a0, 79bfc0, b81fb0) weren't followed. | V/L |
| 0x80 | another dirty kind | I |
| 0x02 | **save requested**. Set by ProcessDirty and by Begin's retry path. Cleared at enqueue: OnQueued `FUN_009395c0` calls `res->vtbl+0x78`, which is `FUN_0023e600`: `flags = (flags & 0xf9) \| 4`. | V |
| 0x04 | **save in flight**. Set at enqueue. Cleared together with 0x01 and 0x80 by `FUN_0023e620` (`res+0x90`, ok=1) after a successful serialisation. | V |
| 0x08 | Set together with 0x01 whenever a local-profile save op ends with status ≠ 0: Finish calls `res+0x80(res, 0, x)`, which is `FUN_00292420` and does `\|= 9`. That covers the status-2 retry as well as write errors. A profile with this bit saves only on an explicit 0x02 request, and FlushAllSaves (alt mask 0) skips it. | V (meaning I) |

Also on the local profile: **`+0x6c0` means "save the big profile first"**.

- `FUN_002908e0` (attach the big profile and migrate entries into it) sets it to 1 and marks the
  big profile 0x02.
- The big profile's `+0x80` method (`FUN_0023fb50`, ok=1) clears it, and so does the reset in
  `FUN_00280320`.
- While it is set, Begin(local) takes status 2 (see §5.2). This is how the game forces the
  big-then-little order.
- It is set only after a load-time migration, so it is rare. [V for the writes, I for how often]

## 2. What triggers the save, and the Pod-specific part

### Per frame, on the main thread

`main()` is `FUN_00000890`. Its loop runs `while (!IsQuitting) (*DAT_010cdd00)()`, where the
frame function is `FUN_00005b90` (the table at 0106bc50 maps "Pod"/"YellowHead" to 5b90). That
calls `FUN_00009d70`, which calls `Update(RGame->is_pod, 1)` at **b6e4** and the present
`FUN_0005caf0` (which sets "GFX_frame_num", `DAT_01202b54`) at b73f and bb09. [V]

The autosave mask is therefore `is_pod | 2`: 0x03 in the Pod and 0x02 elsewhere.

- **Pod edits set only 0x40**, which neither mask includes. That is why a sticker never saves
  on its own while you stay in the Pod.
- It saves only after something calls ProcessDirty (mask 0xc1 includes 0x40, so it sets 0x02).
  The next frame's Update then queues the saves. [V]

### Committing the Pod into the profile

`FUN_00260210(RGame)` [V] does this only if all of these hold:

- you are in the Pod, and it is your own Pod (`FUN_0025b7e0`);
- the level is loaded (state 4 or 5);
- the local profile is loaded;
- the local profile has flag 0x40.

When they do, it:

1. serialises the live Pod level (`FUN_007bcef0`) into a new level resource and hashes it
   (`FUN_0023cc40`);
2. stores the descriptor at local+0x4e8 if it changed;
3. sets `flags = (flags & ~0x41) | 1`.

**The profile save op calls it itself.** `FUN_009395f0` (called from Begin `FUN_00939780`)
starts with `if (RGame) FUN_00260210();` and then serialises the resource [V]. So any profile
save that runs while you are in your loaded Pod with 0x40 set picks up the sticker. Nothing has
to "leave the Pod" first.

The exit path `FUN_0025f290` (leave level, at 25f3f4) does the same commit inline and then
calls ProcessDirty [V].

### Callers of ProcessDirty [V]

| Caller | Context |
|---|---|
| `FUN_0025f290` | leave level |
| `FUN_002639a0` | slot/level switch. It also snapshots the level. |
| `FUN_00878230` | level-change commit, called per frame from 9e5c. It calls ProcessDirty only when `is_pod`. |
| `FUN_00879db0` | start/go to a level |
| `FUN_00350a90` | |
| `FUN_004cacf0` | boot profile load; this is the boot-time save |
| script native `ProcessDirty__` | |

Which of these fired at Pod-computer open is **[L]**.

### Threads

- The pump, Begin and serialisation, Finish, and the Pod commit run on the **main thread**
  (`main`, the frame loop) [V via the call chain].
- Run (the mount and file writes) runs on a **JobManagerWorker** thread [V that it is the job
  manager; thread identity is L].

## 3. How the runtime can ask for "save now"

### A. Recommended: use the engine's own quit (the game services it at a safe point)

- `DAT_0118ff39` = quit requested.
- `DAT_0118ff38` = quit committed. The game sets it **right after FlushAllSaves returns**.
- `DAT_0118ff40` = request time.
- Setter: `FUN_000aabe0(char req, byte b)`, which is
  `if(!ff39 && req) ff40 = clock(); ff39 = req; ff3a |= b;`.
- `IsQuitting` is `FUN_00013ef0` and returns `ff38 || ff39` [V].

The end-of-frame check `if (ff39 && !ff38) { FlushAllSaves(); ff38 = 1; }` appears in
`FUN_000aac60`, `FUN_000aacc0`, `FUN_000ab6c0` and `FUN_000aaee0` [V].

- `FUN_000aacc0` is called inside the present `FUN_0005caf0` (at 5d1c3), and it acts when
  `DAT_012c8fe1 == 0`, which is the case outside init. That the call is unconditional is [I]:
  only about 25 instructions of a 1.7 KB function were read.
- The fallback is [V]: `main()` leaves its loop on IsQuitting and calls `FUN_000ab6c0(0)`, which
  runs the same flush.
- So after the store, the **next present on the main thread** flushes every local user's
  profile with mask 0xc1. That covers 0x40, so Begin commits the Pod first.
- While quitting, Begin skips its deferral checks (`if (!IsQuitting && ...)`).
- `main()` then leaves its loop and calls `FUN_000ab6c0(0)`. The flush is skipped because ff38
  is already set; it then runs every init step's shutdown callback in reverse, then
  `FUN_00012080`.

Runtime sketch, in place of the `AnySaveMounted` wait in `quit.cpp`:

```
*(u8*)(base+0x118ff39) = 1;
// optional: *(u64*)(base+0x118ff40) = <nonzero time>, or call FUN_000aabe0(1,0) (PS4_SYSV_ABI)
wait until *(u8*)(base+0x118ff38) == 1 && !AnySaveMounted()   // keep the 15 s timeout
then SDL quit / _Exit
```

**`ff38` means only that the flush loop ended, not that anything was saved.** After it, check
the outcome:

- for every slot with a nonzero `local`, both `local+0x48` and `*(local+0x6a8)+0x48` should
  have `(flags & 0xc5) == 0`;
- otherwise log "profile not saved" with the flag bits.

Cases where A finishes without saving the local profile:

- `local+0x6c0` set (Begin status 2: the big profile saves, the local one is left with
  0x02|0x08|0x01, and nothing re-queues it because autosave is off while quitting);
- a pending reboot (status 3);
- `util+0x39`;
- 0x08 left over from an earlier failure.

**Most robust option: B, then A.** Request ProcessDirty first and wait up to about 5 s for the B
predicate below. That way the game's own per-frame loop handles the big-then-little ordering and
the retries. Then set `ff39` for a final forced flush, which also covers anything Begin kept
deferring, and exit at `ff38` once nothing is mounted.

The store is a plain byte write and is safe from any thread. The overlay menu calls RequestQuit
from the UI thread, and that is fine. The overlay doesn't pause the game thread; it only gives
the game a neutral pad.

### B. Alternative: ProcessDirty, the same request the game itself makes

1. On the **game thread**, call `FUN_004f30c0()` (no arguments). For example, call it inside
   the `Translate__i` hook when the pause-menu Quit is confirmed; natives run in script
   context, exactly as `ProcessDirty__` does.
2. From another thread, the only option is a `lock or byte [res+0x48], 2` on each slot
   (`local` and `local+0x6a8`) whose `flags & 0xc1` is nonzero. That can lose a race with the
   game's own non-atomic read-modify-writes of that byte. The risk is small, but not zero.
3. The next frame's Update queues and runs the saves asynchronously, and the game keeps
   rendering meanwhile.

Done when all of these hold:

- the frame counter `DAT_01202b54` has advanced by at least 2 since the request;
- the queue is empty: `*(u32*)(*(u64*)(base+0x13a4e50)+0x30) == 0`;
- every loaded slot has `(local.flags & 6) == 0` and `(big.flags & 6) == 0`;
- `!AnySaveMounted()`.

If right after step 1 no slot has 0x02, nothing is dirty and Quit can close immediately.

### Not recommended

- Calling `FlushAllSaves`, `FUN_00260210`, `FUN_004f3140` or the op functions from a runtime
  thread. They serialise live level and profile state on the caller's thread and race the game
  thread.
- Calling `FlushAllSaves` from inside the Translate hook. It would block mid-script and
  serialise the level from within a script native.

## 4. Completion signal; levels and create mode

**Completion:**

- With A: `ff38 == 1` (set after the queue drains, which is after Run's umount) **and** no
  `/savedataN` mounted [V for the order].
- With B: the four-part predicate above.
- In both cases ops are popped only after Finish, which comes after the job completes, so "queue
  empty" includes the file write [V].

**In a level:**

- The commit is a no-op outside the Pod, and the save covers only the local and big profiles.
- ProcessDirty plus Update with mask 0x02 is exactly what the game does at level transitions, so
  this is safe [I].
- **Create mode:** the level being edited is *not* part of these saves. Level saves are separate
  ops ("SaveLevel", `RequestSaveLevel__`). Quit would still lose unsaved create work. This is
  the same as the PS4 [I].
- **Adventure progress:** `playfart` (type 0x3f) is not in the 7 slots, and FlushAllSaves never
  touches it. Whether the game saves adventure progress elsewhere is **[L]**.

## 5. What makes a forced save dangerous or ineffective

1. **Mid-transition.** If RGame+0x70 (the level) is not in the loaded state, the Pod commit
   silently does nothing. That only matters if the Pod is being unloaded; the exit path will
   already have committed it.
2. **Begin's non-save exits** in `FUN_00939780`, for the local profile only:
   - If `local+0x6c0` is set, status 2: Finish runs and **re-arms 0x02 on both profiles** each
     time, so under B it can loop.
   - The "player busy" check uses `FUN_007037c0` (component +0x27c, or +0x278 equal to 3 or 4).
     It **defers indefinitely, with no counter**.
   - If the player has no thing or component, it defers for up to 200 frames
     (`op+0x11c`, about 6.7 s at 30 fps).
   - **Only the two deferrals are skipped while quitting.** The `+0x6c0` test (status 2) and
     the reboot test come *before* the IsQuitting check. Under A, status 2 is a lost save (see
     §3 A).
   - If `DAT_012c8712` (reboot or user switch pending) is set and the op is for the primary
     user, the op aborts (status 3).
3. **Saving suppressed.** With `util+0x39` set (after a broken-save dialog), or flag 0x08 on a
   profile (last save failed) under A, nothing is saved, but `ff38` still becomes 1. Log a
   warning if the queue never became non-empty.
4. **Under A,** `IsQuitting` turns true for the rest of the frame; many waits and loops bail
   out (`FUN_00248fc0`, `FUN_00236120`, ...). FlushAllSaves blocks the main thread, and the
   profile op's +0x30 is `ret 0`, so no "saving" frames are drawn: the window can look frozen
   for about 1 s.
5. **After `ff38`,** the game thread runs the engine's shutdown steps. A retail PS4 build has
   probably never executed them, so they may crash or hang under the HLE. Exit right after `ff38`
   plus unmount instead of waiting for `main()` to return **[L]**.
6. **Multiple local users** are handled (users 0–3) [V].

## 6. Evidence pointers

- **Logs:** play_log.txt lines 468–749 (boot load), 1004–1087 (boot save, `littlefart1927`),
  1689–1702 (`bigfart54` and `littlefart1928` while in the Pod), 1951–1955 (`littlefart1929`),
  2616–2628 (menu, then quit). play_log.1.txt lines 1004–1083 (boot save `littlefart1926`), then
  no saves before the quit at line 1784.
- **Decompiles:**
  - qre_out6 (Update, Enqueue, FlushAllSaves, message handlers);
  - qre_out7 (QueueProfileSaveIfDirty, pump, the flush call sites);
  - qre_out9 (quit flags, main, init steps);
  - qre_out10 (Begin, Finish, ctor);
  - qre_out12 (0x40 setters, leave-level `FUN_0025f290`);
  - qre_out13 (the Pod commit `FUN_00260210`);
  - qre_out14 (`FUN_009395f0` calls `FUN_00260210`);
  - qre_out15 (deferral predicates, Run `FUN_00939a30`);
  - qre_out16 (JobManager);
  - qre_out17 (frame function table, present).
- **Key disassembly:**
  - `23e600: mov al,[rdi+48]; and al,0xf9; or al,4; mov [rdi+48],al` (enqueue);
  - `23e620: test sil,sil; je; and byte [rdi+48],0x7a` (serialised ok);
  - `b6d8: movzx edi,byte [rax+0x4f5]; mov esi,1; call 4f3620`;
  - `5d1c3: call aacc0` inside the present.

## 7. Open questions for a live run (suggested probes)

1. **What fired the 13:43 save?** Run with `LBP3_NATIVE_TIMELINE=1` and open the Pod computer
   after placing a sticker. Look for a "new ProcessDirty__" line. Alternatively, use
   `--break=eboot.bin+0x4f30c0` with callers.
2. **Who sets 0x40, 0x02 and 0x04.** Use `LBP3_HWWATCH=*13a49f8+48`, which watches slot 0's
   local-profile flags byte. You should see 0x40 at the sticker, 0x02 from ProcessDirty, and
   enqueue/serialise in Begin.
3. **Op order.** The log wrote big before little, but `FUN_004f3140` queues little first. The
   candidate explanation is `local+0x6c0`: status 2, which makes the big profile save first.
   Check it with `--break=eboot.bin+0x939825` (the `cmp byte [r14+0x6c0]` in Begin), plus
   `0x93a180` (Finish) and `0x4f2d10` (job trampoline). Compare the TIDs to confirm that Run
   runs on a worker. It matters for A: if `+0x6c0` turns out to be common, use "B then A".
4. **Approach A end to end.** Place a sticker, Quit, and confirm that `ff38` flips, that
   `littlefart` (and `bigfart` if dirty) are rewritten, and that the sticker is there after a
   restart. Also check whether the engine shutdown steps crash or hang before the runtime exits.
5. **Approach B, if used.** Check whether `FUN_007037c0`'s "busy" state can hold while the pause
   menu is open, which would defer indefinitely, and whether `local+0x6c0` is ever set.
6. **Adventure saves.** Does quitting in the middle of an adventure lose `playfart` progress
   under either approach?
