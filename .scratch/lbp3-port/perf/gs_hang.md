# Why the GPU dies at the first ES+GS draw with `LBP3_GS_STAGES=1` (read-only), 2026-11-12

Code read at `D:\Recomps\lbp3-wt\gs` (runtime `23d3070`) and `D:\Recomps\lbp3-wt\gs-shadps4` (shadPS4 `e56074fe`). shadPS4 paths are under `src\`.

Data:
- logs `analysis\m2_gs_p1_{on,off}_{1,2,3}_stderr.txt`;
- the shader cache `user\shader_cache\CUSA00473\`;
- the Windows System log;
- `analysis\ghidra\eboot_etexec.elf`.

Copies of the relevant cache files, their disassembly and two patched modules are in `perf\gs_hang_files\`. The tools used were spirv-dis, spirv-val and spirv-as from the Android NDK 29 (`%LOCALAPPDATA%\Android\Sdk\ndk\29.0.13846066\shader-tools\windows-x86_64\`, SPIRV-Tools v2022.4); no Vulkan SDK is installed. I also ran `python -I` and `tools/ghidra_decomp.sh` (read-only). Nothing was built, run on the GPU or edited.

## Answer in brief

- **It is not a hang. The GPU raised an exception in the geometry shader's program header.**
  - Every "on" session logged the same three nvlddmkm events 1 ms before the reset. The event IDs are 13, then 153, then Display 4101:
    - `Graphics Exception: Shader Program Header 8 Error`
    - `ESR 0x405840=0x90000100`
    - `ESR 0x405848=0x80000000`
  - Times: 00:58:46.458, 01:00:19.729, 01:00:59.811. None of the 21 earlier 153 events this month has an event 13 with it.
  - **How the ESR decodes.** nouveau reads 0x405840 as the `SHADER` trap: `sph = stat & 0xffffff`, `stage = (stat >> 24) & 0x3f` (`nvkm/engine/gr/gf100.c`, `gf100_gr_trap_intr`).
    - That gives SPH error bit 8, which the driver's own text confirms ("Shader Program Header 8").
    - It gives stage 0x10, i.e. bit 4. In NVIDIA's pipeline-shader numbering, 4 is `GEOMETRY` (`NVC697_SET_PIPELINE_SHADER_TYPE_GEOMETRY`, open-gpu-doc `classes/3d/clc697.h`). So the faulting program is **very probably** the GS.
    - That stage reading is an inference: nothing public says the 6-bit field has one bit per shader type, although 6 bits for 6 types and a single set bit fit it.
  - **Timing.** In on_1, event 13 fell in the same millisecond as the third new pipeline key (`g_0x8a81d9e032b9c511.key`, 00:58:46.458). In on_2 and on_3, the cache preloaded the same pipelines at boot and nothing faulted. The fault came only when the first GS draw ran. So creating the pipeline is fine; running it faults at once.
- **Most likely cause: shadPS4 declares `OutputVertices 64` for a GS that emits 4 vertices.** That puts both modules over NVIDIA's `maxGeometryTotalOutputComponents`:
  - **Where the 64 comes from.** The 64 is `VGT_GS_MAX_VERT_OUT` (`vk_pipeline_cache.cpp:149` → `emit_spirv.cpp:446-447`). LBP3 programs it once per context as a generic ring budget: GSVS {16,0,0,0} dwords per vertex × 64 vertices, and ESGS 128 dwords (`FUN_0005bed0`, below). It is not this shader's count.
  - **What the GS emits.** Each GS has exactly 4 `OpEmitVertex`, no loop, and writes 24 (GS A) or 38 (GS B) components per vertex.
  - **Declared output** (built-in `gl_Position` counted):
    - A: 64 × (4 + 5·4) = **1,536**;
    - B: 64 × (4 + 9·4) = **2,560**.
  - **The limit.** 1,024 is the value 2,772 of the ~2,990 Windows reports on gpuinfo show, and NVIDIA's usual value. **Not yet read from this RTX 3090.**
  - **The checks don't catch it.** shadPS4 never compares against the device limit (`grep maxGeometry` finds nothing). spirv-val doesn't check device limits, and the NVIDIA driver built both pipelines without complaint.
- **Fix:**
  - take `OutputVertices` from the GS's real count, in `BuildRuntimeInfo` (the copy shader's vertex stride), not from `VGT_GS_MAX_VERT_OUT`;
  - add a guard that drops, with a log line, any GS whose output exceeds the device limit. It must parse the copy shader fresh, because the cache doesn't keep `gs_copy_data`;
  - the caller then purges or bypasses the cache entries written at 00:58:46.
  - Details in §4.
- **Unknown:** what SPH error bit 8 means. There is no public table, so the link "over the output limit → SPH 8" is an inference. It is the best fit: right stage, an error raised as the program starts, and the only limit these modules break that I could find. One run settles it (§4).

## 1. What was compiled (on_1, 00:58:46; on_2 and on_3 only reused it)

Three pipeline keys, each with three stages: fragment, vertex (the ES compiled as a Vulkan VS) and geometry. The decoded key stage hashes are `[FS, -, -, VS, GS, -]`.

| Key | PS (meta → spv) | ES as VS | GS | Copy shader (meta) |
|---|---|---|---|---|
| `g_0x4c029a3ae422890d` | `dd549569…` → `0x4299ba393ac46b7d_0` (velocity) | `107cb2fe…` → `0xafab4dd4a4c2cf80_0` | `9261acaa…` → `0xc5e4b26d39f96975_0` (**GS A**) | 0x1bbbe9e00, 79 dwords |
| `g_0x68e9be5f822b7283` | `23603ec6e8b6e39f` (already cached) | `4718b091…` → `0x95fc9845f4fb945c_0` | `da59f111…` → `0xf601cf8219ab7139_0` (**GS B**) | 0x1bbbe8f00, 121 dwords |
| `g_0x8a81d9e032b9c511` | `23603ec6e8b6e39e` → `0x221ff5dcbbbe9151_1` (depth) | as above | as above | as above |

**GS runtime info**, decoded from the `.meta` files (`RuntimeInfo` at byte 32; `hw.gs` at byte 72; layout `runtime_info.h:141-153`). Both GS are identical apart from the copy shader:
- in-primitive 10 (`AdjLineList`); out-primitive TriangleStrip on all four streams; GS mode ScenarioG;
- `num_invocations` 1, `output_vertices` **64**;
- `in_vertex_data_size` (`VGT_ESGS_RING_ITEMSIZE`) **128**, `out_vertex_data_size` (`VGT_GS_VERT_ITEMSIZE[0]`) **16**;
- `num_outputs` 0 (no clip/cull/point-size outputs).

The ES meta has `vertex_data_size` 128. The draw-time state from the log: GS mode 0x180013, which is ScenarioG, cut mode 1, ES and GS write-optimise on, on-chip 0; streamout 0.

**The GS shares its hash with its copy shader:** `vs_copy_hash` = the GS `pgm_hash`. The PSSL compiler gives both the same footer hash. That is harmless: the copy shader is never compiled through `program_cache`.

**No GCN was dumped** (shader dumping is off; nothing newer than 11-10 in `analysis\shaders\dumps`). The GCN of the ES, GS and copy shader exists only in guest memory.

**The SPIR-V** (`perf\gs_hang_files\*.spvasm`):

| Module | Interface | Body |
|---|---|---|
| GS A `0xc5e4b26d39f96975_0` | `InputLinesAdjacency`, `OutputTriangleStrip`, `OutputVertices 64`, `Invocations 1`. `gl_in[4]` plus **31** inputs `gs_in_attr0..30` (vec4[4], locations 0–30). Outputs: `gl_Position` and `out_attr0..4`. | **4 `OpEmitVertex`**, 1 `OpEndPrimitive`, **0 `OpLoopMerge`**. Before every emit it writes all 24 output components exactly once. Reads `gs_in_attr0..4` and `gl_in`, at vertex indices 0, 1 and 3 only. |
| GS B `0xf601cf8219ab7139_0` | Same modes. 31 inputs. Outputs: `gl_Position` and `out_attr0..8`. | 4 emits, no loop, 38 components per emit. Reads `gs_in_attr0..8` at vertices 0, 1 and 3. |
| ES A `0xafab4dd4…` | Vertex. Outputs: `gl_Position` and `out_attr0..4`. | No loop. |
| ES B `0x95fc9845…` | Vertex. Outputs: `gl_Position` and `out_attr0..12`. | No loop. |
| PS `4299ba39_0`, `221ff5dc_1` | Fragment. | No loop. |

- **Memory:** no module stores to a storage buffer. The ES→GS and GS→VS ring accesses were all turned into interface variables. What's left is reads of the game's constant buffers (`ssbo_*`, `NonWritable`).
- **spirv-val** (`--target-env vulkan1.3`) passes all six modules. Under `vulkan1.2` it only complains that the SPIR-V version 1.6 is too new for that target. This validator does **not** check device limits or how stages match, so a pass clears neither suspect.

## 2. How shadPS4 translates this case

- **Program registers.** `RefreshGraphicsStages` binds the stages in this order: FS, then ES (as `SwStage::Vertex`), then GS (`vk_pipeline_cache.cpp:586-703`, the EsGs case `:624-639`).
- **Runtime info** (`BuildRuntimeInfo`, `vk_pipeline_cache.cpp:143-180`):
  - ES: `vertex_data_size` = `VGT_ESGS_RING_ITEMSIZE`.
  - GS:
    - `output_vertices` = `VGT_GS_MAX_VERT_OUT` (`:149`);
    - `in_vertex_data_size` = ESGS item size (`:173`);
    - `out_vertex_data_size` = `VGT_GS_VERT_ITEMSIZE[0]` (`:174`);
    - the copy-shader code is taken from the **VS** program registers (`:176-178`).
- **GS input VGPRs.** For `AdjLineList`: V0 = 0, V1 = 1, V3 = 2, V4 = 3, with V2 the primitive ID (`frontend/translate/translate.cpp:295-322`). That is upstream code (`bfc1a588c`, 2026-07), not a port change.
- **Ring removal** (`ir/passes/ring_access_elimination.cpp`):
  - **ES** (`:60-89`): every GLC+SLC buffer store becomes an output attribute, chosen by its instruction offset: offset < 16 → Position, otherwise `Param(offset/16 - 1)`. It asserts `offset < vertex_data_size*4`.
  - **GS ring loads** (`:106-127`): become `GetAttribute(attr, comp, voffset >> 2)`, with the bucket `soffset/256`.
  - **GS ring stores** (`:128-151`): become outputs through the copy shader's map (`frontend/copy_shader.cpp:10-99`):
    - `comp_ofs = copy.output_vertices*4`;
    - `output_size = VGT_GS_MAX_VERT_OUT * dwords_per_vertex * 4`.
- **The log's warning.** `VERT_ITEMSIZE 16 is different than actual number of dwords per vertex 24` (and 38) comes from `:95-101`. The pass then uses the copy shader's count, so the warning is cosmetic here.
- **SPIR-V emission:**
  - modes (`backend/spirv/emit_spirv.cpp:442-449`; `AdjLineList` → `InputLinesAdjacency` at `:37-38`);
  - `gl_in[NumVertices = 4]` (`spirv_emit_context.cpp:55-56`, `:526-528`);
  - **one input location per vec4 of the ESGS item, minus one**: `in_vertex_data_size/4 - 1` = 31 (`:531-537`);
  - one output per copy-shader attribute (`:753-759`);
  - Emit and Cut come from `s_sendmsg` (`frontend/translate/scalar_flow.cpp:54-77`).
- **The pipeline topology** maps to `eLineListWithAdjacency` (`liverpool_to_vk.cpp:127-128`).

**Upstream history:**
- Before `a146ff2e` (#5072, 2026-09-23), the pass logged `MAX_VERT_OUT {} is larger than actual output vertices {}` and used the copy shader's count for scenario G. So upstream knew that `VGT_GS_MAX_VERT_OUT` can exceed the shader's real count.
- `OutputVertices` has always been the raw register (`927bb0c1`, `3c0c921e`).

## 3. What the runtime and the game supply

- **The runtime's `sceGnmSetEsShader` and `sceGnmSetGsShader`** (`runtime/src/hle/gnm_packets.cpp:128-162` on `fix/gs-stages`) write the same registers as shadPS4's `core/libraries/gnmdriver/gnmdriver.cpp:1563-1625`:
  - ES: SH 0xC8 {lo, 0}, SH 0xCA {rsrc1 with the modifier, rsrc2};
  - GS: SH 0x88, SH 0x8A, context 0x2E5 (streamout), 0x29B (output primitive), 0x2E4 (instance count).
  - The trailing NOP comes out the same: the runtime fills up to `cmd + size`, and shadPS4's `WriteTrailingNop<11>` (`gnmdriver.cpp:103-108`) is a header plus 11 dwords. Both cover the 12 dwords left (20 − 8 and 29 − 17).
  - The values seen at draw time agree: output primitive TriangleStrip, 1 invocation, streamout 0. **The port of these writes is not the problem.**
- **The game writes the ring state itself, once per context, with fixed values.** The context setup `FUN_0005bed0` (two copies) calls:
  - at +0x5c913 / +0x5d0c4: `FUN_00060a70(ctx, esgsRing, 0x400000, 0x80)` → `FUN_00acf650`, which sets `VGT_ESGS_RING_ITEMSIZE` (0x2AB) = **128**;
  - at +0x5c93f / +0x5d0f0: `FUN_00060b10(ctx, gsvsRing, 0x400000, &{0x10,0,0,0} @0xe12920, 0x40)` → `FUN_00acf6b0`, which sets:
    - `VGT_GS_VERT_ITEMSIZE[0..3]` (0x2D7) = {16, 0, 0, 0};
    - `VGT_GSVS_RING_OFFSET_1..3` (0x298);
    - `VGT_GS_MAX_VERT_OUT` (0x2CE) = **64**;
    - `VGT_GSVS_RING_ITEMSIZE` (0x2AC) = 16 × 64 = 1,024.
  - Both functions also put the ring V#s in the global resource table (`ctx+0x58`, +0x20..+0x88). shadPS4 doesn't need them, because the ring accesses are removed.
  - These are budgets: 1,024 dwords per GS primitive covers 4 × 24 = 96 (A) and 4 × 38 = 152 (B). On the PS4 the GS addresses the ring with its own compiled vertex stride, so the oversized `MAX_VERT_OUT` is harmless there.
- **The GS's real count is far below 64:**
  - each GS emits 4 vertices statically;
  - the store mapping produced 24 and 38 *distinct* outputs per emit from 12-bit instruction offsets. That needs `copy.output_vertices` ≤ 42 for A and ≤ 26 for B (24 × 4 × M ≤ 4,096; 38 × 4 × M ≤ 4,096);
  - the copy-shader sizes (79 and 121 dwords for 24 and 38 loads) also fit M = 4 better than 64.

## 4. Ranked causes, how to confirm each, and the fixes

**1. The GS output exceeds NVIDIA's `maxGeometryTotalOutputComponents`, because `OutputVertices` = `VGT_GS_MAX_VERT_OUT` = 64. Likely.**

Evidence:
- the fault is in a program header, very probably the GS's (ESR stage bit 4), and is raised as the GS starts (Xid 13, not a 2-second timeout);
- 64 × 24 = 1,536 and 64 × 40 = 2,560 components, against the usual NVIDIA 1,024;
- 64 is LBP3's global ring budget, not the shader's count;
- shadPS4 has no limit check.

**Confirming it, without the GPU:**
- **(a)** Read this device's limits, once the other agent is finished: `vulkaninfo` (in System32; it opens the device but renders nothing). Look at `maxGeometryTotalOutputComponents`, `maxGeometryOutputVertices`, `maxGeometryInputComponents` and `maxGeometryOutputComponents`. If the first is ≥ 2,560, this cause is wrong.
- **(b)** A guard that is always on, in `RefreshGraphicsStages` (EsGs case):
  - compute `output_vertices × 4 × (num_attrs + 1)`;
  - compare it with `limits.maxGeometryTotalOutputComponents` (add a getter in `vk_instance.h`);
  - if it is over, print one `[gpu] GS over output limit: …` line per program and `return false`, so the draw is dropped and counted as dropped.
  - **Take `num_attrs` from a fresh `ParseCopyShader` of the live copy shader**, not from `Info::gs_copy_data`. That field is filled only during translation, and the cache doesn't keep it: `Info::Serialize` writes only `InfoPersistent`, the flat buffer and the SRT (`vk_pipeline_serialization.cpp:388-394`; `gs_copy_data` is at `info.h:111`, outside `InfoPersistent`). A GS loaded from the warm cache would read `num_attrs` 0, slip past the guard, and run the 64-vertex module.
  - Even with the fresh parse, do the guard run with `LBP3_NO_SHADER_CACHE=1` or after the purge below.
  - One run with `LBP3_GS_STAGES=1` should then show those two lines (A 1,536, B 2,560), no Xid 13, and the draws dropped as today.

**The fix:**
- In `BuildRuntimeInfo`, Geometry case (`vk_pipeline_cache.cpp:149`), parse the copy shader there (`Shader::ParseCopyShader(params_vc.code)`, cached by `vs_copy_hash`). Then set `output_vertices = copy.output_vertices ? min(VGT_GS_MAX_VERT_OUT, copy.output_vertices) : VGT_GS_MAX_VERT_OUT`. This is the pre-#5072 rule, applied to the emitted mode as well.
- Making the change in `RuntimeInfo`, not only in `emit_spirv.cpp`, matters. The specialization compares `output_vertices` (`runtime_info.h:154-160`), so the stale cached permutation stops matching. A fix only in emit would silently keep using the cached `OutputVertices 64` modules.
- Keep the guard from (b) as a safety net.
- `ring_access_elimination.cpp:137` uses `gs_info.output_vertices` only as a modulus, and stays correct with the smaller value.

**One guarded confirmation run** (the fix plus the guard):
- with `LBP3_NO_SHADER_CACHE=1`, or after deleting the 00:58:46 entries: `g_0x4c029a3ae422890d.key`, `g_0x68e9be5f822b7283.key`, `g_0x8a81d9e032b9c511.key`, `0xc5e4b26d39f96975_0.spv`, `0xf601cf8219ab7139_0.spv`, `0x9261acaa31970925.meta` and `0xda59f1116f4eb1f5.meta` (the ES files can stay);
- to the Pod;
- pass if: no guard line, `dropped 0 draws`, and no nvlddmkm 13/153 in the System log.
- **Do the cache cleanup in any case** (the caller's job, with no `lbp3.exe` running; not done here). Those three keys are preloaded at every boot today, harmlessly but uselessly, and would be reused whenever the switch is on.

**No-rebuild variant: not a safe confirm, don't use it.**
- `perf\gs_hang_files\patched_ov4\` holds GS A and GS B with only `OutputVertices 64→4` changed (2 bytes differ, the other is the generator word; spirv-val passes).
- Copied over the same names in the cache, the warm-up would load them as-is: `LoadPipelineStage` does no integrity check (`vk_pipeline_serialization.cpp:248-302`).
- The shader-patch hook can't be used instead: `IsPatchShaders()` is hard-coded false (`runtime/shim/core/emulator_settings.h:32`).
- **Why it isn't safe.** A GS permutation's binding start follows the PS's resource count. GS B was built at start {12, 2}, after a PS with 11 bindings (`221ff5dc_1`: 1 SSBO, 5 images, 5 samplers). But PS `be6a043d…79` (`0x95a226e006f31c03_1`) has **21** bindings (1 + 10 + 10), and so does `aca4148b…a0` (`0x4f5df0405c6bbbd7_1`). `be6a043d…` is drawn with GS B in the Pod (off_1, off_3). So the Pod would compile a **fresh** GS B permutation with `OutputVertices 64` and could fault again. (`f4540f6d…e9` / `0xe3408e078895ef47_1` has 11 and is in off_2; `aca4148b…` is in off_1.)

**2. The GS declares 31 input locations (from ESGS item size 128) where the ES writes 5 or 13. Unlikely to fault; worth fixing.**
- At 31 × 4 = 124 components it is within the usual `maxGeometryInputComponents` of 128.
- The unread inputs are never read, so the driver drops them.
- It does break Vulkan's rule that every input location of a stage is written by the stage before it, and an ESGS item size over 132 dwords would cross the limit.
- **Confirm:** (a)'s `maxGeometryInputComponents`.
- **Fix:** size the inputs from the ES's stored attributes (`infos[Vertex]->stores`) or `copy`-like parsing, capped at the limit. Change it in a separate run from cause 1.

**3. Ruled out** (evidence):
- **Infinite loop:** none of the six modules has a loop, and the GS emits are straight-line. A loop would show as a timeout (event 153 alone), not as event 13.
- **Ring or offchip memory:** all ring accesses were eliminated, and no module stores to memory. A bad address would be an MMU fault (Xid 31), not an SPH error.
- **The register port:** it is identical to shadPS4's; streamout 0, on-chip 0.
- **Primitive and topology mapping:** correct for `AdjLineList`.
- **The PS or the ES, as the faulting stage:** the ESR stage field reads as geometry. That reading is itself an inference (see unknowns).

## What stays unknown

- What NVIDIA's SPH error bit 8 is. Nothing public names it, so cause 1 is an inference until the guarded run. Searches of nouveau, open-gpu-doc and NVIDIA's open-gpu-kernel-modules found no table.
- That the ESR stage field is one bit per shader type, which is what makes 0x10 mean the geometry stage. The SPH text confirms only the SPH bit.
- The exact output limit of this RTX 3090. 1,024 is the common value, not yet read here.
- The copy shader's exact vertex count M (4 is very likely; the bound is ≤ 42 / ≤ 26). The GCN wasn't dumped.
- Whether the GS output is *correct* once it no longer faults. Both GSs read input vertices 0, 1 and 3 and never 2. For lines with adjacency (0 = previous, 1–2 = the edge, 3 = next), that is odd. It may be LBP3's own vertex layout, or a VGPR-order question in `translate.cpp:299-320`. The first screenshot pair will show it.
- How many GS permutations the Pod with two players, Bunkum and the other levels add (binding starts follow the PS).
- Why the runtime logs no device loss after the reset: the presenter keeps showing the last frame. That is separate from the GS.
