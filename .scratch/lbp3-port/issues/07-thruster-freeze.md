# What hangs the GPU when a Thruster fires?

Type: task
Status: resolved
Blocked by: none
Map: [LBP3 native port](../map.md)

## Question

In the human's Create session, a Thruster was switched on and ran for a while, then rendering stopped at flip 17,220 with the Thruster firing ([Build LBP3 Recompiled and run it on this dump](02-baseline-run.md), gap 8). The GPU command thread waits forever in `gpu::ReadbackDisplayBuffer` → `Vulkan::Scheduler::Finish` → `Semaphore::Wait`, and the GPU stays at 45–48% load and 180 W. A submission is running and never finishes. Which draw or dispatch never finishes, why, and how big is the fix?

Upstream fixed a Thruster freeze on Oct 14 (stubbed `sceGnmMapComputeQueue` and `sceGnmDingDong`, `NOTES.md:477-481`). This one either has a different cause or is a regression that shows on NVIDIA. The answer decides where GPU risk sits on the milestone ladder.

## Leads

- The Thruster's flame and particles: probably compute work or a special shader stage.
- `sceGnmSetEs/Gs/Hs/LsShader` and `UpdateGs/Hs` only write a NOP (`runtime/src/hle/gnm_packets.cpp:66-89`). If the effect uses a geometry or tessellation stage, the video core runs it without that stage.
- None of the 19 stubbed Gnm functions were called in the baseline runs (no `[stub] libSceGnmDriver` line in `analysis\*.txt`).
- Evidence so far: `analysis\perf\perf01.txt`, the `[perf]` dump in `analysis\play_log.txt`, and `analysis\screenshots\freeze1.png` (all in `D:\Recomps\lbp3-recompiled`, none in git).

## Checklist

1. **Reproduce without the human if possible (AFK).** Find the Thruster in the Popit by remote input. The baseline run didn't find it under Gadgets → Movers. Give it about 30 minutes.
2. **If that fails (HITL).** The human makes a My Moon level containing one Thruster that is switched off, saves it, and closes the game. The agent can then load that level and switch the Thruster on by remote input as often as it needs.
3. **AFK.** Take an F9 trace of the frame before the switch, F10 at the freeze, and run `dump_draws` and the shader dumps for the last submission. Name the queue (graphics or compute), the draw or dispatch, its shader and stage. Run once with the Vulkan validation layer on.
4. **AFK.** On a throwaway branch, skip the identified draw or dispatch, or replace its shader, to confirm it's the culprit. Record the diff.

Keep logs and dumps out of git. The game dump stays read-only. Start the build with `cmd /d /c .\build.bat` from `D:\Recomps\lbp3-recompiled`, because the agent's shell sets `NoDefaultCurrentDirectoryInExePath=1`.

## Done when

An `## Answer` names the work that hangs (queue, draw or dispatch, shader, stage), gives the cause or the best-supported hypothesis, says whether the stopgap removes the freeze, and estimates the real fix in sessions for the milestone ladder.

## Comments

- 2026-11-10 (agent, on `local/frame-pacing` `dfd6828`, correct 33.3 ms pacing). **Step 1 (AFK) done.** It was found by remote input, with the human's hint "r1 bro, second page".
  - **Path to the Thruster:**
    1. From the Pod: Home → Create → My Moon → Create! on an empty slot.
    2. Square opens the Popit. Go down to the Tools Bag and press Cross.
    3. R1 moves to the next page, Gadgets. Walk right with the D-pad past the Movers into the next page's "Getting around" section; the Thruster is there. Its card says "Attach to objects to give them thrust".
    4. Cross picks it up, Cross places it, and Circle drops the copy on the cursor.
    5. To tweak it: Square → Popit Cursor → Cross, hover over the Thruster, then Square. The entries are Physics Properties, Acceleration, Currently On, Input Action and Thruster Sounds. The menu reopens on the last entry used, so read the label before toggling. The panels sit off-screen (gap 7), so only the labels are readable. (Later found: the panels were on screen; the agent's screenshots were cropped, 08 Comments.)
  - **Saving:** Options → Save works. A level containing one switched-off Thruster is saved in My Moon (`user\savedata`, 01:02:21).
  - **The human's freeze did not reproduce** in about 4 minutes (about 7,800 flips) of the Thruster firing in Create mode at 33.3 ms (21–22 ms of work per frame, nothing unusual). An F9 trace of a normal frame is in `analysis\targets\dump_010247_1`.
  - **A different hang reproduced.** With the Thruster on in Create mode: Options → Change to Play Mode → "leave Create Mode?" → Save Changes. Flips stopped at 31,380 and the save never started (no savedata mount).
    - The GPU command thread was idle: it polled empty queues, and the GPU drew 44 W.
    - The main thread spins with `scePthreadYield` in a job barrier at `eboot+0x8aa370`. It waits until 7 `u16` progress counters at `[*r15 + rbx + 0x42…0x4e]` reach a ticket number (call chain `7c314 4513fa 4535f8 42de22 43ac9d 42dae3 5a88 ba34 6900 a22 df8f`).
    - Every worker thread (JobManagerWorker0–3, SceneThreadWorker0–4, Spu 0–4, main fast/slow) is idle in a wait, so a job was dropped or a wake-up was lost.
    - Evidence: `analysis\t07_f10.txt` and `analysis\perf\t07_freeze_perf.txt` (F10 at the hang). The F10 overwrote the baseline's `perf01.txt`; the baseline's `[perf]` dump is still in `play_log.txt`.
    - This hang is not the baseline's GPU freeze, which had the GPU thread stuck in `Scheduler::Finish` at 180 W. It may be a separate gap (a hang when leaving Create mode with an active Thruster, or with any change), and it needs its own repro check.
  - **Tool:** [tools/self_unwrap.py](../tools/self_unwrap.py) rebuilds the eboot ELF outside the dump (`analysis\eboot.elf`) for `tools/disasm.py`.
  - **Next:** retry the GPU freeze with `LBP3_PACING=present`, because the human's freeze happened while the game ran too fast (13–24 ms frames). Also retry with the Thruster attached to a moving object.

- 2026-11-10, second attempt (`LBP3_PACING=present`, unpaced like the human's session).
  - The saved level loads through Home → "Recently Edited" → Edit Now (Square). The Thruster had fallen to the floor near the start.
  - It was switched on (presses of 100 ms; 150 ms presses registered twice in the tweak menu).
  - Closing the menu with two Circle presses then led, by some path, to a Bink movie (Swoop's introduction), so the 6-minute watch (frames at 8 ms) didn't observe the Thruster. Inconclusive. (Corrected 2026-11-10: the movie was the game's attract video, `lbp3_attract.bik` from frame 24,570 in the log, which plays after 5 minutes without a button press; 04 Comments.)
  - Log: `analysis\t07_run2_stderr.txt`. The freeze has not reproduced in either attempt.
  - **Asked the human** how their freeze happened: mode, attachment, and how long it ran. They know conditions the agent can't recover from the logs.

- 2026-11-10, third attempt: **the human's freeze reproduced** (it is not yet known whether a Thruster was involved this time; `local/frame-pacing` `dfd6828`, `LBP3_PACING=present`, the human playing). What they did is not yet known (asked).
  - Flips stopped at 8,550 (about 3 minutes at 21.6 ms frames). The log, the thread dumps and two F10 reports are in `analysis\t07_human_stderr.txt`, `analysis\perf\t07h_perf01.txt` and `t07h_perf02.txt`.
  - **Same signature as the baseline freeze.** The GPU command thread sits in the flip handler (`videoout.cpp:187`) → `gpu::ReadbackDisplayBuffer` → `Scheduler::Finish` → `Semaphore::Wait`. The game's main thread waits for that flip in `sceKernelWaitEqueue` (from `eboot+0x5ce34`).
  - **Each freeze coincides with a Windows GPU reset.** The System log has `nvlddmkm` event 153 and `Display` event 4101 ("Display driver nvlddmkm stopped responding and has successfully recovered") at 07:08:41 today and at 20:50:19 on 2026-11-09. The baseline's `freeze1.png` was taken at 20:53. Both 153 events carry the same data. There was no reset at the Save Changes hang above, so that hang is a separate gap.
  - **What this means.** GPU work ran over the 2-second limit, and the driver killed it. That makes the Vulkan device lost (inferred, not yet logged). `Semaphore::Wait` (`vk_semaphore.cpp:64`) loops on `waitSemaphores(...) != eSuccess`, so it spins forever when the call returns `eErrorDeviceLost`. The F10 samples match that: the GPU command thread is always busy inside `nvoglv64.dll` and `Semaphore::Wait`, never asleep.
  - **Correction to the Question.** "A submission is running and never finishes" was a misreading. The load seen after the freeze (42%, 180 W) is most likely the presenter redrawing the last image 4,400–6,400 times a second in `present` mode. The game's only busy GPU engine was 3D at 18%, and the compute engines were idle. The hung work itself died with the reset.
  - The last log line before the stall is `[shader warning] ... Unimplemented clamp mode 4` (a new sampler). The normal play test logged it 25 times without a freeze, so it is not a cause by itself.
  - **Pacing is a suspect.** Both freezes happened in `present` mode, with the presenter already at about 2,650 presents a second before the stall. None has happened on vblank pacing (the agent's 4-minute Thruster run, the human's 5.6-minute play test). The next run uses the default vblank pacing to tell the two apart.
  - **Instrumented for the next run** (`diag/gpu-checkpoints`: lbp3-recompiled `b4ba0a4`, shadPS4 submodule `4ef80152`). `LBP3_GPU_CHECKPOINTS=1` enables `VK_NV_device_diagnostic_checkpoints` and marks every draw and dispatch with its shader hashes. On `eErrorDeviceLost`, `Semaphore::Wait` and `SubmitExecution` now log the markers that last reached the top and the bottom of the pipe, with the work between them, and end the process. `LBP3_TEST_DEVICE_LOST=<n>` runs that report after n frames without a reset: it printed correctly, and the Pod held 33.3 ms with the markers on (about 160 per frame).
  - Tooling note: F10 numbers its reports per run (`perf01`, `perf02`, …). A watcher that copies `perf01.txt` can pick up an older run's report.

- 2026-11-10, fourth attempt: the human, default **vblank pacing**, `LBP3_GPU_CHECKPOINTS=1` (`diag/gpu-checkpoints`). What they did is not yet known (asked).
  - **It froze again, but without a GPU reset.** Flips stopped at 3,180 (106 s at 33.3 ms). There was no device-lost report and no `nvlddmkm`/`Display` event, and the GPU drew 44 W.
  - **This is the Save Changes hang from the first attempt, not the reset.** The main thread spins in the same job barrier (`eboot+0x8aa370`, same call chain), and the log has the same pair of stuck waits as that attempt's log (`t07_run1_stderr.txt`):
    - **compute** `WaitRegMem` (equal) on a slot of an 8-slot label ring at `0x1e1303cd8…d10`. It wants `0x1818` and holds `0x179a`. In the first attempt it wanted `0x4c8` and held `0x44a`; in both, the wanted value is 0x7e past the held one.
    - **gfx** `WaitRegMem` (equal) for a flag to become 1 (`0x1e1303c70` here, `0x1e1303c28` then).
  - **How the ring works, read from the log and live memory** (`analysis\t07h2_labels.txt`): compute `RELEASE_MEM` writes `seq<<4 | 9` to the next slot, round-robin. The game then raises it to `…a`, so all 8 slots end in `a` at the hang. Each compute submission first waits for its slot to equal the next round's `…8`. That value has to come from the game (the CPU or a job), and it never arrived.
  - **The barrier, read live** (`analysis\t07h2_barrier.txt`):
    - The main thread waits for the minimum of 7 `u16` counters to reach ticket 275.
    - Two counters are in use, at 47 and 46. The other five are `0xffff` (unused).
    - So two consumers stopped about 228 tickets back, while every worker thread is idle.
  - **Cause or symptom is open.** Either the CPU side lost a wake-up and never arms the slot (the two stuck waits are then symptoms), or the runtime's early compute `RELEASE_MEM` (applied when the packet is parsed, before the dispatch runs) breaks an ordering the game relies on. A single live read of the barrier showed two counters at 47 and 46 against ticket 275.
  - **So there are two freezes.** The GPU reset has shown up twice, both times in `present` mode. The compute-wait hang has shown up twice, both times in `vblank` mode: the agent's Save Changes, and this run. With four runs, the pacing pattern may be chance. The compute-wait hang moves to [its own ticket](10-compute-wait-deadlock.md); this ticket stays on the GPU reset.
  - **Diagnostics now** (`diag/gpu-checkpoints`, lbp3-recompiled `80588d4`): `Semaphore::Wait` waits in 1 s steps. It logs any result other than a timeout, and after 5 s it logs the checkpoints without ending the process, so a wait that never ends gets reported even when no reset happens. The next human run also keeps a rolling screenshot every 4 s, so what they were doing is on record.
  - Logs: `analysis\t07h2_stderr.txt`, `analysis\perf\t07h2_stall1.txt`.

- 2026-11-10, **the GPU reset reproduced without the human**, on vblank pacing, with the trigger in [ticket 10](10-compute-wait-deadlock.md): leaving Create mode through Save Changes with the Thruster firing. That makes 4 resets in 9 runs; the other 5 deadlocked. Each reset is in the System log (`nvlddmkm` 153 + `Display` 4101), and the new device-lost report caught it and ended the process. Logs: `analysis\t10r_tdr_stderr.txt`, `t10y_tdr_stderr.txt`, and `t10k_tdr_stderr.txt`. `t10d`'s log was overwritten by the next run; its report is in the session transcript. Whether the human's two resets came from the same trigger is unknown. They were in Create mode, and what they did last is not on record.
  - **The checkpoints, three runs with them on, all alike.**
    - Bottom of pipe stops inside a pass of about 50 indexed draws with vertex shader `9d4e1cf1010b7667` and pixel shader `462ef2915e42ff9d`, at its 3rd to 5th draw.
    - Top of pipe stops at the next draw, a 3-vertex draw with vertex shader `461c4e6d9a93c15b` and no pixel shader, which waits behind the pass.
    - So that pass is the leading suspect, not a proven culprit. The markers cover only draws, dispatches and the readback; copies, fills, resolves and uploads between them are unmarked, and a gap of 50 draws between top and bottom may be normal pipeline depth. The test that settles it: skip the pass (and, separately, `5c5e…`) by shader hash in the reset recipe.
  - **Bad data in the same frame.** Before the reset, compute shader `5c5e1be6e0ada38c` (9 dispatches in the hung submission) binds a buffer of 1,912,602,624 bytes at an unmapped address (`BindBuffers: Clamped size … to 0`). Pixel shader `ebb656d6240b4b34` gets the same error, and a sampler is rejected as invalid (`max_aniso=6 … border_color_base=0x16aff0100`). None of the human's two resets logged these lines, so they may not be the cause.
  - **No shader loops.** Dumped with `LBP3_DUMP_SHADERS=1` and an empty shader cache (`analysis\shaders\dumps\`): the control flow of `462ef2915e42ff9d` (trivial), `9d4e1cf1010b7667` (321 IR lines, no branches), `5c5e1be6e0ada38c` (nested ifs) and `461c4e6d9a93c15b` has no loops. So the reset is not one of these shaders running forever. That the pass reads garbage resources (descriptors, or index/vertex buffers the game hadn't finished writing) and faults or stalls the GPU is a hypothesis, untested.
  - **Reading.** Both freezes come from one trigger: leaving Create mode while the Thruster's SPU-job/compute pipeline is running. All 4 resets ran with the slow sync trace on, so the reset may be what happens when timing avoids the deadlock (ticket 10). Next: a skip-by-shader-hash switch at the marker sites, and markers on `CopyBuffer`, `FillBuffer`, `Resolve` and `DepthStencilCopy`.

- 2026-11-10, **after ticket 10's fix** ([10](10-compute-wait-deadlock.md), patch `patches/10-gfx-submit-wait.patch`):
  - **The reset in the save recipe has the same cause as ticket 10's deadlock.** On the same build and save, under timing that favours the reset (flag polling plus the sync trace), the old submit wait reset 2 of 2 times and the fix passed 3 of 3.
  - **The race:** the runtime let a graphics submit return before the graphics queue had started. The game then reset the per-frame flag that the buffer's first wait checks. When the wait still passed, the frame rendered on top of a fluid pipeline that never ran that frame. That fits the garbage buffer (1.9 GB at an unmapped address), the rejected sampler, and the hang inside the main scene pass seen at all four checkpointed resets. The fluid-data link is the best-supported reading, not proven.
  - **Not yet known:** whether the human's two resets share this cause. They happened in Create mode at fast pacing, with no save recorded, and with the human playing; one of them reset 3 minutes in. Fast pacing makes the game run ahead of the GPU more often, which widens the same race, so a shared cause is plausible.
  - **No AFK test was possible.** The 240 Hz display is off while the human is away, so only the 165 Hz display exists, and there `LBP3_PACING=present` falls back to the 33.3 ms timer. 8-minute soaks there were OK with both waits (correction, 2026-11-10: after 5 minutes without input the game leaves the scene for its attract video (`lbp3_attract.bik`), so the Thruster scene ran only for the first 5 minutes of it; `soak.sh` now nudges the stick every minute and reports IDLE if the attract video opens).
  - **Next:** a HITL run. The human plays as before, on the 240 Hz display, started by `tools/hitl_start.sh <tag> isolate` (diagnostic build: GPU checkpoints, rolling screenshot, present pacing; the newer fixes `LBP3_EPOLL_PRECISE` and `LBP3_GUEST_FPU` switched off, so the submit wait from 10 is the only fix switched on; the diagnostic build differs from the session that reset in other small ways, such as logging). The agent arms `tools/watch_freeze.sh <tag>` alongside. A clean run only counts if the reset still reproduces that day, so if it stays clean, follow it with `hitl_start.sh <tag> control` (the same with the old submit wait). If it still resets, this ticket is a separate bug, and `LBP3_SKIP_SHADERS` on the hung pass is the next test. The switch was smoke-tested on 2026-11-10: `9f99bb0401d9cc34` is the Thruster's flame composite, and skipping it removes only the flame (04 Comments). If it doesn't, 07 closes with 10's patch. The launcher was smoke-tested too (2026-11-10): `hitl_start.sh <tag> isolate` on the 165 Hz display printed the 240 Hz warning, and the status lines showed 33.3 ms frames, 30 of 30 on the timer, so the warning is right.
- 2026-11-10, **HITL `isolate` session `t07_hitl1`** (240 Hz display, present pacing, `diag/gpu-checkpoints` `1c80781`; only ticket 10's submit-wait fix on). The human played in Create with a Thruster, then went back to the Pod and reported "no crash". The game ran from about 18:08 to 18:16, so about 8 minutes including boot (the human said 15). It reached 22,410 frames, with no device loss, no stuck wait, no flip stall, and 0 submits while the graphics queue was blocked. The 30-frame averages ran at 16–28 ms in play (4–33 ms overall), the fast frames of the sessions that reset. 5 frames took over 40 ms after boot, the worst 181 ms at frame 4410 (during an 81 KB asset read from `base_001.farc` with 3 new images). Log: `analysis\t07_hitl1_stderr.txt`. Clean, but shorter than the 10–15 minutes planned. Next: the `control` run (`t07_ctl1`, started 18:16).
- 2026-11-10, **HITL `control` session `t07_ctl1`: the reset reproduced with the old wait.** Same setup as `t07_hitl1` (240 Hz, present pacing, the same binary) with `LBP3_SUBMIT_WAIT=all`; the human played the same way. The game reset about 1.5 minutes after launch, at tick 4175 (frame 4,170, 13.7–22 ms frames). The System log has `nvlddmkm` 153 at 18:18:02 and `Display` 4101 at 18:18:04, and the device-lost report ended the process. It shares the earlier resets' marker: 4 rejected samplers with the same garbage border-colour pointer `0x16aff0100` just before (the three earlier resets in ticket 10's recipe whose logs survive have one each). The point where the GPU stopped differs. The two of them run with checkpoints hung inside the main scene pass (`draw-indexed h0=9d4e1cf1010b7667` in flight). Here all of tick 4173's work had finished, the Thruster's flame composite (`h1=9f99bb0401d9cc34`) among it, and only the submission's end was in flight. (Corrected 2026-11-10: this line first said the flame composite was in the hung submission.) Log: `analysis\t07_ctl1_stderr.txt`.
  - **Comparison** (by the status lines that matched the Thruster scene in the soaks: 30 early releases per 30 frames with the fix, 390 with the old wait): with the fix, about 7,900 frames, roughly 3.5 minutes, of the Thruster scene and no reset; with the old wait, about 510 frames, roughly 11 s, then the reset. 0 rejected samplers with the fix, 4 with the old wait. Only the rolling settings file changed in either session. Not like-for-like for certain: the old-wait reset came the moment water started firing (below), and for `t07_hitl1` the human recalls "I think i fired water" (asked afterwards; not in the log).
  - **Verdict (agent):** the human's resets share ticket 10's cause, and ticket 10's fix stops them, on the evidence of one session each way. Per the plan above, 07 closes with 10's patch; the `LBP3_SKIP_SHADERS` fallback isn't needed. The ticket 10 session (`all` preset) and the Prologue playthrough keep watching for resets.
  - **The human on the `control` reset** (2026-11-10, after `t10_hitl1`): "in the previous one the moment i started firing water it crashed". So the old-wait reset came the moment a water (fluid) emitter started firing in a scene with a Thruster. That fits the fluid-data reading above: a frame rendered on top of a fluid pipeline that never ran that frame. With the fix (`t10_hitl1`, `all` preset), the human fired water with 3 Thrusters and saved, with no reset.

## Answer

Resolved on 2026-11-10 with [ticket 10](10-compute-wait-deadlock.md)'s fix. No change of this ticket's own is needed.

**The work that hangs.** The graphics queue. In the checkpointed resets of ticket 10's recipe it hung inside the frame's main scene pass (`draw-indexed h0=9d4e1cf1010b7667` in flight). In the human's `control` session it stopped at the end of a submission whose work had all finished. Every reset is preceded by rejected samplers with the same garbage border-colour pointer, `0x16aff0100`: descriptors read from memory that wasn't ready.

**Cause (best-supported).** Ticket 10's runtime race. A graphics submit returned before the graphics queue had started, so the game reset a per-frame flag that the queued buffer was about to check. The frame then rendered on data the compute side hadn't produced that frame. That data is most likely the fluid simulation's: the human's `control` reset came the moment a water emitter started firing.

**The stopgap.** Not needed: ticket 10's fix (graphics submits wait on the graphics queue only) removes the resets. `LBP3_SKIP_SHADERS=9f99bb0401d9cc34`, which removes only the Thruster's flame, stays as a diagnostic.

**Evidence** (the 240 Hz display, the human playing, `diag/gpu-checkpoints` `1c80781`):

| Session | Wait | Pacing | Water fired | Result |
|---|---|---|---|---|
| `t07_hitl1` | fix | present (the original resets' setup) | probably: the human recalls "I think i fired water" | about 3.5 minutes in the Thruster scene, no reset, 0 rejected samplers |
| `t07_ctl1` | old | present | yes; the reset came the moment it started | GPU reset about 11 s into the Thruster scene (`nvlddmkm` 153) |
| `t10_hitl1` | fix, every fix on | vblank | yes, with 3 Thrusters | about 23 minutes and a profile save, no reset |

The agent's runs agree: ticket 10's recipe passed 6 of 6 with the fix at 240 Hz, 3 at each pacing.

**Estimate.** Nothing beyond ticket 10's patch, which lands in milestone 1. Milestone 1's HITL check on `port` adds Thruster plus water at present pacing, to confirm the case `t07_hitl1` probably covered. The same kind of race may exist elsewhere in the runtime (spec, Known risks). The checkpoint build and the rejected-sampler warning are how it would show.
