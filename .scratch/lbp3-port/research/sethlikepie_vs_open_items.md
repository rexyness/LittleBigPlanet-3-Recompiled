# sethlikepie's LittleBigPlanet 3 PC against our open items (2026-11-19)

The user asked: "analyze https://github.com/sethlikepie/littlebigplanet3-pc and see what you can bring to my version". This is the read-only study, laid out like [lbppc_vs_open_items.md](lbppc_vs_open_items.md). Four lanes did the reading, and their reports hold the detailed citations:

- [sethlikepie_fps60.md](sethlikepie_fps60.md): their engine-level 60 fps against our frame generation;
- [sethlikepie_gpu.md](sethlikepie_gpu.md): their shadPS4 (sgdc3's LBP3 fixes and their own changes) and performance;
- [sethlikepie_features.md](sethlikepie_features.md): upscalers, mouse in Create, free camera and teleport, the F1 menu, diagnostics;
- [sethlikepie_runtime.md](sethlikepie_runtime.md): HLE libraries, online, launcher and scripts.

**Labels**, as in the lanes:
- **read**: from code;
- **seth's claim**: from their comments or docs, not measured by us;
- **inferred**: our reasoning.

**Citations:** their code is cited as `seth:<path>:<line>` at `07cc7e7`. Nothing of theirs was built, run or executed.

## What it is

- **The repo:** sethlikepie/littlebigplanet3-pc `07cc7e7` ("0.1.1"), one squashed commit pushed 2026-11-18, made by sethlikepie and Emre_exe. Their README: "high frame rates, FSR 3.1 / DLSS, PC settings".
- **A third lineage, neither ours (PurpleGitty's) nor LBPPC's.**
  - It descends from deadinside28's bloodborne_pc, through Supermedo's Windows port and FelipePrado0's gow3_pc, retargeted to LBP3.
  - **Parts:**
    - a C runtime (`src/`);
    - an MSYS2 CLANG64 build;
    - a vendored shadPS4 (`gpu/shadps4`): upstream `b4e7ae7`, recompiler backports, sgdc3's `lbp3-compat` GPU commits and about 1,000 `lbp3:` changes;
    - a PC-feature shim (`gpu/shim`);
    - Tkinter and native launchers.
  - It shares bloodborne_pc ancestry with LBPPC, but the shared files differ heavily (`runtime_pad.c` by 1,459 diff lines, for example). 17 runtime files exist only in theirs. So our LBPPC studies cover almost none of it. **read**
- **Same eboot.** Their patch tool and bug report check `eboot.bin` for sha256 `4e335c5b…8534bf`, and our dump's `eboot.bin` has the same hash. **Every eboot address in their code applies to ours unchanged.** **read**
- **Licence:** GPL-2.0-or-later; the DLSS and XeSS bridges are MIT. It can go into our runtime (GPL-3.0-or-later) and our shadPS4 fork (GPL-2.0-or-later), with their notices kept and credit given. **read**
- **Most of their performance figures aren't LBP3's.** `docs/parallel_gpu.md` and most of `docs/upscaler.md` were measured on God of War III, on Linux, with an RX 7800 XT. The LBP3-specific claims are the ones in code comments. **read**

## In short

- **Where they're ahead of us:**
  - PC features: engine-rendered 60 fps and above, temporal upscalers, mouse-driven Create, a free camera and teleport, an in-game settings menu, an F12 report;
  - a long tail of shadPS4 work.
- **Where they're behind us:**
  - **Online.** They have no remote co-op transport: P2P sockets stay on the loopback (`seth:src/runtime_netloop.c:17`). They talk only to an LBP game server ("Bonsai"), and the player supplies the patch for it.
  - **System dialogs.** Theirs complete at once and draw no UI.
  - **Exact maths.** They have none, so their Intel players should hit the intro stall our v0.1.6 fixed (inferred).
- **Nothing in their tree for:**
  - the Intel 12th-gen crashes;
  - the four open online items (rejoin with no spawn, lost-message bursts, other networks, Dive In);
  - flicker.
- **They have no online gate on anything that writes game state.** All of these run online in their build:
  - the free camera, whose pause skips simulation steps;
  - teleport;
  - the Create cursor;
  - the 60 fps mode.
  - **Whatever we take from these must be gated on our own online state.** Don't gate on their name for `0x254270` ("is online"), which is unverified.
- **Corrections to my first look, in chat:**
  - LBP3 doesn't import AJM or Videodec; their code for those comes from God of War III.
  - AvPlayer is reached only by the Share-video gallery and online video overlays, and it is never called in any of our logs.
  - **So nothing a player sees is missing in ours.**
  - There's no Intel hybrid-CPU handling in their tree either.

## Ranked: what to bring over

### Tier 1: small, safe, and on our open items

These fit one runtime branch and one shadPS4 branch.

- **Checked against both sources** for this summary (not only the lanes' reading):
  - item 1: seth's `ExtendWriteFault` uses `mask & ~gpu[index]` (`seth:gpu/shadps4/video_core/buffer_cache/region_manager.h:180-199`), and `LBP3_FAULT_WINDOW` defaults to 256 KiB (`buffer_cache.cpp:194-195`);
  - item 2: our async path returns `nullptr` for every draw until its pipeline is built (`shadPS4:src/video_core/renderer_vulkan/vk_pipeline_cache.cpp:712-743`);
  - item 3: our `SubresourceExtent` compares with the defaulted `operator<=>` (`texture_cache/types.h:63-67`);
  - item 4: `EmitWriteLane` returns `u32_zero_value`;
  - items 5 and 7: as marked in the table.

| # | Item | Ours today | Effort | Risk | Open item | Lane |
|---|---|---|---|---|---|---|
| 1 | **Write-fault window:** a guest write fault also unprotects the clean, buffer-only pages in the aligned 256 KiB around it, using their mask that skips GPU-written pages (`mask & ~gpu`) | One page per fault. gfx_wait.md's "fix 2" planned exactly this | S (about 40 lines plus a switch) | Medium (coherence; the dark-Pod area) | Diner and fluid frames (about 1,380 faults per fluid frame) | gpu §2a |
| 2 | **Wait policy for background shader builds:** some draws wait for their pipeline: full-screen and indirect draws, one-time targets, frames under 300 draws (loading screens, menus), photos and thumbnails. Every other new draw is still skipped, as now | `84e058af` skips every draw. Seth's LBP3 claim: when every draw is skipped, the Pod's decoration comes up as bare cardboard | S–M | Low | First-visit pauses. It makes background shader builds safe to turn on by default | gpu §2e |
| 3 | **sgdc3 fixes we lack:** `7184f52` (reset stage state before loading a cached pipeline), `80ffa53` (per-dimension extent compare), and the one line left of `2966581` (the clear remap) | Missing | S each | None or very low | Correctness after cache damage | gpu §1 |
| 4 | **Recompiler bundle:** `eb51f01` (`V_WRITELANE_B32`, plus the rcp/rsq clamps), upstream `#5346` and the P6 SNORM clamp. Add F91 (the `s_and_b64 exec` scope) only if a scan finds lit-scene shaders using it | Missing. Our `EmitWriteLane` returns zero for the whole register (`emit_spirv_warp.cpp:41-43`, checked) | S, plus one cache rebuild limited to the affected programs | Low | Correctness. Ticket 17 only if the scan says so | gpu §1, §5 |
| 5 | **Frame-generation cut rule:** the camera position at `+0xd0` in the render camera block. A cut is a move over 150 units and 6× the recent step, or a turn over 6° and 6× the recent turn | The largest jump in floats 12–14 at `+0x20` (`videoout.cpp:618-641`: where it reads is checked). Per seth's layout that's the translation row of view×proj, which mixes position and rotation, so a turn isn't measured as a turn and the thresholds are tuned on a mixed quantity (inferred) | S (half a day) | None | fg_present.md follow-up 1 | fps60 §6 |
| 6 | **Save backup before every Play**, keeping 10 | `BackupSaves` runs only from the button and before a restore | S | None | Save safety (the 2026-11-13 save incident) | runtime §5 |
| 7 | **Windows 11 timer-resolution opt-out:** `SetProcessInformation` with `PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION` | `timeBeginPeriod(1)` only (`kernel_time.cpp:55`, checked) | S (one call) | Timing only. Gate it on Introduction steps ≠ 1 and Thruster 3/3 | Co-op hitches for a Windows 11 friend who minimizes or covers the game (inferred; this PC runs Windows 10) | runtime §4 |
| 8 | **Garlic heap scaling** with the render size (`0x5bf56`, +320 MiB per extra 1080p of pixels) | Only by hand (`LBP3_RES_POOL_MB`) | S | Low | 4K safety | gpu §3 |

### Tier 2: medium, player-visible or tooling

| # | Item | Effort | Online | Lane |
|---|---|---|---|---|
| 9 | **Create mouse, buttons and wheel to the pad, while a Popit is open:** left = Cross, right = Square, middle = Triangle, back = Circle, wheel = d-pad. First, a cheap check: place a held object with our touchpad mouse. Seth dropped that approach for "weird placing" | S–M | Safe (pad input) | features §2 |
| 10 | **In-game report key (F12):** saves a screenshot, the place in the game, the context and the last 30 s of input at that moment; the launcher zips it | S–M | Safe (read-only) | features §5, runtime §7 |
| 11 | **DLC straight from `.pkg` files**, by reading their first 0x80 bytes. This drops `pkg_extractor.exe` from a friend's setup | S | No new risk | runtime §6 |
| 12 | **Player-1 state readout** (position, Popit and pause flags, SimFrame) and **pad replay keyed on the simulation frame** (seth's is keyed on wall-clock time), for the self-driving tests. Whether replay is deterministic is unproven | S–M each | Safe | features §3, §5 |
| 13 | **"Where the player is", read from game memory** (CGame `0x12ecc80`: Pod, Create, the story level's name, the player count), written as a `[place]` log line for Discord and the launcher | S–M | Read-only | runtime §7 |
| 14 | **FireBurn/FSR-Vulkan's checked-in shader headers** might unblock the FSR 3.1 frame-generation spike that stalled on `FidelityFX_SC.exe`. The upscaler headers are confirmed; the frame-generation folder was seen by name only. Take seth's leak fix (patch 0003) too | M | Safe | features §1 |
| 15 | **Masked fill applied to the image (their F81)**, which is our unfinished F3 in reuploads.md | M | — | gpu §2i |
| 16 | **Trophies:** an unlock store, a popup and a list. The names need the player's own console trophy key; the pictures don't | M | None | runtime §2 |
| 17 | **Honour the game's own HTTP timeouts and abort.** Log the values the game passes first | S | None | runtime §3 |

### Tier 3: large, or offline-only

| # | Item | Effort | Online | Lane |
|---|---|---|---|---|
| 18 | **Render/output size split, then temporal upscalers.** First the split: display buffers and UI at the output size, the scene at the render size, plus their six refraction and in-world-TV redirects, with the game's own last pass doing the scaling. Then the upscalers (FSR 3.1, DLSS, XeSS, TAA): jitter as a host viewport offset, computed camera and object motion vectors, a redirect at the display pass. The result lands in the guest display buffer, so our readback and D3D11 present pick it up as they are. **But regression risk is high:** our frame-generation warp would read render-size, jittered velocity and depth (it needs rescaling, and its disocclusion checks see the jitter), and photo and icon captures may come out sub-pixel jittered. Only GPU drawing shrinks: the readback and upload stay at the output size | M, then L | Safe (host-only). Keep the simulation's aspect at 16:9 | features §1 |
| 19 | **Absolute Create cursor** (writes 7 copies of the cursor point, overrides the cursor velocity), **teleport** (the game's own warp plus resets), **free camera** (overrides the render camera and restores it; its pause skips steps), **Hide HUD** (stubs the UI draws) | M–L | **Offline only.** The cursor, teleport and the free camera's pause write simulation state (class D). The free camera itself is class C. Hide HUD is render-only, but it also stubs a script UI pass not yet shown to be side-effect free | features §2–3 |
| 20 | **Coarse shading (VRS) on Sackboy's fabric and fuzz at 2560 wide and up.** Seth's LBP3 4K claim: 3.9 ms and 1.8 ms of fragment work close up | M, opt-in | Safe | gpu §2i |
| 21 | **Engine 60 fps** (their mode 23, the default): a second, real render per simulation step, with camera, objects, poses, particles and UI interpolated and every edit restored | L: 5–9 weeks plus perf work. With the second render, a heavy pass on our path comes to about 31 ms (19.2 ms today + 11.8 ms) of the 33.3 ms slot, so expect frequent fallback to 30 | **Unproven:** there's no online gate in theirs and it was never run in lockstep. Keep it off online if it's ever ported | fps60 |

### Not worth it, or not applicable

- **Their draw-prep workers, two-stage draw pipe and per-draw memos:**
  - our GPU thread isn't the limit: in a fluid frame it reaches its wait 7.6 ms into the 33 ms;
  - L effort and high risk;
  - LBPPC turned its own draw pipe off because it corrupted memory.
- **sgdc3's `6f4b3cd` + `3a4d9b0`** (defer mid-frame refreshes by a frame) break the stream order that ticket 17 proved the PS4 keeps. Seth needed further patches to undo the damage.
- **sgdc3's `64abeb2`** (slack on event-queue waits during compiles) answers no symptom of ours.
- **Their stacked-targets choice (ATLAS, off): keep ours on.**
  - Their own comment says the PS4 reads those targets as one texture.
  - Their measurement, "Manglewood 21–26% darker" with it on, makes a third scene where the faithful path is darker. It corroborates `37508fd9`.
- **Things that need a Vulkan swapchain** (we present through D3D11): Reflex and Anti-Lag 2, their ImGui Vulkan overlay, their FSR 3 frame-generation present.
- **The rest:**
  - the video recorder, the F83 scan and the readback queue;
  - their memory model (one committed section);
  - their mods loader (God of War III's layout);
  - launcher translations, and separate Standard and Online builds;
  - AvPlayer, AJM, MP3 and Videodec;
  - every online piece;
  - their dialogs.
- **Already have:**
  - JPEG;
  - the guest threads' FPU state (0x9fc0 / 0x037f);
  - window fit and the hang sampler;
  - tolerance for `tv_nsec == 1e9`;
  - PS4 save import (ours is safer);
  - GPU readbacks off, and an adaptive fallback;
  - 5 of sgdc3's 13 fixes: `c90754b`, `9d38194`, `1f45940` (by our own route), `174a8f4` (a policy) and nearly all of `2966581`.

## By open item

- **Brightness (ticket 17).** Nothing in seth targets it. Four leads:
  1. Their ATLAS note corroborates our stacked targets, so keep them on.
  2. Scan the 135k shaders with the `tools/res1440_fullscan.py` decoder for F91's `s_and_b64 exec` form and for the clamp and WRITELANE opcodes. A hit in lit-scene shaders is a lead; otherwise the bundle is plain correctness.
  3. **The gamma functions:**
     - at default settings the game never calls `sceVideoOutAdjustColor_` or `sceVideoOutColorSettingsSetGamma_`, which are stubs in ours: there is no call in any of our logs;
     - so they don't cause the over-brightness;
     - **untested:** moving the brightness slider. If that calls them, the slider does nothing in ours today, and seth's gamma path (shadPS4's) is the fix.
  4. Optional, and your call: run their build (an untrusted binary) on the same eboot for a WELCOME shot. It would show whether the brightness is shared shadPS4 behaviour.
- **First-visit shader pauses:**
  - item 2, then background shader builds on by default (your call);
  - their staging prewarm only after a measurement.
- **Diner and fluid frames:** item 1.
  - First, gfx_wait.md's step 1: log the fault addresses, to see how many are sequential writes to clean, buffer-only pages.
  - Seth tried and rejected "hot pages" (never re-protecting pages that are written often): 33 fps and a GPU ring timeout. Don't go that way.
- **4K cost:** items 8, 15, 18 and 20.
- **Intel 12th-gen crashes:** nothing in either half of their tree.
- **Online (rejoin with no spawn, bursts, other networks, Dive In):** nothing. Items 7 and 10 help indirectly.
- **Create mode with keyboard and mouse:** items 9 and 19.
- **Test rigs:** item 12, and teleport (item 19, offline).

## The lockstep rule for anything taken from seth

| Class | What it writes | Online |
|---|---|---|
| A. Pad or touch synthesis | Only what the game reads from player 1's pad | Safe |
| B. Host-side render only | Vulkan state, extra passes, the presenter | Safe |
| C. Guest render state, restored | Guest memory the renderer reads, put back afterwards | Unproven: offline until a sync-probe test passes |
| D. Simulation state | Memory the simulation reads, calls to the game's state functions, skipped steps | Diverges: offline only |

- Gate on our own online state (`LBP3_ONLINE*`), next to `LBP3_RES_ASPECT_SPLIT` (`res_patches.cpp:375-380`).
- For anything in class C that we'd want online, run two instances with `LBP3_SYNC_PROBE=1`, one with the feature on and one with it off, for 10+ minutes over several levels and Create.

## Found on the way (our notes and repos)

- **`perf/fps60_timing_re.md`** reads `DAT_01061a04` as a body-copy selector "(= 1)". Seth reads it as a per-RenderPrep stamp, backed by an experiment. Recheck.
- **`perf/best_gfx.md`** calls `3a0615bd0d8d3a5e` "the composite". Seth's profile calls it the SMAA weights pass, with the post composite at `33c89263dcb0a389` and the display composite at `5ed16706fc9e0eb1`. Recheck.
- **The FSR 1 window upscale `d06a0a1`** (`exp/upscale`, perf/upscale.md) is on no branch of the runtime repo now, but the tag `archive/exp/upscale` keeps it, so it's safe. (The features lane looked only at branches.)
- **Their mode 23 changes flip state for good:** it overwrites the frame time `0x1202b28` and doubles the flip counter. Two readers of that state, `0x25485f` and `0x254872`, sit inside our confirmed step function's address range (inferred). This matters only if the 60 fps mode is ever ported.
- **gzip:** their claim that LBP3 "inflates itself" contradicts our r3 measurement against Refresh (`best/online.md`). Keep ours.
- **`SanitizeCopyLayers`** printed 15,904 warning lines in our logs. sgdc3's `1f45940` quiets it (cosmetic).

## Decisions that are yours

1. **DLSS.** Is it OK for our GPL-3.0 runtime to load, at run time, a bridge DLL that statically links NVIDIA's NGX library? That is seth's approach, with players adding `nvngx_dlss.dll` themselves. It only matters at item 18's DLSS step. FSR 4 assets should never be redistributed either way.
2. **Background shader builds on by default** once item 2 is in.
3. **Trophies** are outside the map's offline finish line (`map.md`). In or out?
4. **Community XML patches.** Seth loads shadPS4/GoldHEN patches. Only if you want them: both co-op players would need the same set, so the L3P1 HELLO would carry its hash and refuse a mismatch.
5. **Running their build** for an independent brightness shot (an untrusted binary).

## Suggested first branches

**Batch 1 (items 3, 5, 6, 7, 8) is ported and tested:** [perf/seth_ports.md](../perf/seth_ports.md) has the runtime branch `feat/seth-small` (`cbe2c29`) and the shadPS4 branch `lbp3-seth-small` (`694fe2ce`), pushed to the forks and not merged.

**Batch 2 (items 1, 2 and 4) is ported too,** on `feat/seth-batch2` and `lbp3-seth-batch2`, on top of Batch 1; see the same file.
- The shader scan made item 4 more than plain correctness: `v_rsq_clamp_f32` is in nearly every LBP3 shader, and `v_writelane_b32` ran wrong in 43 shaders the player's cache holds.
- Item 2 fixes a real bug: with background builds on and a cold cache, the poster reflected in the Pod's window goes missing.

1. **Runtime `feat/seth-small`:**
   - item 5;
   - item 6;
   - item 7, with its gate;
   - item 8, in the resolution table.
2. **shadPS4 `lbp3-seth-small`:**
   - item 3;
   - item 1: the fault-address log first, then the window behind a switch, off by default until the A/B;
   - item 2, on `LBP3_ASYNC_PIPELINES`.
3. **Then the opcode scan, and item 4** with one cache bump.
4. **Credit:**
   - sethlikepie and Emre_exe in `CREDITS.md`;
   - sgdc3 for its fixes;
   - each file's notice kept.
