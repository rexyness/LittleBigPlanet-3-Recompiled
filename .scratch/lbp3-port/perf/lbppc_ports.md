# LBPPC's ideas, ported (2026-11-14)

From [research/lbppc_vs_open_items.md](../research/lbppc_vs_open_items.md), after the move to GPL-3.0-or-later (fork main `35fc071`), which lets LBPPC's runtime code be used directly. Three items; the user said "get to work".

## 1. Exact math online (runtime `feat/exact-math` `419b87e`; not in main yet)

- **The problem:** online, LBP3 runs every player's world in lockstep and compares them ("The game diverged"). Its `vrcpps`/`vrsqrtps` are approximations whose bits differ between AMD and Intel; `vdpps`'s addition order isn't fixed. This PC is an AMD Ryzen 9 5900X.
- **The port:** LBPPC's (emurphy04, GPL-3.0-or-later, 951fb8d) stub generator and their 3,373-site list (3,085 in eboot.bin; 288 in spu.prx, LBPPC's image offset 0x16c0000, verified from their log's "Starting linked module 6 at image offset 0x16c0000"). Each site jumps to a stub: 1/x = `vdivps` by 1.0, 1/sqrt(x) = `vsqrtps` + `vdivps`, dot product = mul, blend, two `vhaddps`. Correctly rounded, so the same bits on every CPU.
- **Ours beyond LBPPC:** every site and moved instruction checked with Zydis first (the instruction, 128-bit, no rsp operand, nothing moved that branches or uses the stack, RIP field as listed; a failing site is left as it is and logged); the `vrsqrtps` stub re-aims a RIP-relative operand (LBPPC's doesn't); spu.prx patched in place wherever it's loaded.
- **When:** online (levels server or shadNet), as LBPPC; `LBP3_EXACT_MATH=1/0` forces it.
- **Version safety:** the co-op link's HELLO carries capability 2 (exact maths). A connection between a game with it and one without is refused with a log line ("Both players need the same version (v0.1.3 or later…)") instead of drifting apart later. So **it can only ship as a release everyone updates to**; it is not in the user's live build.
- **Cross-port co-op isn't possible anyway:** our players use our own link protocol ("L3P1"), LBPPC's theirs. And LBPPC's game threads run with Windows' default MXCSR (0x1f80: `runtime_thread.c` `host_start` sets none), ours with the PS4's 0x9fc0 (gap 8), which would also make their worlds and ours drift apart. Worth telling the LBPPC team (their AMD/Intel players already match each other).
- **Tests:**
  - `exactmathtest` (offline): every stub form (low/high registers, RIP operands, moved instructions with and without their own RIP operand, `vdpps` with 7 masks) run from 3,000 random states through an assembly harness, original against stub, under MXCSR 0x1f80 and 0x9fc0: destination exact, all other ymm (full width), flags and the red zone unchanged; the checks refuse 8 unsafe forms. 90,068 checks, 0 failed.
  - `p2ptest`: an exact-maths player and an older one are refused, two exact ones connect. 176 checks, 0 failed.
  - In game, forced on (`em_regress.sh emrg`): all 3,373 sites pass the audit; keyboard, photo, backup dialogs, co-op pass; Introduction 0.25% / 0.00%; Thruster 3/3; Bunkum 16/16 with Needlepoint; sky cyan; no driver events.
  - Cost, heavy Needlepoint walk, on/off alternately: work 20.31/19.09 ms on vs 18.81/19.54 off (≈ +0.5 ms, ~3%); standing 18.26/17.01 vs 16.86/16.92; 0 frames over 40 ms in all.
- **Not proven:** the cross-brand benefit needs an AMD-Intel session; one AMD PC can't show it. What's proven: the stubs are exact and the game runs normally with them.

## 2. Background shader builds (shadPS4 `84e058af`; runtime `feat/async-only` `8cf8548` + `eccdd1e`)

- **The port:** LBPPC's async pipeline builds (GPL-2.0-or-later, 951fb8d; on by default there since 0.1.3). A new graphics pipeline is set up on the GPU thread and built by the driver on 1-4 workers (4 here); its draws are skipped until it's ready; it's saved to the cache once built.
- **The refactor:** `GraphicsPipeline`'s constructor split into the guest-state part (vertex inputs, multisampling, auxiliary tessellation and discard shaders, which used to be made while listing the stages) and `Build()`, which reads only the key, the kept stages and modules and the serialization data. Without the switch the constructor calls `Build()` at once.
- **Switch:** `LBP3_ASYNC_PIPELINES=1`; the launcher's Shaders card "Build new shaders in the background" (`async_shaders`, off by default).
- **Measured** (`_tools\bg_ab.sh`), heavy Needlepoint route from cold caches (an empty shader cache and an empty NVIDIA driver cache via `__GL_SHADER_DISK_CACHE_PATH`):

  | | Off (GPU thread) | On (background) |
  |---|---|---|
  | Walk: frames over 40 ms / worst | 20 / 1,752 ms | 1 / 138 ms |
  | Standing: frames over 40 ms / worst | 26 / 1,962 ms | 0 / 33.7 ms |
  | Session: frames over 40 ms / worst | 81 / 6,040 ms | 32 / 3,033 ms (the game's own load pause, also in warm runs) |
  | Pipeline building on the GPU thread | 19.7 s (570 pipelines) | 0.2 s (522) |

  Screenshots (`analysis\screenshots\m2_m5_bg_async*`): the off run was so far behind that at the same moments it was still on the map and in the level's intro cutscene; the on run's level shots are complete, including 20 s later. GPU warnings 0.
- **Open:** the taste call (no freezes vs things appearing up to about a second late, the first time); friends start with empty caches, where it matters most. Frame generation's warps for the frames where a draw was skipped weren't looked at separately.

## 3. Skipping the copy back on write faults (added `2ab6e4f7`, reverted `9423d341`)

LBPPC's `LBPPC_WRITE_FAULT_NO_READBACK` (on by default there). Ported, then measured on the heavy route (4 runs, off/on alternately): `BufferCache::ReadMemory`'s write-fault path was never reached ("0 read back" in every window; the switch's first-use line never printed). Our earlier changes handle those writes before they get there, so the skip has nothing to skip. Reverted. The Diner slowdown (perf/diner_1440.md) remains unexplained by it.
