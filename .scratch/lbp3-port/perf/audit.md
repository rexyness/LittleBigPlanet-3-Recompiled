# Performance audit: runtime and video core (read-only), 2026-11-11

Code read at `D:\Recomps\lbp3-recompiled` (branch `m3/coop-verify`, shadPS4 submodule `50b4720a`). Nothing was built or run. Paths: `hle/`, `gpu/`, `video/`, `core/` are under `runtime\src\`; `video_core/` and `common/` are under `third_party\shadPS4\src\`. Logs: `analysis\play_logs_20261111\play_log*.txt`, parsed with throwaway scripts (`python -I`).

## The numbers being explained

Per-level averages over the `[video]` lines (a level runs from its first `.farc` open to the next one; revisits aren't split out):

| Log, display | Level | avg ms | work ms | slot wait ms | frames over 40 ms | early releases per frame |
|---|---|---|---|---|---|---|
| `play_log.1`, 240 Hz | needlepoint_peaks | 37.7 | 33.9 | 3.8 | 22.9% | 0.43 |
| | newtons_airship | 33.5 | 28.9 | 4.6 | 0.6% | 0.20 |
| | stitchem_gate | 38.2 | 34.4 | 3.7 | 30.0% | 0.62 |
| | stitchem_manor | 34.5 | 31.8 | 2.6 | 4.1% | 0.27 |
| | stitchem_boss | 33.7 | 28.6 | 5.1 | 0.8% | 0.60 |
| | titan_released | 33.7 | 24.9 | 8.8 | 0.4% | 0.23 |
| `play_log.3`, 165 Hz (timer grid) | needlepoint_peaks | 35.2 | 29.2 | 6.0 | 6.5% | 0.25 |
| `play_log`, 240 Hz | manglewood_hub / diner | 34.1 / 33.6 | 25.8 / 22.3 | 8.3 / 11.3 | 1.1% / 0.4% | 0.05 / 0.05 |

work + slot wait matches avg in every row.

**"Early releases" counts more than blocked graphics frames.** The counter (`video_core/amdgpu/liverpool.h:106,113,139,146`) adds up releases from three call sites:
- main's `WaitIdle` before each flip (`hle/videoout.cpp:456`);
- `DingDong`, the async-compute submit (`gpu/renderer.cpp:612`);
- the graphics submit wait (`gpu/renderer.cpp:675`).

The first two fire whenever the compute queue sits on a learned address. Some `play_log.3` windows show 40–63 releases per 30 flips.

With that caveat: in Needlepoint Peaks (`play_log.1`), the windows with 26–30 early releases per 30 flips averaged work 39.4 ms, and windows with none averaged 28.4 ms. Stitchem Gate shows the same split: 38.9 ms against 27.9 ms. This is a correlation, not a cause (see D, suspect 3).

## A. What "work" and "slot wait" measure

Both are timed in `WaitFlipSlot` (`hle/videoout.cpp:108-132`), which runs **on the shadPS4 command-processor thread** (`shadPS4:GpuCommandProcessor`). The call chain:

- The game calls `sceGnmSubmitAndFlipCommandBuffers` (`hle/videoout.cpp:447`). This is its only graphics submit. `sceGnmSubmitCommandBuffers` logs its first 5 calls (`hle/videoout.cpp:406-408`), and none of the four logs has those lines.
- The command processor parses the flip packet and calls `IrqC::Signal(GfxFlip)` synchronously (`video_core/amdgpu/liverpool.cpp:423-427`, `third_party\shadPS4\src\core\platform.h:65-83`).
- The handler is in `hle/videoout.cpp:188-212`. It runs under `g_handlerMutex` (`gpu/renderer.cpp:122-125`) and does three things:
  1. `ReadbackDisplayBuffer` (`:204-205`);
  2. `WaitFlipSlot` (`:208`);
  3. `PresentPixels` and `SignalFlip` (`:209-210`).

- **slot wait** is the time spent in `WaitFlipSlotInner` (`:110-113`):
  - **Vblank path** (`video/presenter.cpp:309-347`): the wait for the target vblank. A late frame returns at once, and the next target becomes vblank + 8 at 240 Hz. If no vblank arrives (the window moved, or is minimised), it waits up to 200 ms (`:320`).
  - **Timer path** (`hle/videoout.cpp:81-95`), used at 165 Hz because 165/30 isn't a whole number (`presenter.cpp:316`): sleep, then spin to a 60 Hz grid. A frame up to 8.3 ms late flips at once. A later one waits for the next grid point, so on the 165 Hz display a 42–50 ms frame becomes 50 ms.
- **work** is the time from the end of the previous slot wait to the start of this one (`:114`, `lastEnd`), on the same thread. It is the whole frame period minus the slot wait. It is **not** the game's CPU time. In order, it contains:
  1. the tail of frame N on the command-processor thread: `PresentPixels`, `SignalFlip`, and the rest of N's command buffer;
  2. the game's main thread waking from its submit wait and running frame N+1 until the next submit;
  3. `WaitIdle` (`hle/videoout.cpp:456`), the patch, and the command-buffer copy (`gpu/renderer.cpp:644-659`);
  4. the command processor parsing N+1 and recording its Vulkan commands, including any first-sight pipeline compile or texture upload;
  5. any time the graphics queue sits blocked on a `WAIT_REG_MEM`;
  6. in `ReadbackDisplayBuffer`, `scheduler.Finish()` (`gpu/renderer.cpp:226`). This is the frame's **first and only** Vulkan submit, followed by a wait until the GPU has executed the whole frame;
  7. the invalidate and an 8.3 MB `memcpy` (`:228-230`).
- **avg, worst and over 40 ms** are intervals between `SignalFlip` calls (`hle/videoout.cpp:138-146`): when a frame is handed to the window, not when it reaches the screen. The presenter shows it at its next vblank.
- `LBP3_PACE_LOG=1` writes work and wait per flip to `analysis\pace.csv` (`:119-131`).

## B. Per-frame costs from submit to screen

"Serial" means this cost lies on the same critical path as the game's main-thread CPU work and the GPU work, so it cannot overlap them.

| # | Cost | Where | Thread | Serial? |
|---|---|---|---|---|
| 1 | Wait until all queues (graphics and async compute) are idle, or blocked with a 20 ms grace or a learned address | `hle/videoout.cpp:456` → `gpu/renderer.cpp:576-587` → `video_core/amdgpu/liverpool.h:95-121` | game main | yes |
| 2 | Overrun scan of the top-level DCB | `hle/videoout.cpp:351-360`, `hle/pm4.cpp:201-210` | main | yes, small |
| 3 | Copy of DCB and CCB (default on; `LBP3_COPY_CMD_BUFFERS=off`) | `gpu/renderer.cpp:644-659` | main | yes, small |
| 4 | **Main thread blocks until the command processor has finished the whole flip buffer**, which includes items 5–9. It returns early only if the graphics queue blocks (`LBP3_GPU_ASYNC=1` skips this wait) | `gpu/renderer.cpp:665-675` → `video_core/amdgpu/liverpool.h:128-154` | main | **yes: the frame pipeline is fully serial** |
| 5 | PM4 parse and Vulkan recording for every draw and dispatch: pipeline lookup or compile, binding, buffer-cache copies (`video_core/buffer_cache/buffer_cache.cpp:170-187`), texture refresh and upload (`video_core/texture_cache/texture_cache.cpp:786-858`). One thread serves every queue (`video_core/amdgpu/liverpool.cpp:179-241`) | `video_core/amdgpu/liverpool.cpp:311-1072` | command processor | yes |
| 6 | **The GPU runs the frame only at the readback.** Nothing else submits during normal frames: `sceGnmSubmitDone` is a no-op (`hle/videoout.cpp:479`), so `Liverpool::SubmitDone` and its `Flush`/`OnSubmit` (`video_core/amdgpu/liverpool.cpp:230-236`) never run, and `gnmdriver.cpp` isn't built (`runtime/cmake/video_core.cmake:27-41`). The GPU is idle while the frame is recorded, and the command processor is idle while the GPU runs | `gpu/renderer.cpp:226` → `video_core/renderer_vulkan/vk_scheduler.cpp:117-132` | command processor | yes |
| 7 | **GPU→CPU readback of the display image every frame**: barrier, copy to a host buffer, `Finish`, invalidate, and an 8.3 MB `memcpy` of 1920×1080×4 into a recycled vector | `gpu/renderer.cpp:187-232` | command processor | yes |
| 8 | Slot wait (vblank or timer) | `hle/videoout.cpp:64-101`, `video/presenter.cpp:309-347` | command processor | yes. It also halts every queue and every later EOP interrupt (ticket 10's job kicks) for its duration |
| 9 | `PresentPixels` (swap under a mutex) and `SignalFlip` (status line every 30 flips, flip event) | `video/presenter.cpp:414-440`, `hle/videoout.cpp:135-165` | command processor | yes, tiny |
| 10 | Presenter: on each new frame, `SDL_UpdateTexture` of 8.3 MB into a D3D11 texture (CPU→GPU again), then a draw and `SDL_RenderPresent` once per real vblank (165 or 240 per second) | `video/presenter.cpp:218-259` | presenter | no (separate thread; small GPU and PCIe contention) |
| 11 | Vblank counter (`IDXGIOutput::WaitForVBlank`) | `video/presenter.cpp:61-107` | vblank thread | no |
| 12 | Async compute submit: a static mutex, then the same all-queue wait as item 1 before every `DingDong` | `gpu/renderer.cpp:607-627` | game `ComputeSubmit` | can stall compute submission behind items 5–8, unless a learned address releases it (usually, per ticket 10) |
| 13 | `ProcessDownloadImages` at every EOP, EOS, WRITE_DATA and RELEASE_MEM. It downloads synchronously, with `Finish`, only while a capture is queued (the `read back` field) | `video_core/texture_cache/texture_cache.cpp:81-136`, `video_core/renderer_vulkan/vk_rasterizer.cpp:494-496` | command processor | only during captures |
| 14 | EOS with GDS store calls `Finish` mid-frame | `video_core/amdgpu/liverpool.cpp:826-833` | command processor | yes, if LBP3 uses it (unknown) |
| 15 | Write-tracking faults. The first CPU write to a page the caches have protected goes through the exception handler, then `InvalidateMemory` and `VirtualProtect`; the handler copies its vector of handlers on every fault. Dynamic buffers are protected again after each upload | `gpu/shadps4_glue.cpp:106-119`, `video_core/page_manager.cpp:434-445`, `video_core/renderer_vulkan/vk_rasterizer.cpp:1345-1362`, `gpu/video_core_glue.cpp:31-51` | any game thread that writes | adds CPU time on game threads. The count per frame is unknown |
| 16 | Readback on a fault: the faulting thread waits for the command processor and a `Finish` (`SendCommand<true>`). Off by default (`LBP3_READBACKS`) | `video_core/buffer_cache/buffer_cache.cpp:98-127`, `runtime/shim/core/emulator_settings.h:21-26` | game thread | off |
| 17 | Timed mutexes spin with `SwitchToThread` instead of sleeping. The process makes about 1–3k `scePthreadMutexTimedlock` calls per frame. The counter is process-wide; the log names the caller of every 100,000th call, mostly main | `hle/kernel_pthread.cpp:190-221`, `:529-542` | game threads | burns cores; no wait added unless contended |
| 18 | Logging: one status line per 30 flips, `[pace]` and `[fs]` every 5 s, shadPS4 warnings capped per call site (`gpu/shadps4_glue.cpp:32-51`), Info level filtered before formatting (`common/logging/log.h:35,50-56`). About 1.3 MB in a 61-minute session (~350 B/s) | various | various | **negligible** |

Not per-frame, but worth knowing:

- **Garbage collection never runs.** Because of item 6, `Rasterizer::OnSubmit` (`video_core/renderer_vulkan/vk_rasterizer.cpp:487-492`) is never called. The texture cache never collects images or samplers, staging rings and dedicated buffers are never trimmed, and the buffer cache's fault buffer is never processed. VRAM therefore only grows, and the pages of old images stay write-protected.
- **Build flags:** the build is RelWithDebInfo, which means `/O2 /Ob1`, limited inlining (`build.bat:6`, `build\CMakeCache.txt`).
- **Already in place:** `timeBeginPeriod(1)` is set (`hle/kernel_time.cpp:52`), so sleeps are not 15.6 ms.

## C. What can block a game thread for hundreds of ms

1. **First-sight shader and pipeline compiles, on the command-processor thread, while main waits in item 4.**
   - **Code:** `GetGraphicsPipeline` and `GetComputePipeline` compile synchronously: GCN→SPIR-V, then `vkCreate*Pipelines` (`video_core/renderer_vulkan/vk_pipeline_cache.cpp:340-392`). The disk cache only saves keys and SPIR-V, on an IO thread (`video_core/cache_storage.cpp:139-167`). At boot, `WarmUp` recreates every cached pipeline (`video_core/renderer_vulkan/vk_pipeline_serialization.cpp:304-377`), so only never-seen pipelines stall during play. The `VkPipelineCache` starts empty each run (`video_core/renderer_vulkan/vk_pipeline_cache.cpp:332`), so those boot recreations lean on the NVIDIA driver's own cache. Compiles are logged at Info, which is filtered out, so the play logs don't show them.
   - **Evidence:** I matched the stall windows to the write times of `user\shader_cache\CUSA00473\*.key` and `*.spv`. Window times come from summing window durations backwards from each log's last write.
     - `play_log.txt` (Manglewood, a new world, 630 cache files written): 18 of 19 windows after boot with a worst frame of 150 ms or more had cache writes within ±4 s. That includes all 7 windows with stalls of 300 ms or more: 612, 1067, 474, 1413, 515, 1365 and 698 ms. Only 18.6% of all windows have writes that close.
     - The 1365 ms and 698 ms stalls (`play_log.txt:6040`, `:6063`, ≈21:01:49 and 21:01:57) line up with 140 cache files written between 21:01:46 and 21:01:57, and with recompiler warnings in the log (`play_log.txt:6032-6072`, `TranslateProgram`).
     - `play_log.1.txt`: 10 of the 12 post-entry windows of 150 ms or more had writes, including the 1126 ms stall in titan_released (`:9608`, 29 pipeline keys). The overall rate is 9%.
     - `play_log.3` played content mostly already in the cache (49 writes). 3 of its 14 post-boot windows of 150 ms or more had writes nearby (`:5147`, `:5626`, `:5684`; 170–233 ms). The slow stretches at 17:32 and 17:46 had none.
2. **Texture cache at level entry:**
   - **Image creation and uploads:** creating images (`video_core/texture_cache/texture_cache.cpp:635-639`) and uploading them, with a `memcpy` into staging and a GPU detile (`:850-857`), all on the command-processor thread.
   - **Lighting volumes:** the volumes are expanded one layer at a time, 144 expansions, once per session at the first level (`:538-541`; warnings at `play_log.1.txt:4588-4616`).
   - **Seen today:** the level-entry windows with no compiles nearby had 189–203 ms frames and 70–126 new images (`play_log.1.txt:4630`, `play_log.txt:1081`, `play_log.3.txt:975`). All three also contain the session's 144-expansion burst, so the ~0.2 s can't be put on new images alone. (`play_log.1.txt:8110`, with 147 new images and a 163 ms frame, had 4 pipeline keys written nearby.) This explains single 0.2 s hitches, not 0.5–1.4 s ones.
   - **Not seen today:** "Image overlap resolve failed" (free and recreate, `:616-632`, ~200 ms per frame per NOTES) and capture readbacks (`read back` was 0 in every stall window).
3. **The file read path** (`hle/kernel_fs.cpp:215-289`): it reads synchronously on the calling thread, in 1 MB chunks through a thread-local bounce buffer, then copies into guest memory. Nothing in it should take seconds for 14–50 MB on NVMe. Two things are unmeasured:
   - each destination page that the GPU caches still protect faults once during the copy (item 15). Old images are never collected, so a level's freed memory can stay protected;
   - no per-read latency is logged anywhere; the `[fs] read` lines give volume per 5 s.
   - Today's logs don't contain the Introduction stalls of 1–2.5 s (the Introduction here peaked at 67 ms), so this stays open.
4. **Memory mapping:** every `mmap`, flexible map or `munmap` clears the committed-range cache that the command processor's buffer copies rely on, after which they go back to `VirtualQuery` system calls. An unmap also frees every texture-cache image in the range and invalidates the buffer cache, on the calling thread (`hle/kernel_memory.cpp:112,129,141,164` → `gpu/renderer.cpp:134-150` → `gpu/video_core_glue.cpp:65-96`, `video_core/texture_cache/texture_cache.cpp:204-211`). How often this happens is not logged.
5. **Save data:** `sceKernelFsync` calls `FlushFileBuffers` (`hle/kernel_fs.cpp:405-410`). Whether LBP3 fsyncs during story saves is unknown.
6. **Display changes:** if the vblank source drops, the flip waits up to 200 ms (`video/presenter.cpp:320`) and the vblank thread re-enumerates (`:66-103`).
   - The 503–517 ms frames at `play_log.1.txt:4501-4510` coincide with the window moving from 165 Hz to 240 Hz.
   - But those windows were timer-paced (28–30 on timer), and the time shows up as work, not slot wait. So the 200 ms timeout doesn't explain at least the first of them, and the cause is unconfirmed.
   - Either way, it happens only on display changes.
7. **Logging** cannot account for it (B, item 18).

Also seen, and not one of the reported symptoms: in `play_log.3` (165 Hz) there were stretches of 10–60 s at 40–115 ms per frame, at `:2140-2260` and `:4640-4740`, with no new images and no compiles.
- During them the vblank thread counted only 82–150 vblanks per second out of 165 (`[pace]`). Host threads weren't being scheduled on time, or the driver's vblank wait was delayed.
- In the second stretch, the process-wide timed-lock counter rose to about 23k calls per frame. Its sampled caller was mostly main, at `eboot+0x2152d`.
- The cause is unknown. The 240 Hz story session (`play_log.1`) showed no vblank deficit.

## D. Ranked suspects and the cheapest check for each

### Symptom 1: heavy levels over budget (work about 34 ms)

1. **The serial frame pipeline (B, items 4–8).**
   - **What the code shows:** main's frame N+1 cannot start until frame N has been recorded, run on the GPU (submitted only at the readback), copied back, and its vblank slot has passed. On a PS4 the CPU builds N+1 while the GPU runs N.
   - **Size:** command-processor time + GPU time + copy time, all unmeasured. The only earlier figure is "costs ~2.5 ms/frame" for the synchronous submit (NOTES.md:256, October, Introduction-era content).
   - **Check:**
     1. Press F10 at a heavy spot in Stitchem Gate or Needlepoint Peaks. The report samples `main` and `GpuCommandProcessor` (`core/runner.cpp:612-646`), so it shows:
        - main's share in game code against its host chains in `WaitGfxIdleOrBlocked` and `WaitGpuIdleOrBlocked`;
        - the command-processor thread's share in `Scheduler::Finish` / `Semaphore::Wait` (GPU), in `WaitFlipSlot`, and in recording.
     2. Then an A/B with `LBP3_GPU_ASYNC=1`: one frame stays in flight through `hle/videoout.cpp:456`. If the average drops to 33.3 ms, the serialisation is the cause.

     Caveats for the A/B: the Bink video may tear, and ticket 10 deadlocked with `GPU_ASYNC` in the Thruster save recipe. Under it, read avg and frames over 40 ms; "work" changes meaning.
2. **Command-processor recording time on one thread**, which is part of 1.
   - **What the code shows:** shadPS4's cost per draw and dispatch, plus buffer-cache copies.
   - **Check:** the F10 host chains for `GpuCommandProcessor`, and the first instrumentation point below.
3. **Blocked waits, seen through the early-release counter** (which sums three call sites; see the note under the first table). The graphics queue may be blocked at the start of the frame.
   - **What the logs show:** windows with 26–30 early releases per 30 flips have work about 11 ms higher than windows with none. This is a correlation only, and the counter includes compute-side releases.
   - **Ticket 10's protocol:** each frame's command buffer starts with a wait on a flag that the game's job threads set 8–12 ms after the frame begins. While that wait holds, everything else (recording, GPU, readback) waits for the jobs.
   - **Runtime side:** items 7–8 halt the command processor, which delays the EOP interrupts that kick job lists.
   - **The grace path:** a blocked wait on an address that isn't learned costs 20 ms (`video_core/amdgpu/liverpool.h:109-115`, `:142-148`). Addresses written by a command stream are forgotten again (`video_core/amdgpu/liverpool.cpp:96-98`). `grace_releases` is counted but not printed.
   - **Check:** split `early_releases` by call site (main's `WaitIdle`, `DingDong`, the submit wait), and add to the status line:
     - the graphics queue's blocked time per frame (timestamps around `video_core/amdgpu/liverpool.cpp:927-940` and `:975-992`);
     - `grace_releases`.
4. **GPU execution time.**
   - **Check:** time `scheduler.Finish()` at `gpu/renderer.cpp:226`.
5. **Display readback copy**, about 1–2 ms estimated.
   - **Check:** time `gpu/renderer.cpp:228-230`.
6. **Write-tracking faults on game threads** (B, item 15).
   - **Check:** add an atomic count and a QPC sum in `GuestFaultSignalHandler` (`video_core/page_manager.cpp:434`), printed per status window.
7. **Spinning timed locks, or CPU oversubscription.**
   - **Check:** the `[pace]` vblanks/s against the refresh rate, which is free; it showed no deficit in `play_log.1`'s heavy levels. Then count contended spins at `hle/kernel_pthread.cpp:205-216`.
8. **`/Ob1` build.**
   - **Check:** rebuild Release (`/Ob2`) and compare work at the same spot.
9. **No garbage collection** (VRAM growth). This needs VRAM close to full before it matters.
   - **Check:** Task Manager's dedicated GPU memory over a session.
10. **Presenter at 240 Hz with a texture upload per frame.** Small.
    - **Check:** PresentMon or GPU-Z GPU load.

**Ruled out:**
- logging;
- a coarse timer: `timeBeginPeriod(1)` is set;
- `ImGui::TextureManager::Submit`, which is a no-op (`runtime/shim/imgui/renderer/texture_manager.h:5`);
- the shader warning paths: rate-limited, and Info is filtered.

**One instrumentation point would decompose "work" exactly:** per-flip timestamps in the existing `LBP3_PACE_LOG` CSV, taken at:
- submit entry (`hle/videoout.cpp:447`);
- after `WaitIdle` (`:456`);
- flip IRQ entry (`:189`);
- around `Finish` (`gpu/renderer.cpp:226`);
- after the `memcpy` (`:230`);
- plus the graphics queue's blocked time.

From these: main CPU, blocked time, recording, GPU and copy, each per frame.

### Symptom 2: stalls of 0.5–1.4 s

1. **First-sight pipeline and shader compiles** (C1; strongest evidence).
   - **Check:** replay a level that has just been played for the first time (Stitchem Manor, the Manglewood diner). With a warm cache its compile stalls should disappear.
   - Or compare `user\shader_cache\CUSA00473` file times with the stall times, as done above.
   - **Small instrumentation:** print the per-window change in `num_new_pipelines` (`video_core/renderer_vulkan/vk_pipeline_cache.cpp:356,384`) and the compile milliseconds on the `[video]` line.
   - **Converse test:** `LBP3_NO_SHADER_CACHE=1` should multiply the stalls.
2. **New images and uploads at level entry** (C2), about 0.2 s.
   - **Check:** already visible in `new images` and `expanded`. The compile counter above separates the two causes.
3. **Read path, faults, map/unmap, fsync** (C3–C5): the candidates for gap 10's Introduction stalls; unproven.
   - **Check:**
     - `LBP3_TRACE_READS=N` with an F10 report, or `LBP3_SAMPLE=main:<s>`, during a transition;
     - small instrumentation: time each `ReadImpl` and log any read over 20 ms with thread, file and size; count faults and map/unmap calls per status window.
4. **The game's own load work.** Gap 10 says the boot stalls of 2–3 s happen on a PS4 too.
   - **Check:** F10 at the stall, which shows main in game code rather than in runtime waits.
5. **Display or window changes** (C6): only when the window moves or the monitor changes.

## What stays unknown

- How work splits between main CPU, job-thread latency, recording, GPU execution and the readback copy (D, instrumentation point above).
- Which waits produce the early releases (the counter sums three call sites), and how many are 20 ms grace releases.
- How many write-tracking faults happen per frame, and per level load.
- Whether LBP3 uses EOS with GDS store (a mid-frame `Finish`), calls `fsync`, or maps and unmaps memory during loads.
- What caused the `play_log.3` stretches with the vblank deficit.
- How fast VRAM grows without garbage collection.
