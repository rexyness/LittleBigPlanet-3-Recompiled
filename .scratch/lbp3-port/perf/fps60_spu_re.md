# 60 fps analysis: what `spu.prx` does with time

Static RE of the game's job module `gamedata_orbis/spu/spu.prx` (unwrapped earlier to the clone's `analysis\spu.elf`). This note asks one question: if the simulation stepped at 60 Hz instead of 30, what in `spu.prx` would have to change? The other two parts of the 60 fps analysis are `fps60_timing_re.md` (the eboot's clock and tick) and `fps60_experiment.md` (forcing a 60 Hz flip rate).

**Labels:**
- **[decompiled]**: read in Ghidra's C output.
- **[disasm]**: read in the disassembly.
- **[scan]**: a byte scan or a scan of constant references over the whole module.
- **[inferred]**: a reading of the evidence, not proven.

Addresses are module offsets (`spu.prx+0x...`), the same as ticket 10's.

**Tools.** A separate Ghidra project: `D:\Recomps\lbp3-recompiled\analysis\ghidra_spu\spu.gpr`.
- Its program is `spu_etexec.elf`: `spu.elf` with `e_type` set to `ET_EXEC`, so addresses equal module offsets.
- The 18 imports and 100 job entry points are named (`plt_names.txt`, `job_names.txt`, `all_names.txt` in the same folder). Analysis took 25 s.
- The eboot's project wasn't opened. A copy of `tools/ghidra_decomp.sh` pointed at this project does the decompiling; it uses its own lock, `ghidra_spu/decomp.lock`.
- Constant scans ran over the module's whole read+execute segment (code and read-only data, `0x0–0x71108`) and over the linear disassembly `analysis\spu_full.asm`.

## Answer

- **No clock.** `spu.prx` never reads time [scan].
  - It imports 18 functions:
    - mutex lock and unlock;
    - event-flag set and wait;
    - `memset`, `memcpy` and `memmove`;
    - `printf`, `fmodf`, `_FSin` and `powf`;
    - TLS, the stack guard, `__cxa_guard_*`, `__cxa_finalize` and `module_start`/`stop`.
  - It has no clock, timer or sleep import.
- **No 30 Hz or 60 Hz time constants.** These values occur 0 times anywhere in the module [scan]:
  - float 1/30 (and its neighbours `0x3D088888`/`0x3D08888A`), 1/60, 60.0f and 1/120;
  - double 1/30, 30.0, 1/60 and 60.0;
  - integer 33,333, 16,666, 33,333,333 and 16,666,666.
  - Every use of 30.0f or an integer 30/60 that does occur is spatial or a loop bound (table below).
- **The main integrators take their step from the caller:**
  - rigid-body movement (`PBodyUpdate`);
  - the FLIP fluid's particle advection, gravity and vector operations;
  - the water height field's damping, speed and rest level;
  - the particle effects' damping and acceleration.

  The eboot fills these in when it builds each job [decompiled]. So halving the step for these needs **no change in `spu.prx`**; the eboot has to pass different numbers.
- **About ten read-only constants are tuned per step** [disasm]:
  - a rope or tether's velocity retention (0.9) and smoothing rate (0.00725);
  - the fluid's decay (2.92·x⁴), vorticity strength (0.175) and per-step counters (+0.1, +4000);
  - the water's smoothing (×0.998 per pass) and rain amount.

  Stepped at 60 Hz, each of these acts twice as often per second: ropes and fluid would be more damped and water would settle faster. Each is one value in read-only data, so it could be patched. But most have no exact 60 Hz equivalent: the 0.9 retention could become √0.9, while the x⁴ decay and the counters would need retuning by eye. The vorticity and counter readings are inferred.
- **Not here: animation, the fluid's "Spluid" driver jobs, and implicit springs.** Their job-table slots are empty, and `Implicit_Spring` is an empty function [decompiled]. Whatever animation or cloth timing the game has lives in the eboot.

**Bottom line for 60 Hz:** `spu.prx` is a library of computation kernels with almost no notion of time. The step-rate question belongs to the eboot: what it passes as the step, how often it runs the job chains, and what per-step numbers it bakes in. On top of that, `spu.prx` has a short list of per-step constants that would make fluid, ropes and water behave a little differently at 60 Hz unless they were retuned.

## 1. What `spu.prx` is, and how jobs are dispatched

**A port of the PS3 SPU job system to x86** [inferred, from the structure below]:
- Jobs read their inputs from a DMA-style input list into a per-thread "local store" buffer and write results back (`FUN_00001ec0` gets, `FUN_00002260` puts).
- Job names include `RSX_Kick` and `DMAMemCpy`.
- The dumped "separate executable allocation" from ticket 10 (`analysis\job_1c140c10000.elf`) is a byte-for-byte copy of `spu.prx`'s code [scan: its offset `0x1000` equals `spu.elf` file offset `0x4000`, which is vaddr 0]. All job code is therefore in this one module.

**The interface** [decompiled]:
- The only export, at `0x4460`, returns `0x3ed0`, the worker thread's main loop.
- Per pass of the loop, a worker:
  1. waits on its bit of `SpuEventFlag` (`sceKernelWaitEventFlag`, mode `0x21`, no timeout);
  2. collects the 16 workloads into priority buckets (`0x28`-byte entries; the state word at `+0x38` is running, ready and max contention, as in ticket 10);
  3. claims a workload with a CAS while running < min(ready, max);
  4. runs its chain (`FUN_00003a80`).
- The chain runner calls `(*jobtable[ctx+0x29e0])()`. The table is pointed to from `0x748f0`.
- A job finds its parameter block at `*(ctx+0x2a60)+0x20`: up to `0x1c` qwords, plus an input/output element list.

**The job table** at `0x742b0`: 100 entries of {function, name} [decompiled, from relocations]. Grouped:

| Group | Jobs (table index) |
|---|---|
| Soft bodies, ropes | `SoftSkin` (0), `Verlet` (3), `Squish` (4), `Spring` (5), `Resolve` (8), `ComMerge` (9), `SquishMerge` (10), `Implicit_Spring` (13, empty), `Tether` (17) |
| Rigid bodies, collision | `PBodyUpdate` (22), `ApplyForces2` (23), `ReadShape` (21), `ReadJoint` (38), `Read/WriteConstraints` (43/42), `ConvexEnumerate` (37), `QueryConvexes` (50), `RayCast` (31), `CSG` (12) |
| Broad phase | `AASort` (25), `AAMerge` (26), `Sweep` (27), `EndPoints` (28), `EndPointMerge` (29) |
| Fluid (FLIP solver, the eboot's `SPLUID0–3` chains) | 47 `Flip_*` stages (51–97, 99): advection, gravity, pressure (Poisson with a preconditioner), vorticity, wind, sort and spawn |
| Water surface | `WaterSurface` (45), `WaterSurface2` (46) |
| Rendering and other | `Normals` (6), `FrustCull` (7), `RenderBaseGetPrimitives` (33), `GenerateMesh` (40), `Triangulate` (98), `ParticleEffect` (41), `ATG1–3` (34–36), `NPCBuildSkylines` (47), `WebCam_Convert` (39), `RSX_Kick` (1), `Dependency` (2), `Fork` (24), `DMAMemCpy` (30) |
| Empty slots (no function) | `FindClosestSurface`, `Spluid`, `SpluidR`, `SpluidI`, `SHA1`, `Compress`, `Uncompress`, `AAMerge`, `SortConstraints`, `WriteConstraints`, `QRCode_Preprocess`, `NPCNavigate`, `AnimJob` |

The eboot side is from ticket 10 [decompiled there]:
- the job-list structs `{block*, spurs*, wid, name}` named `SKIN`, `NORMALS` and `SPLUID0–3`;
- `ReadyCountStore` at `eboot+0x8ab130` marks a workload ready and sets `SpuEventFlag`;
- the main thread waits for the frame's jobs in the barrier at `eboot+0x8aa370`.

`Flip_Kick` (`0x18ce0`) holds ticket 10's label-command interpreter (`0x18f7d…0x18fda`). That is how the fluid hands work to GPU compute.

## 2. Timing inside the jobs

### The step comes from the caller [decompiled]

| Job | What it does with time | Where the number comes from |
|---|---|---|
| `PBodyUpdate` (`0x333d0`, helper `0x32120`) | Per body: if \|v\|² + \|ω\| ≥ 0.001, moves and rotates it by `v·s` and `ω·s` (`0x32120` builds the rotation with a sine polynomial). Below that, velocity is zeroed (sleep). | `s` is a float at params `+0x0c` (the high half of qword 1). The low half picks one of two state copies (`+0x20`/`+0xb0`). |
| `Flip_Move_Particles_In_Grid1` (`0x19900`) | `p += (0.5·s)·v` for x, y and z (a half step). | `s` is an inline scalar in the job's input list. |
| `Flip_Add_Gravity` (`0xbd60`) | `v += a + b·x` | `a` and `b` are inline scalars. Then a per-step decay (see the table below). |
| `Flip_Increment`, `Flip_Scale_And_Increment`, `Flip_Scale` (`0x17810`, `0x1c2a0`, `0x1bc60`) | Generic `y += k·x` and `y = x/k`. | `k` from the inputs. |
| `WaterSurface` (`0x4e880`; wave-update case 6 = `0x4e530`) | `h' = (1 - r)·rest + r·(h - h·c·Δ)`, with `r = edge ramp × damping`. The ramp is row/30 for the first 30 rows: spatial. | `damping`, `rest` and `c` are caller floats. |
| `ParticleEffect` (`0x32070` → `0x315c0`) | Verlet per step: `x' = x + (x - x_prev)·d + a`. Ages are compared with time fields in the block. | `d` (block `[0..3]`), `a` (`[0x10..0x13]`) and the time fields (`[0x14..0x17]`) are written by the eboot. Whether those times are frames or seconds can't be told from here. |

So rigid bodies, fluid advection and gravity, water and particles all take their step size or per-step coefficients from the eboot. For a 60 Hz step the eboot would have to pass:
- half the `PBodyUpdate`/FLIP step;
- a quarter of the per-step acceleration `a` for the Verlet-style particles;
- a damping of √d instead of d.

None of that needs a code change in `spu.prx`.

### Per-step constants baked into `spu.prx` [disasm, with the readings marked]

| Constant (read-only address) | Used at | Job (how attributed) | What it does | At 60 Hz |
|---|---|---|---|---|
| 0.9 (`0x6b390`) | `0x4a50d`, `0x4a43b` | `Tether`: helper `0x4a280`, called from `0x4aab1` [disasm] | Verlet prediction `x + (x - x_prev)·0.9`, clamped to ±200 | 0.9 retention per step becomes 0.81 per 1/30 s. Would need √0.9 ≈ 0.949. |
| 0.00725 (`0x6b3f8`) and 0.95 (`0x6b3f4`) | `0x4a344`, `0x4a326` | `Tether`, same helper | `cur += (target - cur)·0.00725`: a per-step smoothing (0.95 scales the target) | Smoothing rate doubles per second. Would need ≈ 0.00363. |
| 2.92 (`0x67560`, `0x67570`) | `0xbd0e`, `0xc047`, `0xc1da` | `Flip_Add_Gradient`, `Flip_Add_Gravity` [decompiled] | `x -= min(2.92·x⁴, x)`: a nonlinear per-step decay of a fluid field | Decays twice as often. No exact conversion (nonlinear). |
| 0.175 (`0x67a30`, `0x67a70`) | `0x23f6b`, `0x244a4`, `0x246f8` | `Flip_Vorticity`, `Flip_Vorticity_Force` [disasm] | Vorticity-confinement force `0.175·(N × ω)`, with N normalised (+0.01 against division by zero) | Per step, unless the caller scales the force by its step when applying it [inferred; the applying job isn't traced]. |
| +0.1 (`0x67998`), +4000 (immediate `0xfa0`, capped at `0xffff`) | `0x22653`, `0x2267d` | `Flip_Update_From_Grid` [disasm] | Per-particle counters advanced each step (age or foam) [inferred] | Twice as fast per second. |
| 0.199601 = 0.998/5 (`0x6bd30`, `0x6be70`) | `0x52060`, `0x53866` | `WaterSurface` helper and `WaterSurface2` inline [disasm] | 5-point smoothing ×0.998 per pass | Smooths and damps twice as often. |
| 0.00025 per unit (`0x6baf4`) | `0x4e813` | `WaterSurface` helper `0x4e6d0`, called from `0x4eefd` [decompiled] | One "raindrop" added at a pseudo-random cell each pass | Twice the rain. |

This list comes from scanning every constant reference to near-1 factors (0.8–0.9999 and 1.0001–1.2) and from a per-job table of all float constants (see Reproduction). Other jobs' constants are geometry or math:
- Newton-Raphson `0.5/3.0`, sine and arctangent polynomials, π and 2π;
- 1/255 and 1/32767 conversions;
- grid sizes 248/124 and AABB margins.

Uses of 30 and 60 that are not time [disasm]:

| Value | Where | What it is |
|---|---|---|
| 30.0f (`0x6bae8`) | `0x4e5a9` | the water's 30-row edge ramp (spatial) |
| 30.0f (`0x674d0`, vector `{30,30,0,0}`) | `0x9ef1`, `EndPoints` | AABB margin (min/max) |
| 30.0f (`0x67510`) | `0x55792`, `WebCam_Convert` | camera image processing |
| integer 30 or 60 | `Flip_Apply_Preconditioner1` `0x10361` (`0x1e`/`0x1e00`), `Flip_Add_Wind` `0xcb56` (index range 1–30), `Flip_Get_Velocity_Update`/`Flip_Vert_Swizzle` (loop bound 60 on 64-wide grid rows), `Normals` `0x2dc94` (count clamp 60) | loop bounds and index limits |
| 120.0 (`0x696f0`) | `0x5cfd3` | `120 / x` in the shared helper area after `0x54000`; unclassified, not a rate |

## 3. If the simulation stepped at 60 Hz with half the step

- **Nothing in `spu.prx` reads or assumes the frame rate.** No clock, no 1/30, no 60. The kernels that integrate motion take their step from the eboot, inline in the job inputs or in the job's state block.
- **What would have to change in `spu.prx`: 8 values** (the table above, not counting the 0.95 scale): 10 read-only addresses, counting duplicate copies, plus one immediate (+4000). All are per-step factors. Patching them is mechanical (each is one aligned float in read-only data), but choosing replacements isn't:
  - per-step retention and smoothing (0.9, 0.998, 0.00725) convert exactly (√, or halving the rate);
  - the x⁴ decay, the vorticity strength, the counters and the rain would need retuning by eye.

  Leaving them unpatched gives slightly more damped ropes and fluid and faster-settling water at 60 Hz, not a breakage [inferred].
- **No sign of a design independent of the step.** The baked factors are per step, with no dt multiplier.
- **No sign of 30 Hz assumptions in the kernels either.** The 30 Hz knowledge, if any, is in the eboot: the step values it passes and how many job-chain runs it does per frame. That belongs to `fps60_timing_re.md`.
- **For the timing lane, the step that reaches `PBodyUpdate`:**
  - it is the float at `+0x0c` of the job parameters (job type 22);
  - FLIP's step is the inline scalar of `Flip_Move_Particles_In_Grid1` (type 79);
  - the eboot code that builds those jobs is where the step is chosen.

## Reproduction

- Strings and the job table: Python over `analysis\spu.elf`. The table is decoded from the PRX's 203 relocations (190 relative ones fill the `{fn, name}` pairs).
- Constant scans: Python over the read+execute segment (byte patterns) and over `analysis\spu_full.asm` (rip-relative constant references, 4 lanes for vector loads, immediates).
- Job attribution is by address range from the job table's sorted entry points. That is exact for code inside a job function, and [inferred] for helpers placed between jobs, unless a caller was found (as for `0x4a280` → `Tether`, `0x4e530`/`0x4e6d0` → `WaterSurface`).
- Decompiling: Ghidra 12.2 headless on `analysis\ghidra_spu` (`Decompile.java` from `tools/ghidra/`).
- Limits:
  - The linear disassembly can misread data inside the code segment, which costs a few false or missed references; the byte scans cover what it misses for the time constants.
  - I didn't trace into the eboot which values it passes, or how often each chain runs per frame.
