# 60 fps analysis: the eboot's clock, tick and step

Static RE of the eboot (`analysis\ghidra\lbp3.gpr`, program `eboot_etexec.elf`; addresses are module offsets, `eboot.bin+0x...`). The question: what decides how fast LBP3's simulation runs, and what would a 60 Hz game need? Companion notes: `fps60_spu_re.md` (the job module) and `fps60_render_re.md` (the renderer). Prior art (GoldHEN, RPCS3 patches) was moved to another lane and isn't covered here.

**Labels:** **[dec]** read in Ghidra's C or disassembly; **[str]** string or constant evidence (scans of the ELF or of Ghidra's references); **[inf]** a reading of the evidence, not proven; **[meas]** agrees with an earlier measurement (tickets 06/09).

**Tools:** `tools/ghidra_probe.sh` runs `tools/ghidra/Probe.java` (batched xrefs, decompiles, disassembly, scalar/constant/string scans) under the same lock as `ghidra_decomp.sh`; a batch takes about a minute. Float values below were read from the ELF (`.rodata` file offset = address + 0x4000).

## Answer

- **The simulation has no clock and no dt.** One sim step is one call of a parameterless step function; physics is integrated in units of "one step" (the rigid-body job gets the step `s = 1.0`). Time in seconds is derived from frame counts by dividing by 30. **[dec]**
- **The step rate follows the main loop, not the wall clock.** The "game host thread" builds input frames with no clock read; the main loop runs as many steps as frames are ready (one per loop pass in normal play). The game's real-time speed therefore comes only from the flip rate: the game asks for **30 Hz flips in play** and **60 Hz only during boot initialisation**. **[dec + inf, meas]** (unpaced 13–26 ms frames ran the game too fast, ticket 06; a 100–440 ms hitch still gave one step.)
- **So a 60 Hz flip alone gives a double-speed game, not a smooth one.**
- **No frame-rate setting, no 60 Hz gameplay path.** But there is a **dormant 50 Hz (PAL) path**: with a flag that nothing in the PS4 eboot sets, the loop cycles a 6-pass counter, skips the display update on the 6th pass and moves the *display copy* of rigid bodies by a fractional step (k/5) on the others. That is the only in-engine "between steps" mechanism found, and it covers rigid bodies only. **[dec]**
- **True 60 Hz simulation: not practical.** Every per-step quantity (velocities, forces, frame-counted timers in code, scripts and level data, network frame settings, ~8 per-step factors in `spu.prx`) would need halving or retuning; there is no single dt to change. **Render-side interpolation: partly scaffolded** (0-step frames are handled, rigid bodies can be displayed at a fraction of a step), but soft bodies (Sackboy), animation, particles and the camera have no fractional path. Port-side frame generation (`fps60_render_re.md`) remains the cheaper route.

## 1. Flip rate

- `FUN_0005d2c0(mode)` is the only setter path [dec]:
  ```c
  DAT_01060a10 = mode;
  if (mode == 2) sceVideoOutSetFlipRate(handle, 1);   // 30 Hz
  else           sceVideoOutSetFlipRate(handle, 0);   // 60 Hz
  ```
- Callers, all with immediates [dec]:
  - `FUN_0005bed0` (video init, `gfxcore.cpp`): `sceVideoOutSetFlipRate(handle, 0)` directly, after `sceVideoOutOpen` and a 1920×1080 setup.
  - `FUN_000aaee0` (engine initialisation: "InitTimingLog", "Initialisation", "PostResourceInitialise" sections): `mode 0` at its start, `mode 2` at its end.
  - `FUN_00005b90` and `FUN_00000310`: `mode 2`.
- **So:** 60 Hz flips only while the engine initialises (the boot loading screen); 30 Hz from then on. No config, Pro/Neo or capability check feeds the argument [dec]. The mode global's only other reader is the flip submit in `FUN_0005caf0` (passes 2 or 3 to `FUN_0005e1c0` depending on it) [dec].

## 2. The tick: who decides how many steps a frame gets

**The game host thread** (`FUN_0039c290`, created by `FUN_0039c230` with the name "game host thread") [dec]:
- Loops until shutdown: gathers its sockets, calls `select()` (the eboot's own wrapper `FUN_008d9cd0` over `sceNetEpollCreate/Control/Wait`) with **timeval {0 s, 33,333 µs}**, an immediate at `0x39c6db`, then calls `FUN_003a0900`.
- `FUN_003a0900` drains received messages, then loops **at most 5 times** (`if (4 < n || done) break;`) calling `FUN_0039ee60` → `FUN_0035c2a0`, which builds the next input frame for each player slot from queued inputs. Between passes `FUN_0035be90` decrements per-slot pending counters [dec].
- **No clock read** in `FUN_0035c2a0` or `FUN_0039ee60`; the generator's only tuning read is `FramesBeforeAgressiveCatchup / 2` from the network settings [dec].
- **Reading:** frames are produced when inputs are queued, at most 5 per wake; the 33.3 ms timeout only bounds how long the thread sleeps without traffic [inf]. At a 30 Hz flip that timeout lines up with the frames (ticket 09: 183 of 190 ticks right after a timeout) [meas]. Who queues the per-frame input (the main loop, once per pass) wasn't traced; it is inferred from ticket 06's unpaced runs, which took one step per frame at 21–26 ms frames and ran the game too fast [meas].

**The main loop's steps** (`FUN_00009d70`, the per-frame function) [dec]:
- Step budget `n` = `MaxCatchupFrames` (network settings `+0x144`, default **3**), at least 1, raised to 2 in a prediction case.
- `while (stepsThisFrame < n && FUN_003ae640(frame) /* a frame is ready */)`: consume one queued input per player, run **`FUN_00254450(world, inputs, 0)`** (one confirmed sim step), `stepsThisFrame++`.
- Then **`FUN_002514d0(f, world)`**: runs `k` *predicted* steps on a second state copy for display (`FUN_00251240`: `k = min(max(ready, MinPredictedFrames), MaxPredictedFrames)`, defaults 0 and 20), then the render-state update.
- A hitch therefore gives one step, not a burst: only one input frame is queued when the loop comes back [inf, meas: "a 100–440 ms hitch gave a normal single step"].

**Frame counters** [dec]: the world thing's `+0x4d0` block holds the game frame (`+0`, what `GetGameFrame__` returns) and the sim frame (`+4`, `GetSimFrame__`). They are incremented only in `FUN_00254450` (`0x2553d7`, `0x255461`) and `FUN_002514d0` (`0x2541be`, `0x2541d1`); the sim frame's increment is skipped when `FUN_00268060(&DAT_012ed2c0)` is true (a pause check [inf]).

**Network settings in frames** (`FUN_00c51520` serialises them; defaults are the immediates in `FUN_00009d70`/`FUN_00251240`) [dec, str]: `MaxCatchupFrames` (3), `FramesBeforeAgressiveCatchup` (30), `Min/MaxPredictedFrames` (0/20), `NumberOfFramesPredictionAllowedForNonLocalPlayer`, `Min/MaxPacketDelayFrames`, `EnablePrediction`, `AllowGameRendCameraSplit`, `GameStateUpdateRate`. Telemetry strings report `NumPS3s` and `PredictApplied/Desired` [str]: online sessions were lockstep with prediction across PS3 and PS4 [inf], so the step rate was a protocol constant.

## 3. The step: fixed, with no dt

- **`FUN_00254450(world, inputs, flag)` and `FUN_00251340(world, i, n)` take no time argument** [dec]. Nothing in the step loop reads a clock except profiling (`FUN_0001b1b0`/`0x1b3f0` deltas go to `FUN_0001b300` stats), the time of the last step (`DAT_0130d90c`, used to show a message after 3.0 s with no step) and the catch-up screen's thresholds.
- **Rigid bodies move by `v·s` with `s = 1.0`** [dec + `fps60_spu_re.md`]: `FUN_0019d130(s, world, list, copy)` → `FUN_00079650` builds job **0x16 = 22 = `PBodyUpdate`** with parameter qword 1 = `{copy, s}`, exactly the layout the spu lane found (`s` at params `+0x0c`, copy selector picks `+0x20`/`+0xb0`). Calls:
  - confirmed step `0x2552e6`: `s = 1.0` (`0xe670b0`), copy 0;
  - predicted steps `0x2541b2`: `s = 1.0` (`0xe67090`), copy `DAT_01061a04` (= 1);
  - only in the 50 Hz path (`0x2537df`, `0x2540f9`): `s = fraction` (section 4).
  
  So velocities are in units per step [dec + inf].
- **Seconds come from frames ÷ 30** [dec]: e.g. `FUN_008e24f0(start) = (GetSimFrame − start) / 30.0f` (`0xedbcfc`); `FUN_00931f50`: `(float)frames / 30.0f`; `FUN_00115950`: `(30 − n) / 30.0f`.
- **Frame-counted logic** [dec, str]:
  - in code: an idle counter that acts after `0xe10` = 3,600 frames (`FUN_0034a760`); stats every 33 frames (`gameFrame % 0x21`, `FUN_00009d70`) and telemetry every 512 (`& 0x1ff`, `FUN_00254450`);
  - script/reflection field names: `Game.SimFrame`, `Game.GunFireFrame`, `Game.LethalActivationFrame`, `Game.ModStartFrame`, `Game.ModDeltaFrames`, `Game.SpeedScaleStartFrame`, `Game.SpeedScaleDeltaFrames`, `AliveFrames`, `CrushFrames`, `JumpFrame`, `TriggerFrame`, `ActivationFrame`, `BlobVelFrames`, `m_uFrameNo`, natives `SetLifetime__Q5Thingi` (integer lifetime), `GetAnimNumFrames__Q5Thing`, `GetAnimFPS__Q5Thing`.
- **Constant census** (whole eboot):

  | Constant | Where | Count |
  |---|---|---|
  | float 1/30 (`0x3D088889`) | data | 0 [str] |
  | float 1/30 | immediate | 2, both `FUN_003e1890`/`FUN_003e2970` seeding a blend timer's elapsed time at one step [dec] |
  | float 1/60 | data reads | 4 functions, e.g. `FUN_0048b410` advances a wrapping accumulator by 1/60 per call (6000 wrap) [dec] |
  | double 1/30, 1/60; 33,333,333; 16,666 | anywhere | 0 [str] |
  | 33,333 (µs) | immediate | 2: the host thread's `select` timeout and the GPU end-of-pipe event wait (`FUN_0005dcf0`) [dec] |
  | 33.333f | immediate | 2: a field `+0x4b40` set in `FUN_00357e90`/`FUN_00358090` [dec; meaning unknown] |
  | 30.0f | data reads | 150 sites in 107 functions [str] |
  | 60.0f | data reads | 73 sites in 59 functions [str] |

  A sample of about 30 of the 30.0f/60.0f users: roughly half are frame↔second conversions or frame-count thresholds (above), the rest are spatial, angular or UI values [dec, sampled]. There is **no single dt variable**: the 30 Hz assumption is spread over integer frame counts, `/30` conversions and per-step units.
- Look-alikes that aren't the sim step [str/dec]: `m_fSimDt`/`m_fSimTimer` are the snow particle compute shader's constants; `FlipDT`, `FlipSubSteps`, `FlipCFL` are the FLIP fluid solver's tunables (`FUN_00c66af0`, with `TimeScale`, `NoiseTimeStep`, `RippleTimeStep`); `EnableVSync` is a field of a capture/replay settings block (`AutoStart`, `MaxBufferSize`, `MaxFrames`, `ReplayFileDefs`, `FUN_00c886b0`); `refresh_rate` is a telemetry field; double 30.0/60.0 are a 30-day date check (`FUN_008c4050`).

## 4. Leftover 60 Hz / 50 Hz support

- **No frame-rate option**: no fps, framerate, target-fps, sim-rate or interpolation setting string [str]. The only 60 Hz flips are boot initialisation (section 1).
- **A dormant 50 Hz path** keyed on the byte `0x120ac62` (in `.bss`, so 0 at load; **no write found** by references or by width-aliased stores to `0x120ac60/61`) [dec]:
  - `FUN_003bbe10` reports refresh **50 instead of 60** next to 1920×1080 when it is set;
  - `FUN_00009d70` cycles a sub-frame counter `c` = 0..5 (`0xb6fb`: `c = (c+1 > 5) ? 0 : c+1`); on `c == 5` it doesn't call `FUN_002514d0`; it runs `FUN_0024ae10`, possibly one predicted step through `FUN_00251340`, and later `FUN_00257000`. Otherwise it calls `FUN_002514d0(c / 5.0f, world)` (`5.0` at `0xe49ca8`);
  - `FUN_002514d0` then steps the display copy's rigid bodies by the **fraction** on its last predicted step (`0x2540f9`), or by `fraction − 1` when no step ran this frame (`0x2537df`);
  - other readers: frame stats `FUN_0040eeb0`, a frame-time threshold in `FUN_00236060`, video playback `FUN_007469f0`.
  - **Reading:** a PS3-era PAL mode: 25 shown frames per 30 steps, showing bodies at k/5 of a step so motion stays even [inf]. Whether the 6th pass also skips its flip wasn't checked. It shows the engine already copes with 0-step frames and can place rigid bodies between steps, but only through `PBodyUpdate`'s fractional `s` on the display copy. Nothing fractional was found for soft bodies (`SoftSkin`/`Verlet`/`Squish`), animation, particles or the camera.
- **Note against `fps60_render_re.md`:** that note says no blend factor exists, which is true of the render entry. The fractional-step factor above sits upstream, in the display-state update, and only the dormant 50 Hz path uses a value other than 1.0.

## 5. What 60 fps would take

**(a) A true 60 Hz simulation** (60 steps a second at half the step). Since the step rate follows the loop, presenting at 60 Hz already gives 60 steps a second; the work is making each step half as big:
- the rigid-body step `s` 1.0 → 0.5 (the two 1.0 vector constants and their 3 call sites) — easy [dec];
- every force, impulse and acceleration the eboot applies per step (not enumerated; the per-step job inputs the spu lane lists), plus `spu.prx`'s ~8 per-step factors (`fps60_spu_re.md`) — hard, some non-linear;
- every frame count: thresholds in code, `/30` conversions (part of 150 sites), script fields named `*Frame(s)`, animation advance, and the network frame settings;
- level data: creations and story levels store logic timing (timers, sequencers, lifetimes) that the code counts in steps [inf]; it can't be patched per level, so behaviour would change across content.

Risk: very high, effort open-ended, with no way to check every level. **Not recommended.**

**(b) 30 Hz simulation, 60 Hz display (render-side interpolation):**
1. Flips at 60 Hz: patch `FUN_0005d2c0`'s mode or override `sceVideoOutSetFlipRate` in the runtime [dec].
2. Gate steps to 30 a second, because the step rate follows the loop: let the host make one input frame per 33.3 ms of wall clock (or gate the main loop's ready-frame check), so loop passes alternate 1 and 0 steps. The 0-step pass is already handled (`FUN_002514d0` with `k = 0`) [dec + inf].
3. Show the in-between pass at half a step: set `s = 0.5` on the display copy, as the 50 Hz path does with k/5. This covers rigid bodies only [dec].
4. Not covered, each needing new RE:
   - Sackboy and other soft bodies;
   - skeletal animation;
   - particles, with per-render-frame counters (the render lane's `n/10` fades would run twice as fast);
   - the camera, which follows Sackboy, so it judders if only some things move.

Risk: high, with partial coverage; the cheap test of the mechanism is item 3 alone.

**Cheapest experiment from this note:** set byte `0x120ac62 = 1` at runtime. The loop should then cycle the 6-pass counter and place rigid bodies at fractional positions on the display copy [inf]. That would show whether the dormant path still works on PS4 before anyone builds on it. Untested.

## Not done

- The writer of the host's per-slot pending counters (the link "one input per loop pass") wasn't traced; it rests on ticket 06's measurement.
- The 30.0f/60.0f census was sampled (about 30 of the 166 reading functions, plus the four 1/60 users), not classified in full.
- What `FUN_0024ae10`/`FUN_00257000` do on the 6th 50 Hz pass, and whether the display copy is rebuilt from the confirmed state each pass (`FUN_0019cbf0`), weren't decompiled in depth.
