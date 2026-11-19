# LBPPC against our open items (2026-11-14)

LBPPC upstream is at `951fb8d` (0.1.3), one commit past our clone's `aad6a91`; 0.1.3 was already studied in [lbppc_013.md](lbppc_013.md). This pass maps their code onto what's left on our side. Licences: their shadPS4-derived files are GPL-2.0-or-later; their runtime (`src/*.c`) and launcher are GPL-3.0-or-later. **Since our move to GPL-3.0-or-later (fork main `35fc071`, 2026-11-14) both can be used directly** in the runtime repo, keeping their notices and crediting them (the shadPS4 fork stays GPL-2.0-or-later, so their runtime code goes in the runtime repo).

## 1. Exact math online (`src/probe.c` `exact_math_install`, `src/exact_math_sites.inc`, `tools/exact_math_sites.py`)

- **The problem:** online co-op runs both players' worlds in lockstep and compares them ("The game diverged"). `vrcpps` and `vrsqrtps` are approximations whose results differ between CPU makers (a PS4 never had this: every console is alike), and `vdpps`'s summation order isn't fixed either.
- **Their fix:** online by default (`LBPPC_EXACT_MATH=0/1` overrides), each of 3,374 sites in the eboot and spu.prx jumps to a stub: `1/x` as `vdivps` by a broadcast 1.0, `1/sqrt(x)` as `vsqrtps` then `vdivps`, `vdpps` as multiply, blend, two `vhaddps` ((x0*y0+x1*y1)+(x2*y2+x3*y3)) and a blend. Sites shorter than 5 bytes move the following instruction(s) into the stub (RIP-relative fields fixed up). The site list comes from `llvm-objdump -d` of the code segments: every `vrcpps`, `vrsqrtps`, `vdpps` (packed only, no scalar forms).
- **Why it matters here:**
  - this PC is an **AMD Ryzen 9 5900X**: a friend on Intel can diverge from the user;
  - **cross-port co-op:** an LBPPC player has exact math on online, so one of ours without it computes different results even on the same CPU.
- **Ours:** none ("exact-math patches" were on the not-done list).
- **Plan:** take their site list and stub generator (now allowed, with their notice), and regenerate the list from the dump with their method as a check (expect the same 3,374). Division and square root are correctly rounded, so they're bit-identical everywhere; the `vdpps` replacement must use the same addition order. The spu.prx sites are patched when the game loads it. On when online, a switch to force it either way. Tests:
  - stub unit tests on random inputs against exact references;
  - the regression with it forced on offline;
  - ideally our port and LBPPC (installed in `D:\Recomps\lbppc`) in co-op on this PC, with it on and, as a control, off.

## 2. Background shader builds (`vk_pipeline_cache.cpp`, GPL-2.0-or-later; default on since 0.1.3)

Already analysed in [lbppc_013.md](lbppc_013.md) §5: a new pipeline builds on 1-4 workers and its draws are skipped meanwhile. Their worst hitch went 4.5 s → 0.3 s; new things show about a second late. **Plan:** port as an opt-in (launcher switch), let the user judge freezes vs pop-in.

## 3. Busy-level slowdowns (Diner)

`LBPPC_WRITE_FAULT_NO_READBACK=1` (on by default for them, "faster in busy levels, nothing found wrong", `buffer_cache.cpp` ~272): skip the copy-back when the game writes into GPU-written memory. Ours takes ~1,380 write faults per fluid frame (perf/gfx_wait.md). **Plan:** an A/B experiment on the Diner stretch plus the full regression (risky area for us: the dark-Pod and fog history).

## 4. 4K and frame presentation

They present straight from the GPU (shadPS4's Vulkan swapchain, `vk_presenter.cpp`). We render headless, read every frame back (0.7 ms at 1080p, +0.6 ms at 1440p; at 4K with frame generation roughly 4-6 ms) and upload it to an SDL D3D11 window. 4K's extra ~12 ms is mostly GPU drawing, so direct presentation (or D3D11/Vulkan shared textures) would help 4K by a few ms. It's a large rework of the window, overlays and pacing. **Not now.**

## Nothing there for

- **Brightness (ticket 17):** never addressed in LBPPC. Our own lead remains: whether the game's brightness setting feeds the final composite ([lbppc_graphics.md](lbppc_graphics.md) §5).
- **Introduction transition stalls:** their file reads are plain (`runtime_file.c`), nothing to borrow.
- **Stuck in the Pod with a friend:** 0.1.3 says "we know what triggers it" but doesn't say what.

## Worth knowing

- **Dive In with strangers is blocked by design** on both ports: joins are friends-only, the protection against the "force join" exploit that closed the original servers (their docs/online.md). Not a bug.
- **Their known ultrawide issue:** "some UI (bottom button prompts) stretched about 1.34x at ultrawide" (docs/feature-status.md PC-04). We use the same patch addresses, so ours likely shows it too. Check at the next ultrawide session.
