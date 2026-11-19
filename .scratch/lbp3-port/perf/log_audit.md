# Play-log audit (2026-11-13)

Every real play session's log, checked for problems nobody had looked at yet. Offline only; the game wasn't launched. Scripts: `tools/log_audit.py`, `tools/log_audit2.py` and `tools/perlevel_work.py`. Labels: **measured** (counted in the logs), **code** (read in the source), **inferred**.

**Sessions** (all in `D:\Recomps\lbp3-recompiled\analysis\`; duplicates removed by md5):
- **Old build, 11-11:** `play_logs_20261111\play_log.3/.2/.1.txt` (17:53, 18:59, 20:09) and `play_logs\play_log_20261111_2103.txt`.
- **Current build, 11-12:** `play_logs\play_fg_20261112_1458.txt` (90 min, frame generation), `play_1440_log.txt` (76 min, 1440p), `play_1440a_log.txt`, `play_logs\play_log_20261112_1955.txt`, `play_logs\play_dlc_20261112_2054.txt`, `play_logs\play_unlocks_20261112_2143.txt`, `play_logs\play_log_20261112_2159.txt`.
- **Agent boots, listed but not counted:** `play_fg_log.1.txt`, `play_log_20261112_2318.txt`, `play_log.txt` (00:45).

The old-build sessions predate the GPU-flush, pre-submit-wait and Protect fixes. Their heavy frames and early releases (e.g. Needlepoint, `play_logs_20261111\play_log.1.txt` line 4786 on) are superseded and not ranked below.

## Findings, by user impact

### 1. Over-budget stretches at 1440p: the Diner, and the Pod after it (new)

**Measured** in `play_1440_log.txt`, lines ~32,400–34,700 (flips 124,230–131,430, about 4 min):

| Stretch | Work median | Work max | Frames over 40 ms |
|---|---|---|---|
| Diner | 23.9–29.6 ms | 88.9 ms | 629 |
| Pod (craftworld), right after | 29.7–35.2 ms | 46 ms | 533 |

- **Over budget:** 99 one-second windows over 33.3 ms, which means 13–25 fps in places.
- **Early releases:** submit 385 known + 3 grace, compute 12. Graphics-queue blocking averaged only 0.28 ms/frame over the stretch, and 115 pipelines compiled in it.
- **Same level at 1080p:** in the frame-generation session (`play_fg_20261112_1458.txt`) the Diner stayed at median 18.3 ms, p90 21.5, 3 frames over 40 ms in 9.9 min.
- **The Pod in another session:** `play_1440a_log.txt` shows p90 25.3 ms in the Pod, with 14 windows over budget (lines 551–4120).

Per level at 1440p (`play_1440_log.txt`):

| Level | Minutes | Work median / p90 / max | Windows over budget | Frames > 40 ms | Early releases |
|---|---|---|---|---|---|
| Casino | 16.3 | 22.7 / 26.4 / 95.9 | 11 | 83 | 0 |
| Space | 12.1 | 20.0 / 24.3 / 35.5 | 1 | 31 | 0 |
| Crypts | 35.1 | 15.6 / 22.8 / 35.9 | 1 | 35 | 0 |
| **Diner** | 4.2 | 21.2 / **39.3** / 88.9 | **44** | **636** | **400** |
| **Pod, after the Diner** | 2.6 | 23.3 / **40.5** / 46.2 | **66** | **536** | **353** |

**Not known before.** The 1440p cost was measured only on the scripted Needlepoint route: about +3.4 ms, 22.7 ms walking (`verify_2cd464a.md`). What makes the Diner and that Pod stretch heavy at 1440p isn't in these logs; they carry no frame split. So it could be the GPU (1.78× the pixels) or something scene-specific.

**Next step:** an `LBP3_FRAME_SPLIT=1` A/B in the Diner at 1080p against 1440p, which needs a Diner test save. If it's GPU-bound, the Diner is where `RES=1440` costs frames.

### 2. The main thread polls timed locks harder in heavy frames (a lead, not a cause)

**Measured:** the `[kernel] timed lock #N` sampler prints every 100,000th game call to `scePthreadMutexTimedlock`.
- **Rate goes up in heavy windows.** In over-budget windows the samples per one-second window are about 4× higher than in normal ones:

  | Session | Over-budget windows | Normal windows |
  |---|---|---|
  | `play_fg_20261112_1458.txt` | 3.39 | 0.81 |
  | `play_1440_log.txt` | 1.94 | 0.48 |

  That's roughly 340,000 timed-lock calls a second against 80,000.
- **Hot call sites,** all on `main`: `eboot.bin+0x2152d` (1,718 of 4,463 samples in the frame-generation session), `+0xc3b8d` (1,148) and `+0xc3c3d` (1,143).
- **Example:** the 15 s slowdown in Bite Size (`play_fg_20261112_1458.txt` lines 34,509–34,717) had work of 40–102 ms with no compiles and no graphics blocking. Almost every timed-lock sample there came from `0x2152d`.
- **The same site in the crash:** the 21:43 crash session's main thread was also in 2 s timed locks at `0x2152d` (`play_unlocks_20261112_2143.txt`, around line 15,047).

**Code:** a contended timed lock in the runtime spins on `TryAcquireSRWLockExclusive` plus `SwitchToThread` until the deadline, rather than blocking (`runtime/src/hle/kernel_pthread.cpp:204–216`). An uncontended call costs a `Clock::now()` and a try-acquire.

**Inferred:** in those frames the main thread waits in a polling loop on a lock or a job result. Whether the spin-yield emulation makes the wait itself longer, by competing with the lock holder for CPU time, is open.

**Next step:** sample the main thread's stack in the Bite Size or Diner burst, and find what `0x2152d` locks (Ghidra). It's not known in the tickets; `NOTES.md` only says "busy polling loops show up here".

### 3. Known and expected (no action)

- **Crash at `eboot+0x1bd1a5`** (`play_unlocks_20261112_2143.txt` line 15,047): ticket 19. The human asked not to chase it, and it wasn't seen again in about 45 Options presses (`verify_2cd464a.md`). The only crash block in all the logs.
- **Boot:** one frame of about 3.0 s at boot in every session (windows of about 260 ms of work around lines 470–860). This is the game's own boot pause (gap 10).
- **The LBP1/LBP2 import:** 1,229 frames over 40 ms at flips 3,630–9,030 of the unlock session, while the import screen added items one per frame (ticket 19).
- **First-visit freezes:** compile and level-entry stalls of 0.3–1.9 s. Examples: Casino 896 ms (`play_1440_log.txt` line 8,762); the DLC level kit `sackboy_spoke` 1.4–1.9 s (`play_dlc_20261112_2054.txt` lines 11,057–12,680). See `first_visit.md`.
- **Graphics-queue blocking:** p95 0.5–3.9 ms per frame; this is the fluid flag (`gfx_wait.md`).

### 4. A texture-cache copy warning fires about once a frame (rate not noted before; effect unknown)

**Measured:** `FindImageFromRange: Failed to find exact image match for copy addr=0x180940000 / 0x188700000, size=0x4000000`.
- The log dedupes it, printing every 1,000th with a counter. The frame-generation session ends at `(x152000)` after 162,000 flips (line 34,530 shows x119000 mid-session).
- So the 64 MB lookup fails on about 94% of frames.

**Code:** the main caller is the masked-fill pre-sync (`buffer_cache.cpp:359`): a written buffer aliasing a GPU image. With several images in the 64 MB range and none matching its size, the function returns no image and the pre-sync is skipped.

**Known:** `intro_stalls.md` noted the same warning after level loads, but not that it fires every frame.

**Inferred:** it's likely benign, the same path as before the render-target-fill change. Logging costs nothing because of the dedup. Whether skipping the pre-sync loses any image content in that range is unverified.

### 5. Clean in every current-build session

- **Savedata:** no errors. The routine `LBPxSAVEx12x350` read/write probe produces only `mount` lines (ticket 19).
- **Draws and shaders:** 0 dropped draws, 0 `NOT on the list` 1440p shader lines, 0 `timed lock … gave up` timeouts.
- **`[gpu]`:** nothing beyond the known expansion notes (`new-layer upload refused`, `not a layers-only growth`, 2–3 per session, `expand_layers`).
- **Audio:** underruns only on port 3, the 1-channel type-2 voice port (gap 11, known: up to 13,001 in the 1440p session). Never on the music or effects ports 1 and 2.
- **Missing files:** `craftworld.ini`, `dad_cache/pck.farc`, `dad_cache/pin.farc` and `PersistentNotifications_1.per`, one each per boot. These are the game's own probes (inferred harmless), plus the usual patch-overlay probes under `/temp0/hdd0/CUSA00473_00/gamedata/`.
- **Other warnings:** `TranslateProgram: First instruction is not s_mov_b32 vcc_hi`, 1–7 per session (unexamined, low).

### 6. Stubs: the same boot-time set in every session

**Measured:**
- Every session, old and current, logs the same 28–29 `[stub]` calls, all during boot or the Pod:
  - `sceSysmodule*`, `sceCameraIsAttached` (3 calls per session; it isn't log spam);
  - Move and MoveTracker init, `sceGameLiveStreaming*`, `sceVoice*` setup;
  - `sceNpWebApi*`, `sceNpSignalingInitialize`, `sceContentSearch*`, `sceCompanionHttpd*`, `sceCoredumpRegister*`;
  - two `sceVideoOut*` calls and `_sigprocmask`.
- **No stub first appears on entering a level:** the Prologue, Manglewood (hub, Casino, Space, Sackbeast, Crypts, Diner, Bite Size, Bandit's Path) and the DLC kits (SpongeBob hub, Sackboy and Swoop spokes) all add none.
- `sceGameLiveStreamingGetProgramInfo` appears only in 3 sessions.
- The 15 stubbed `libSceAvPlayer` functions were never called; videos are Bink.

**Inferred:** nothing in these logs points to a missing library that later levels need. Unvisited worlds (Ziggurat, Bunkum) could still call something new; it would show as a `[stub]` line.

### 7. Log size: about 4 MB/h on the current build (fix on a branch)

**Measured:** old build 1.3–1.5 MB/h; current build 4.0–6.1 MB/h. Nearly all of it is the once-a-second status lines (`play_fg_20261112_1458.txt`, 6.1 MB in 90 min):

| Lines | MB/h | Share of the log |
|---|---|---|
| `[framegen]` (two lines) | 1.48 | 36% |
| `[video]` | 0.72 | 18% |
| `[perf]` | 0.64 | 16% |
| `[fills]` | 0.43 | 11% |
| `[kernel]` timed-lock sampler | 0.23 | 6% |
| `[protect]` | 0.11 | 3% |

Every session is also archived in `analysis\play_logs\` for good, so it adds up to about 0.4 GB per 100 hours of play.

**Fix (branch only, not merged):** runtime `fix/log-noise` `369e239`, from `port` `9625c5a`, pushed to the fork; worktree `D:\Recomps\lbp3-wt\lognoise`.
- `LBP3_STATUS_FLIPS=N` (30–3,600) sets the status interval, logged once when set. The default stays 30, so test scripts and tools see the same 30-flip windows.
- **Code:** all five status lines are printed from one tick (`hle/videoout.cpp` `SignalFlip` → `PrintPerfLine` → `framegen::PrintStatus`). Their counters accumulate until printed (`exchange(0)`, deltas, `TakeStats`), so a longer interval only lengthens each window.
- **Inferred:** with `LBP3_STATUS_FLIPS=300` in `play.bat` (one set every 10 s), play logs would shrink to about 0.9 MB/h.
- **Trade-off:** coarser windows in the human's logs, so tools that assume 30-flip windows (`perlevel.py`, these audit scripts) would need the interval from the `[video] status lines every N flips` line.
- Built, and the offline tests pass: cooptest, dlgtest, osktest, dialogtest, jpegtest, imetest, framegentest, audiotest, addconttest and rescachetest, all 0 failed. Not run in game.
