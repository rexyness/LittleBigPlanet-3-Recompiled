# The early flip at the Pod's first frame (2026-11-12)

Follow-up to [first_frame.md](first_frame.md) section B and fix candidate 3. This was a read-only pass over the code and the existing split CSVs. Nothing was built or run.

## Answer

- **The stale-marker theory holds as far as code and data can show without a run. The address reuse itself is not proven:** no log records command-buffer addresses.
- **Code:** every link in the chain exists (section 1).
  - The runtime writes `kPatchedFlip` into the game's memory and never removes it.
  - The command processor flips on that value as the first body dword of **any** NOP.
  - The runtime's own NOP fills write only the header, so stale memory shows through as the first body dword. Upstream zeroes that dword.
- **Data:** the early flip follows the **size of the frame just before the Pod**.
  - It happened in 26 of the 27 runs whose previous frame was a 1-buffer, 1773-dword frame, and in 0 of the 6 runs where that frame was 1767 or 1505 dwords.
  - It happens nowhere else in ~150k frames.
  - A 6-dword change in the previous frame (1773 → 1767) is enough to make it go away with the same 18313-dword Pod frame. That points to a stale value whose position is set by the previous frame's size: its marker sits at dword 1715.
- **Effect (section 2):**
  - The frame's only pending flip is consumed at buffer 1. The frame's real marker, in buffer 8, then finds an empty queue and is dropped.
  - So the window shows the display buffer as it was at buffer 1, probably a two-frame-old loading-screen image (inferred), and the Pod's first rendered image is never shown.
  - The game gets its flip event 60–124 ms early, but the main thread is **not** released early, so ticket 10's race is not reopened.
- **Minimal fix (section 3):**
  - The command processor honours `PatchedFlip` only where `PatchFlipRequest` puts it: the NOP header 59 dwords before the end of a top-level command buffer. Any other occurrence is logged and ignored. This is about 6 lines in `liverpool.cpp`.
  - Then zero the first body dword in `FillNop`, as upstream does.
  - **One run of `m5_np_route.sh` proves both the theory and the fix** through the ignore log (section 4).

## 1. Theory against code and data

**Does the runtime ever restore the NOP? No.**
- `sceGnmSubmitAndFlipCommandBuffers` calls `PatchFlipRequest` on the last buffer in the game's own memory (`videoout.cpp:508`).
- The patch writes WRITE_DATA at `size−64` and a NOP at `size−59` whose first body dword is `0x68750776` (`videoout.cpp:332`, `:343-350`).
- Only 7 dwords are backed up (`b[7]`, `:338-339`), and nothing writes them back.
- The interrupt handler doesn't touch command memory (`videoout.cpp:218-247`).
- Upstream does the same (`gnmdriver.cpp:2090-2172`).
- On the console the driver also patches in place, but its CP doesn't act on NOPs.

**Does the copy carry the marker? Yes, and that is neutral.**
- The snapshot (`renderer.cpp:702-717`) is taken inside `SubmitGfx`, after the patch (`videoout.cpp:508` → `:513` → `:398`).
- So it carries this submit's marker, as it must, plus whatever stale value the game's memory holds.
- The 64-slot copy ring can't leak an old marker into a new submission: each slot is `assign`ed exactly `dcbBytes`, and the parse is bounded by the span.
- Indirect buffers are parsed from game memory, not copied (`liverpool.cpp:1027-1030`).

**What the CP flips on.**
- `ProcessGraphics` raises `GfxFlip` for any type-3 NOP with count ≠ 0 whose `data_block[0] == PatchedFlip` (`liverpool.cpp:447-457`), in any buffer and at any offset, including indirect buffers (IBs).
- This is the only `GfxFlip` source. The other signals are `GpuIdle` (`:270`), `GfxEop` (`:878`) and compute pipe ids 0–6 (`:1400`).
- `ff_bufs_at_flip = 1` counts hand-overs (`frame_split.cpp:323-324`, `perf/first-frame`), so the interrupt came while buffer 1 was being parsed.

**How a stale dword can be read at all.**
- A new stream that reuses the memory overwrites every dword it covers, except inside NOP bodies its writer leaves unwritten. The CP looks only at `data_block[0]`.
- So a stale marker fires only if a NOP header lands exactly on the old NOP header's dword (`size−59`) and its first body dword isn't written.
- The runtime's `FillNop` (`gnm_packets.cpp:52-60`) writes only `cmd[0]`. Upstream's `WriteTrailingNop` also writes `data_block[0] = 0` "(only one out of data_block_size is initialized)" (`gnmdriver.cpp:94-108`).
- LBP3 imports the functions that use it (`analysis\ghidra\plt_names.txt:518-528,536`):
  - `sceGnmInsertWaitFlipDone` (7-dword NOP fill, `gnm_packets.cpp:64-68`; upstream writes a WAIT_REG_MEM, `gnmdriver.cpp:1152-1172`);
  - `sceGnmInsertPushMarker` and `PopMarker` (NOP fills `:88-89`; upstream writes the payload and string, `gnmdriver.cpp:1064-1118`);
  - the trailing NOP of every `sceGnmSet{Ps,Vs,Cs,Es,Gs,Hs,Ls}Shader` and the two `*InitDefaultHardwareState` blocks (`gnm_packets.cpp:143-306`).
- **This is the candidate writer of the hole, not a finding.** The game's own NOPs could leave bodies unwritten too.

**Does the game reuse command memory so that an old last buffer lies under an earlier buffer? The data says yes in effect; the addresses are unproven.** The first multi-buffer frame of every run, with the frames before it (scan of every split CSV in `analysis\`, all rows):

| Runs | Frames before the first 8-buffer frame (dwords) | Pod frame | Early flip |
|---|---|---|---|
| ff1–ff8, ffc, `m5_ab_*` (6), `m5_flush_smoke`, `m5_hyp1`, `m5_wg_*` (4), `m5_xab_new1`, `m5_xab_old2` | …1499, 1499, **1773** | 24961 / 24955 | **23 of 23** |
| `m5_xab_old1` (same `expand` build as `xab_new1`, `LBP3_EXPAND` old) | …1499, 1499, **1773** | 24961 | no |
| `is_f3`, `is_s1`, `is_s2` | …1499, 1499, **1773** | 18313 | **3 of 3** |
| `is_f2`, `is_w4` | …1499, 1499, 1767 | 18313 | 0 of 2 |
| `is_f1`, `is_w1`, `is_h1` | …1773, 1505 | 18307 / 23751 | 0 of 3 |
| `is_w2` | …1767, 1505 | 18307 | 0 of 1 |

- Across every row of the completed runs (~150k, as of 04:33; the `m5_xab_*` runs were still being added by another agent, and the in-progress `frame_split.csv` shows it too), the early flip (`record_us < 0` with more than 1 buffer, or `ff_bufs_at_flip < flip_buffers`) occurs 26 times, each time at the Pod's first frame and nowhere else. All game submits are flip submits: LBP3 doesn't import `sceGnmSubmitCommandBuffers`.
- **The cleanest pair** is `is_f3`/`is_s1`/`is_s2` against `is_f2`/`is_w4`: the same build (`port 2d92d65`), the same 18313-dword Pod frame, and a predecessor 6 dwords shorter.
  - A 1773-dword buffer's patched NOP header sits at dword 1714, with its marker at 1715. A 1767-dword buffer's sits at 1708, with its marker at 1709.
  - The outcome follows that position, which fits Pod buffer 1 reusing the predecessor's memory with a header-only NOP at dword 1714.
- **Open:**
  1. **`m5_xab_old1` (today, current base plus the ExpandImage fix) had a 1773 predecessor and the same 24961-dword Pod, and no early flip.** `m5_xab_old2`, the same build and setting, had it. So there is a timing or allocation component: which memory becomes buffer 1 is likely not fixed. It may also have fired inside buffer 8, ahead of the real marker; `record_us` can't show that.
  2. **`[1773, 1505, Pod]` is clean.** With one fixed base, the 1773 frame's marker at 1715 would survive the 1505 frame, which writes only to 1504. Either those Pod frames (18307 / 23751) have no hole at 1714, or the base differs.
  3. **Which writer leaves the hole at 1714.**
- The data builds predate the geometry-shader change (`d84eae8`), which moves NOP holes inside `Set{Es,Gs,Hs,Ls}Shader` blocks. `m5_xab_new1`, on a build with that change, still has the early flip.

## 2. What happens when it fires

At the stale NOP in buffer 1 (`videoout.cpp:219-245`, on the command-processor thread, called synchronously from `liverpool.cpp:457`):
1. **It pops the frame's only pending flip** (`:222-227`). That entry was pushed just before the submit (`:511`), after `WaitIdle`, so it is the Pod frame's own.
2. **It reads back the display buffer and waits on the GPU** (`ReadbackDisplayBuffer`, `renderer.cpp:193-241`).
   - It ends the current rendering (`:214`), which splits any open render pass in the middle of buffer 1, then runs `Finish` (`:233`).
   - That `Finish` is the 33–36 ms of GPU wait for buffer 1's uploads.
3. **It waits for the flip slot,** which also anchors the main thread's frame clock there (`NoteFlip`, `videoout.cpp:94-100`). It then shows the pixels and calls `SignalFlip(arg)` (`:240-242`).
4. **It returns, and the processor carries on** with the rest of buffer 1 and buffers 2–8.
5. **The frame's real marker,** at the end of buffer 8, then finds `g_pendingFlips` empty and does nothing (`:228`). The next frame's entry is pushed only at its own submit, so the queue stays in step: every following frame is normal, with `ff_bufs_at_flip` 8 of 8 and `release_after_flip_us` about 10 µs.

**What the display shows (inferred, not captured):**
- The window shows the display buffer the Pod frame targets, as it was after buffer 1. There are 2 display buffers (`[video] 2 display buffers 1920x1080`).
- If buffer 1 doesn't draw into it, the window shows the loading-screen image from two flips earlier for ~70 ms. If buffer 1 clears or partly draws it, the window shows that partial state.
- **The Pod's first finished image is never read back or shown.** The next frame, 118–178 ms later, is the first Pod image on screen.
- The only window captures (`is_h1_w*.png`, `is_w2_w*.png`) come from clean runs. At a scene cut, a held loading frame is barely visible.

**The game is told early but not released early.**
- The flip event arrives 60–124 ms before the main thread returns from the submit: `release_after_flip_us` is +60,336 to +123,767 µs in every early row (27 rows: the 26 completed runs plus the in-progress `frame_split.csv`), with `submit_rel` 0, meaning the wait ended idle.
- The main thread is still held by the per-buffer `WaitGfxIdleOrBlocked` (`renderer.cpp:729`).
- While the handler runs, the graphics task is neither idle nor blocked, so no grace or learned-address release can fire.

**Ticket 10's race is therefore not reopened.** That race needs the main thread to reach its next frame-begin before the processor reaches the new buffer's first WAIT.

What *is* early:
- the flip event, with its frame-clock anchor;
- a 35–40 ms stall of the single command-processor thread in mid-frame, during which no queue runs and no EOP or `RELEASE_MEM` is signalled. That is the same stall every flip has, but placed in mid-frame.

**There is no display-buffer reuse hazard in the port:**
- The shown image is a CPU copy taken at the interrupt.
- `sceGnmInsertWaitFlipDone` is a NOP fill.
- The pending label that the patch sets to 1 (`g_bufferLabels`, `videoout.cpp:316`, `:342-347`) is never read or cleared, because `sceVideoOutGetBufferLabelAddress` isn't implemented.

**Cost:** none shown (first_frame.md section B).

## 3. Fixes

| # | Change | Size | Covers | Risk |
|---|---|---|---|---|
| **1 (do this)** | **CP gate.** In `ProcessGraphics` (`liverpool.cpp:342`), keep the buffer's end (`dcb.data() + dcb.size()` at entry, next to `base_addr`, `:357`) and a `top_level` flag, false for the IB call at `:1029`. At `:454`, signal `GfxFlip` only if `top_level && (const u32*)header + 59 == dcb_end`; otherwise log once per occurrence (rate-limited): the offset, the buffer size, the NOP count and the body dword. | ~6 lines (+1 parameter in `liverpool.h`) | Any stale marker that isn't at `size−59` of a top-level buffer, whoever left the hole. The gate checks position, not "the last buffer of this submit". | **Low.** It relies on one invariant, which the runtime (`videoout.cpp:332`, `:349`) and upstream (`gnmdriver.cpp:2093`, `:2127`) both set: the patched NOP is always at `size−59`. **Failure mode:** if a real marker were ever rejected, `g_pendingFlips` would fall one entry behind for the rest of the session, and every flip would show the previous frame's buffer. The log line would show that at once. Put a comment at both sites tying them together. |
| 2 (follow-up) | **`FillNop` writes `cmd[1] = 0`** when `dwords >= 2` (`gnm_packets.cpp:59`), as upstream's `WriteTrailingNop` does | 1 line | The holes the runtime leaves (WaitFlipDone, markers, trailing NOPs) | **Nil.** The dword is inside space the game reserved for the packet. Not sufficient alone: game-written NOPs aren't covered. Land it **after** the proof run, because it may hide the evidence. |
| 3 (alternative to 1) | **Patch a private copy.** Copy the last buffer into a runtime buffer, patch that, and pass it in place of `dcbs[count-1]` (`videoout.cpp:508-513`). The marker never enters game memory. | ~10 lines | Any stale marker | **Low.** With `LBP3_COPY_CMD_BUFFERS=off`, the copy needs ≥ 2 slots (a "blocked" release can leave the processor in it). It differs from the console, where game memory holds the patched packet; nothing is known to read it back. It doesn't prove itself: it needs a control run. |
| ✗ | **Clear or restore after the flip.** | – | – | **Not recommended.** In the handler it means the processor thread writing game memory that the game may already reuse (the frame's EOP can mark it free). On the main thread after `Process`, it is safe only with copying on, and it's still a late write into game memory. |
| ✗ | **Pass the expected address.** | – | – | The processor parses the snapshot (`renderer.cpp:715`), so the game's address never matches. Plumbing the snapshot address amounts to fix 1 with more code. |

## 4. The one-run check

**Build:** current `port` plus fix 1 (without fix 2).

**Also required:** a runtime line at each flip submit (`videoout.cpp:498-512`, first ~100 frames, or under `LBP3_FIRST_FRAME`). It prints the frame number (as the split CSV counts it), and for each buffer its index, game address and size, plus the patched NOP's address. The processor's line carries only the offset, buffer size, NOP count and body dword. Its buffer size and its place in stderr, between two submit lines, give the frame and the buffer index.

**Run:** `tools/m5_np_route.sh` once with `LBP3_FRAME_SPLIT=1`.

**Passes if:**
1. **Exactly one "PatchedFlip ignored" line,** at the Pod's first frame, in buffer index 0, at dword **1714**: the previous frame's `flip_dwords` (1773) minus 59.
   - With the runtime line: the ignored NOP's game address (buffer 1's address + 1714×4) equals the previous frame's patched NOP address.
   - That proves the stale-marker theory. The ignored NOP's count names the writer: count 5 is a 7-dword `WaitFlipDone` fill, and count 4 with a 6-dword block is `PopMarker`.
2. **The Pod's first frame flips once, at the end:** `record_us > 0`, `release_after_flip_us` about 10 µs and one period of about 180–260 ms, instead of 69–73 + 118–178 ms. On `perf/first-frame`, `ff_bufs_at_flip` = 8 of 8.
3. **No other ignored line,** and `release_after_flip_us` about 10 µs on every later frame, so the pending-flip queue stays in step.

**If no line appears and the Pod frame is clean,** the run didn't hit (`m5_xab_old1` is 1 miss in 27): repeat once.

**If the line appears at some other offset,** the marker isn't the previous frame's, but the gate still fixes it, and the log gives the address to chase.

**Fails if no line appears and the Pod frame still flips early** (`record_us < 0`). Then either the gate let the marker through, because it sat at `size−59` of some top-level buffer (the submit lines show whether buffer 1's size matches), or the theory is wrong and something else raises `GfxFlip`. In that case, log every `PatchedFlip` the processor sees, accepted or not, with its offset and buffer size.

**Then** add fix 2 and run the regressions: milestone 1 checks, the Bink intro and the Introduction route. The ignore line should then stay silent if the hole was the runtime's.

Scan script used: `python -I` over `analysis\**\*split*.csv`, every row, flagging `flip_buffers > 1 && record_us < 0`, or `ff_bufs_at_flip < flip_buffers`.
