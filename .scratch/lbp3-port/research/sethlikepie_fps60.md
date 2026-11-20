# Lane (sethlikepie study): seth's "60 fps (interpolated)" engine mode against our frame generation (2026-11-19)

Part of [sethlikepie_vs_open_items.md](sethlikepie_vs_open_items.md). The question is what the user's port can take from sethlikepie/littlebigplanet3-pc `07cc7e7` ("0.1.1"). This lane covers its engine-level 60 fps mode: the game renders a second frame per 30 Hz simulation step.

- This was read-only. Nothing from seth's clone was built, run or executed.
- **Labels:**
  - **read**: from code;
  - **seth's claim**: from seth's comments or docs, not measured by us;
  - **inferred**: our reasoning.
- **Citations:**
  - `seth:<path>:<line>` (the clone is temporary). **A bare `:N` means `seth:src/runtime_lbp3_60fps.c:N`.**
  - `lbp3-recompiled:<path>:<line>` for `D:\Recomps\lbp3-recompiled`;
  - `notes:<path>` for `D:\Recomps\lbp3-pc\.scratch\lbp3-port`.
- **Missing documents.** Seth's notes cite `research work/research/R12_60fps.md`, which holds the write-probe results and sections 9–19. It is **not in the repo**. Neither are `60FPS_FIXES.md`, `B5_CULL.md`, `B3_MANGLE.md`, `F119.md` or `F94_120hz_readiness.md`. Everything below comes from code and code comments.

## Answer in short

- **What it is (read).** After each pass's simulation steps, a hook on the main loop's `RenderPrep` call replays the render half of the main loop once more. It uses a camera and every object's render matrix set halfway between the last shown state and the new one, and it flips that frame at 60 Hz. It then restores what it changed bit-exactly and lets the real frame run.
  - Each retargeted call is byte-checked first. 66 source lines call `retarget_call`; some run in loops over site lists and some are diagnostics, so the number of sites is not exact.
  - Dozens of per-draw clocks are held so nothing animates twice per step.
- **What ships is mode 23 ("X"), on by default, not mode 3 (read).** The header comment that says "mode 3" is stale.
  - Modes 19–23 are not features inside mode 3. Each is mode 3 plus a cumulative feature set.
  - The extra flip is **not** hidden from the game in the shipping mode. Only test modes 6–18 hide it.
  - Above 60 there is a variable-rate path that goes up to 1000 presents a second, with real rendered frames.
- **Lockstep (read + inferred).**
  - There is no online gate in the 60 fps code.
  - Seth's tree shows no remote co-op transport. His own comments say so, and the offline Matching2 table answers "signed out". Nothing in this tree shows the mode was ever run in lockstep.
  - The design keeps the confirmed simulation untouched as far as the code shows: no predicted step in the extra frame, no input preview, logic calls left out, and edits restored before the next step.
  - Some persistent state does change:
    - the flip counter and flip times. Two of their readers lie inside the confirmed step function, judging by address ranges (inferred);
    - the RenderPrep stamp;
    - the dim overlay;
    - the motion-blur camera at cuts.
  - **Verdict:** unproven for "The game diverged". If it is ever ported, keep it off online until a two-instance sync-probe test passes.
- **Against our warp frame generation (inferred, partly seth's claim).**
  - Its in-between frames are real renders, with correct disocclusion, UI and the popit.
  - The cost is about a second full render per pass. On our synchronous submit and readback path that likely doesn't fit in heavy levels.
  - Latency is lower for the world and camera. For the local player it is a little lower than ours: +16.7 ms against our GPU warp's measured +19.3–19.9 ms.
  - Getting it right took seth 76 numbered fixes plus 9 playtest bug series in 12.4k lines, and some are still open (cloth).
- **Verdict:**
  - **Not worth porting as a whole now** (L: about 5–9 weeks plus perf work, high risk).
  - Several pieces of address and RE knowledge are directly reusable thanks to the identical eboot.
  - One small FG improvement (the cut rule) is worth taking.

## 1. How it works, and which mode ships

### The mechanism (read)

- **Hook points:** `seth:src/runtime_lbp3_60fps.c:78-97` (header), installed at `:11192-11195`.
  - `0xb89c`: the main loop's `call RenderPrep(gGame, alpha)` (`0x2514d0`) goes to `lbp3_render_prep_hook` (`:7538`).
    - It runs the **extra pass** (`run_mid`, `:5772-5874`) first, then the real `RenderPrep`.
    - The extra pass is `RenderPrep` followed by the render-only calls 1–16 of the main loop (table `:2390-2411`), the flip `0x5caf0(1)` and calls 18–20 (scene-thread wait, scene counters, `0x24aef0` "end render stage").
  - `0xbb09`: the main-loop flip goes to `lbp3_main_flip` (`:7834`): flip rate, pacing statistics, the adaptive fallback, and `0x1202b28` set to the pass time (`:7913`).
  - The 13 other flip callers (loading screens, movies, dialogs, catch-up; list at `:160`) go to `lbp3_paced_flip` (`:8023`), which keeps them at flip rate 1 (30 Hz).
  - `0x25421b`: the input-preview call at the end of `RenderPrep` goes to `lbp3_after_predict_hook` (`:1963`). There objects are interpolated (extra frame) or recorded (real frame).
  - `0x251260`: the `GetNumPredictedFrames` (`0x35da40`) call goes to `lbp3_num_predicted_hook` (`:1996`).
- **The calls the extra pass replays.**
  - The default mask is `MID_DEFAULT` (`:203-206`): all render calls except the world's deferred-list drain (`0x256f90`) and the draw-list reset.
  - The per-pass logic calls are left out: UI update, frontend update, player colours, UI smoothing, and things-every-pass `0x19d170`.
- **Timing.**
  - A pass that presents twice sets `sceVideoOutSetFlipRate(0)` for both flips (`:408-416`, `:7842`). Two flips one vblank each make 33.3 ms, so the simulation keeps 30 steps a second.
  - A pass that presents once, and every other flip, uses rate 1.
- **Interpolated state** (all restored, see section 2):
  - **The camera.** `SetRenderCamera` (`0x963d10`) is called with the midpoint (`:7676-7694`), and bytes `+0x20..+0x1a0` of the render camera block are restored afterwards (`:7768`).
  - **Objects.** Every PPos render-buffer world matrix (`PPos+0xe0`, buffer B) is set to the midpoint of the last two shown states (`objects_interpolate`, `:1615`).
  - **Additions in mode 23:**
    - poses extrapolated half a step;
    - the player delayed half a step (PLAYERFIX);
    - the frontend camera delayed (CAMFIX, `:5877-5891`);
    - particles extrapolated (F71, `:2558`);
    - UI nodes, cursors, prompts and the Popit blended (UI60, UIMENU and others).
  - **The real frame** shows the new state. In mode 23 the local player and frontend cameras are shown half a step late.

### Which mode ships (read)

- `run.py` writes `frame_rate_limit=60` into a fresh `lbp3.ini` (`seth:run.py:91-95`). It sets `LBP3_60FPS` to `lbp3_60fps_mode` whenever the limit isn't 30 (`seth:run.py:242-255`).
- The default mode is **`'23'`** (`seth:scripts/graphics_options.py:20`, normalised at `seth:scripts/graphics_options.py:79`).
- The packaged route is the same:
  - the launcher writes `frame_rate_limit`, default 60 (`seth:launcher/native/lbp3_launcher.cpp:199-201`, `:450-479`);
  - it starts `lbp3-runner.exe`, which is `run.py` frozen (`seth:launcher/native/lbp3_launcher.cpp:4-5`, `:92-93`);
  - the F1 dev row labels mode 23 "X - W + pose interpolation (default)" (`seth:gpu/shim/lbp3_overlay.cpp:1166`).
- **So a new install runs mode 23 at 60 fps by default.** The header's "F1 option makes run.py set mode 3" (`seth:src/runtime_lbp3_60fps.c:70-71`) is stale.
- **How the number maps** (`:10881`, `:10904-10982`):
  - Above 23 means 3.
  - 5 and up sets `test_mode` = the number, and runs as mode 3 for 19–23 and as mode 1 for 5–18.
  - The feature flags are keyed on `test_mode`:
    - hold clocks (T): 19–23;
    - prediction compensation (U): 20–23;
    - UI hold (V): 21–23;
    - skew fix (W): 22–23;
    - pose extrapolation, LAYERFIX, PLAYERFIX, BONETHINGS, SMOOTHFIX and POSEDELAY: 23 (`:11036-11075`, `:11819`).
  - No predicted step in the extra frame for any test mode of 5 and up (`:11967`).
  - No culling in the extra frame from 19 up (`:10928`).
  - **Plain mode 3 has none of T–X.**
- **Above 60** (frame-rate limit > 60, "Match display" or "Unlimited"):
  - The variable rate (F94/F119) runs 2–34 presents per pass on timed flips (`:7382-7535`).
  - It is allowed only in modes 19–23 (`fr_vr_ok = mode >= 19`, `:10888`).
- **Also default-on in mode 23 despite comments that say otherwise:**
  - **LOAD60**: loading screens flip at about 55 a second. It is set at `:11673` for every mode from 2 up, while the comment at `:7989` says "UNSURE, default off".
  - LOADUI, LOADGAME and RING60 build on it.
  - FEANIM is on (`:11226`), while `:6175` says off.
- **Dead or diagnostic modes:**
  - 4 was removed and runs 5 (`:10900`).
  - 5–18 (E and G–S) are bisecting modes. 6 and 8–18 never even show the extra frame (`extra_unshown`, `:10907`). They are listed only under the dev rows of F1 (`seth:gpu/shim/lbp3_overlay.cpp:1146-1176`).
  - 1–3 (A, B, C) are superseded steps.
  - Other diagnostic options: `LBP3_60FPS_OBJECTS=pal` (the dormant PAL `PBodyUpdate` path), `ADAPT=1/2`, about 295 `getenv` knobs and many `*DBG` / `*LOG` / `*DUMP` paths.

## 2. Online and lockstep safety

### What the code does

- **No online gate.** The 60 fps file never checks for an online session (read).
  - Its only "online" branch picks the online thing-render path `0x2570f0` when `0x254270()` is true, which it also holds (`:5804`, `:2465-2469`).
  - Seth's comment says that path was seen with "a 100% save in the Pod", so `0x254270` is not simply "in an online session" (seth's claim).
- **Seth's tree shows no co-op transport:**
  - P2P sockets stay on the loopback, "no shadNet/P2P transport yet" (`seth:src/runtime_netloop.c:17`, seth's claim, a comment);
  - NpSignaling is "a context, no peers" (`seth:src/runtime_npwebapi.c:205`, seth's claim, a comment);
  - in the offline provider table the Matching2 calls answer "signed out" (`seth:src/runtime_services.c:969-977`, read).
  - The README says co-op is "built in" (seth's claim).
  - Nothing in this tree shows the mode was ever run in lockstep (inferred). Whether seth's online works is the online lane's call.
- **Prediction and input preview in the shipping mode (read):**
  - `GetNumPredictedFrames` returns 0 in the extra frame (`predict_mode = 1` for any test mode of 5 and up, `:11967`; hook `:1996-2001`). The original is still called first, then its result is replaced; it is a getter by name (inferred).
  - The input preview `0x254280` is not run in the extra frame (`skip_preview = 1`, `:672`, `:1982`).
  - In the real frame both run exactly as the game runs them.
  - The comment at `:11044` ("PLAYERFIX: the extra frame runs the predicted step") is stale.
  - In plain mode 3 (`test_mode` 0) the extra frame **does** run the prediction (`predict_mode = 2`). That is on the display copy (inferred from our `fps60_timing_re.md`), but it runs twice per pass.
- **What the extra pass changes and puts back (read):**
  - Restored at the end of the extra pass:
    - render camera bytes `+0x20..+0x1a0` (`:7768`);
    - PPos render-buffer matrices;
    - creature poses (`pose_restore`);
    - layer depth and the renderer, UI and mesh clocks (`held_before` / `held_after`);
    - Popit flipbook counters (code bytes patched only for the extra pass);
    - HUD springs and particle records;
    - the culling list count (B5).
  - Restored at `0xbc16` (end render stage), before the next pass's steps: the real-frame edits from PLAYERFIX, CAMFIX, frontend objects and poses, UI nodes, REVEAL and others (`lbp3_end_stage_hook`, `:7310-7375`).
  - Skipped in the extra frame by stubs:
    - creature animation updates (`0x19dbaf`, `0x40279`, `0x4033b`, `0x19d573`; `:2470`);
    - the Bink TV per-draw calls;
    - the screen-fade step inside the flip (`0x5cbc8`; `:162-176`).
- **What the extra pass changes for good in mode 23 (read):**
  - **Flip state.** `hide_flip` is 0 outside test modes 6–18 (`:10905`). So the render counter `0x1202b54` (235 readers per the header), its copy, the flip stamps `0x1202b48`, `0x1296390/98`, the frame times `0x1202b20..37` and the frame statistics `0x120ac70` all advance twice per pass.
    - 45 animation readers are redirected to a 30 Hz mirror (`:187-193`).
    - Another 39 are redirected above 60 (`seth:src/runtime_lbp3_f96ctr.inc`). They are also patched at 60 when the session is "live", which means a display above 61 Hz: the user's 165 and 240 Hz case (`:10713`, `:10889`).
    - `0x1202b28` is overwritten with the pass time at each real flip (`:7913`). Its only reader is the lateness test `0x254077` (seth's claim). By address range that test sits inside `RenderPrep` (inferred).
    - **Two of the flip-state readers seth names, `0x25485f` (flip interval `0x1202b34`) and `0x254872` (frame statistics), fall inside the address range of `FUN_00254450`.** That is our confirmed step function. It starts at `0x254450` and still has its counter increments at `0x2553d7`/`0x255461` (`notes:perf/fps60_timing_re.md`).
      - This is inferred from ranges, not decompiled; the pointer came from review.
      - If it holds, the confirmed step reads 60 Hz flip state in the shipping mode.
      - What it does with it wasn't checked. The same function sends telemetry every 512 frames, which is a plausible use (inferred).
  - **The RenderPrep stamp `[0x1061a04]`**, incremented at `0x251908`, advances once per extra RenderPrep. It is not held back: "holding it back crashed: other users need it unique" (`:5550-5556`, seth's claim). Only the predicted body updates get a corrected value (SMOOTHFIX).
    - Note for our notes: `fps60_timing_re.md` reads `DAT_01061a04` as a body-copy selector "(= 1)". Seth reads it as a per-RenderPrep stamp, backed by an experiment (easing restarted when it advanced twice). Worth a recheck.
  - **The dim overlay** (`0x266300`, call #10) steps on every call, so its fade runs twice as fast at 60 (`:5807-5810`).
  - **The motion-blur camera.** At a cut, CUTBLUR copies the current view-projection into the previous one (`+0x160`). It writes the same value into the saved copy, so the restore keeps it (`:7634-7642`; default on at `:2092`). It is the same write the game's own `ClearMotionBlur` makes.
- **The write probe** (`seth:src/runtime_lbp3_probe.c:1-13`) works in four phases:
  1. It hashes every committed page before and after one extra frame.
  2. It repeats the hashes around a sleep of the same length, to separate background writes.
  3. It diffs the changed pages into clusters.
  4. It puts hardware watchpoints on the cluster starts to name each writer.
  - **The report is in the missing R12.**
  - The code comments record what it found per call (`:2445-2469`, `:5737`): creature animation, layer depth, the renderer time `0x1192acc`, the UI time `0x133ae50`, the texture phases `0x119a200`, mesh times `item+0xcc`, the culling list, and the Bink and Popit counters. Each got a hold, a skip or a save and restore.
  - It was run offline, in a few scenes (level 1 idle, Popit open, the Pod).

### The game's own sync check

- **Joined from both sides (inferred).**
  - Our `LBP3_SYNC_PROBE` documents the check: every 60 world frames `0x1acaa0` (called from `0x256db6`) hashes each player's matrix through `... -> +0x10 -> matrix at +0x40` (`lbp3-recompiled:runtime/src/core/sync_probe.cpp:1-20`).
  - Seth documents PPos `+0x10` as the **current** buffer pointer. It points to sync buffer A (`+0x20`, world matrix at `+0x60`) in the synced stage and to render buffer B in the render stage (`seth:src/runtime_lbp3_60fps.c:180-183`).
  - So the hash reads buffer A's world matrix, provided the hash call runs while the world is in the synced stage. Both the extra pass (call #20) and the real pass (`0xbc16`) leave the render stage before the next pass's steps.
  - **Not checked:** which function and stage contain the hash call `0x256db6`. It is most likely the step, but that wasn't decompiled.
  - Seth writes only buffer B. **No path from seth's matrix edits into the game's own sync hash was found**, under the condition above.
- **A window that the restore rule doesn't cover (inferred).**
  - Between the real `RenderPrep` and `0xbc16`, the game's per-pass logic runs: the frontend update, the deferred-list drain, things-every-pass `0x19d170`.
  - It runs in the render stage and sees PLAYERFIX's delayed player and, after `0xb8ca`, CAMFIX's camera.
  - In stock play those calls already see the per-machine predicted state, so they shouldn't feed the confirmed sim (inferred).
  - Create mode is the case to watch. Seth special-cases the brush PPos (`lbp3_mouse_brush_ppos`, `:1089`).

### Verdict on "The game diverged"

- **Unproven.**
  - The design aims to be simulation-neutral, and no write path into the sync hash shows up.
  - But the flip-state reads inside the confirmed step, the advancing stamp and the unrestored per-pass state are things a lockstep peer at 30 never sees.
  - The adaptive fallback makes the number of extra frames differ from machine to machine.
  - LBPPC's ultrawide divergence (`notes:perf/resolutions.md`) shows that render-adjacent camera state can reach the simulation.
- **If it is ever ported:**
  - force it off online (beside `LBP3_RES_ASPECT_SPLIT` in `lbp3-recompiled:runtime/src/core/res_patches.cpp:375-380`);
  - then test two instances, one at 60 and one at 30, with `LBP3_SYNC_PROBE=1` for 10+ minutes across several levels and Create;
  - check the `[sync] sent` hashes as well as the end-of-session message.
- Our memory rule (same version, no mixed-version tests) doesn't cover this: both peers would run the same build with different settings.

## 3. Quality and cost against our warp frame generation

### Quality

- **Real renders (read, plus seth's claims).** The in-between frame is the game's own render, so it has:
  - correct disocclusion;
  - particles extrapolated (F71);
  - UI drawn by the game (with holds and blends);
  - no 8-bit vector range limit and no smearing of the popit text.
- **Our warp's weak spots** are the popit, which has no vectors, the ±120 px clamp and disocclusion (`notes:perf/fps60.md`, `notes:perf/fg_warp.md`). Seth's design avoids those by construction.
- **The price is a long tail.** Every system that advances once per draw (a "per-draw clock") or moves outside PPos needed its own fix: 76 F-numbered fixes and 9 B-series playtest bugs (B1-ANIM … B6-COLLECT) (read: tags in the file). Known open items:
  - **Cloth and soft bodies** (Newton's coat tails, Sackboy's tie, flags) step once per **present** in `spu.prx` jobs, so twice per tick at 60. Measured while paused at 60 (Newton's hem, the tie): 0.23–0.58 against 0.13 at 30, with a noise floor of 0.05–0.10 (seth's claim). Four experiments didn't fix it, so it ships off (`:3429-3439`).
  - UISPRING is unfinished and off (`:11607`).
  - The Home-screen dim flash is still open (`:10742`).
- **Against our `fps60.md` "missing" list.** The verdict "months, very high risk" was for building this from scratch. Seth has done most of it.

  | System (`fps60.md` / `fps60_render_re.md`) | Seth (read) |
  |---|---|
  | Camera | covered (midpoint, cut rules, CAMFIX for frontend cameras) |
  | Rigid bodies / every PPos | covered (render-buffer matrices at the midpoint) |
  | Sackboy and creatures (skeletal) | covered in 23 (pose extrapolation X, PLAYERFIX, POSEDELAY, BONETHINGS) |
  | Soft bodies (cloth, tie) | **not fixed**: twice per tick, experiments off |
  | CPU particles | covered (F71, extrapolated half a step) |
  | Fluid (FLIP sim), snow compute | not found (no hits for fluid or water in the 60 fps file or `runtime_lbp3_mouse.c`) |
  | Ropes | not found as a system (only the Create cursor tether, B1-CREATE) |
  | Decals | not found (0 hits) |
  | Shadows, reflections | not handled separately: they redraw from the same matrices (inferred) |
  | Lights | covered (LIGHTTICK, FLICKERTICK, PLIGHTTICK, B4-BLINK) |
  | Per-draw fades and clocks | covered (T holds, REVEAL, the 45 + 39 counter readers, the flip's fade guard) |
  | In-world video (Bink TVs) | covered (decode guards, TV buffer swap) |
  | Popit and screen UI | covered (UI60, UIMENU, UICURSOR, UIDIALOG, UIPROMPT, UISTRIP, MOONUI) |

### Latency

All of this is inferred arithmetic. It assumes both frames fit their vblanks and that the pass period stays 33.3 ms.

- **The world and the camera:**
  - the extra frame, at the midpoint, goes out one vblank before the real frame;
  - the real frame goes out on the vblank where the game at 30 would show it.
  - So there is no added delay against 30, and half the motion shows 16.7 ms earlier.
- **The local player (PLAYERFIX, mode 23)** (`:11953`: "real frame halfway between their last two predicted states, extra frame at the last one"):
  - the real frame shows the player half a step back;
  - the newest predicted state appears one vblank (16.7 ms) later than at 30.
- **Frontend cameras (CAMFIX)** (Pod, menus, level entry and exit): half a step (16.7 ms) late (seth's claim, `:5877-5891`).
- **Ours (measured):**
  - real frames reach the screen +19.3–19.9 ms later with the GPU warp (`notes:perf/fps60.md`, prototype section);
  - the earlier blend prototype was +21.5 ms, with generated frames 14.0–14.4 ms after the slot (`notes:perf/fg_present.md`).
- **Net (inferred):** seth's is lower for the world and camera, and a little lower for the local player (+16.7 against +19.3–19.9 ms).
- **If the extra frame misses its vblank,** the pass becomes 50 ms and the next extra frame is skipped (ADAPT=3).

### Cost

- **Per pass, seth adds about a second render (read + inferred):**
  - `RenderPrep` without prediction;
  - thing render lists, scene, players, frontend UI and generated meshes;
  - a submit and the GPU frame;
  - the flip.
  - Seth's own `run.py` notes that level 1 reached 97% of passes with an extra frame only after GPU readbacks were turned off, against 51% before (`seth:run.py:256-263`, seth's claim).
- **On our path (inferred from our measurements):**
  - The heavy Needlepoint walk is 19.2 ms of work per pass with one render: main thread 5.2 ms, patch + copy 6.6–6.7 ms, GPU submit + run about 4.4 ms, readback 0.7 ms (`notes:perf/fg_present.md`, `notes:perf/fps60_render_re.md`).
  - Patch + copy, GPU, readback and present upload are all per submit. A second render adds 6.7 + 4.4 + 0.7 ms plus the main thread's render share. That is 19.2 + 11.8 = **31 ms or more per pass**, against 33.3.
  - Our submit also waits for the previous frame (`gpu::WaitIdle`, `lbp3-recompiled:runtime/src/hle/videoout.cpp:684`). The flip handling, including the slot wait, sits on the command-processor thread (`lbp3-recompiled:runtime/src/hle/videoout.cpp:275-325`). So the real frame's GPU work starts after the extra frame's slot (inferred).
  - **Expect frequent fallback to 30 in heavy levels** until the submit path is faster.
- **The adaptive fallback ADAPT=3 (read, `:7914-7960`):**
  - a pass over 40 ms makes the next pass present once (catch-up);
  - 6 late passes in the last 30 give 30 single-present passes (1 s), doubling up to 4 s when a retry fails within 30 passes;
  - passes over 200 ms, or with steps ≠ 1, count as hitches and aren't counted.
  - It works like our generator's drop rules. There is nothing new to take for frame generation.

## 4. Portability onto our runtime

- **The mechanism is the same as ours (read).**
  - **Seth:**
    - `alloc_near` (`:8106`) and rel32 retargeting checked against the expected target (`rel32_target` / `retarget_call`, `:8124-8137`);
    - `movabs rax; jmp rax` stubs (`emit_jump`, `:10158`);
    - `lea [rip+...]` displacement redirects for the counter readers;
    - one copied-bytes trampoline (UISPRING).
  - **Ours already does all of these:**
    - `Redirect` (`lbp3-recompiled:runtime/src/core/sync_probe.cpp:180`);
    - `AllocNear` (three copies: `res_patches.cpp:305`, `exact_math.cpp:44`, `sync_probe.cpp:156`);
    - byte-checked `kPatches` (`lbp3-recompiled:runtime/src/core/patches.cpp`).
  - Seth's hooks are C functions with the guest ABI attribute. Ours call guest code the same way (`PS4_SYSV_ABI` function pointers, e.g. `lbp3-recompiled:runtime/src/hle/videoout.cpp:643-650`).
  - The C code is GPL-2.0-or-later, so it can go into our GPL-3.0-or-later runtime.
- **Pure address and RE knowledge, reusable verbatim (identical eboot):**
  - main loop sites `0xb89c`, `0xbb09` and `0xbc16`;
  - `RenderPrep` `0x2514d0(game, alpha)`, the flip `0x5caf0`, the 13 other flip callers, the flip-rate site `0x5d2d3`, `SetRenderCamera` `0x963d10`;
  - the render camera block (`+0x20` view×proj, `+0x60` view, `+0xa0` camera-to-world, `+0xd0` position, `+0xe0` projection, `+0x160` previous view×proj);
  - the PPos buffers A and B with `+0x10` current, the PWorld PPos array `+0x100/+0x108`, the `world_of` chain, `SimFrame` at `PWorld+0x414`;
  - the 45 + 39 counter readers and the flip-state list;
  - the per-draw clocks of calls #8 and #12, the Bink guards, the loader tick `0x236060` with its 34.3 ms threshold, the RenderPrep stamp `0x1061a04`.
- **Needs rework in our present path (inferred, from reading ours):**
  - **The flip rate.** Seth toggles the global rate from the main thread before each flip. Our `g_flipRate` is read in the flip interrupt on the command-processor thread (`WaitFlipSlotInner`, `lbp3-recompiled:runtime/src/hle/videoout.cpp:95-133`), which can lag the main thread by a frame.
    - Carry the slot period in each `PendingFlip`, as `simSteps` is carried now.
    - Don't go through `sceVideoOutSetFlipRate`, which prints a line on every call (`lbp3-recompiled:runtime/src/hle/videoout.cpp:379-385`).
  - **"Discard next flip".** The shipping mode uses it: CUTHIDE=2 (`:2101`) hides both frames at a camera cut, and FIRSTHIDE (`:2107`) the first pass.
    - Seth's driver completes the flip without presenting it (`seth:gpu/shadps4/core/libraries/videoout/driver.cpp:893-899`).
    - Ours needs a `discard` flag on `PendingFlip`: skip the readback, the slot wait and the present, keep `SignalFlip`. About 20 lines.
  - **The frame clock.** `kernel::NoteFlip` anchors the main thread's and the host thread's clock to the last flip, with an 8 ms dead zone (`lbp3-recompiled:runtime/src/hle/kernel_time.cpp:78-118`). An extra flip mid-pass would move that anchor.
    - Only real flips should note it, or ticket 09's steps ≠ 1 fix may regress.
    - Measure steps ≠ 1 with the mode on.
  - **Two readbacks and uploads per pass.** This already happens with frame generation on (60 uploads a second).
  - **Frame generation must be off** while the mode is on (see section 5).
  - **Timed flips above 60** (F94/F119) would need presenter work on top. Not needed for 60.

## 5. Coexistence with our frame generation

- **Seth makes them exclusive (read):**
  - `lbp3gpu_60fps_wanted()` is false when frame generation is on (`seth:gpu/shim/lbp3gpu.cpp:290-293`);
  - its FSR 3 frame generation pauses when the game runs above 40 fps (`seth:gpu/shim/lbp3_overlay.cpp:1076`);
  - the frame-generation row is shown only at limit 30 (`seth:gpu/shim/lbp3_overlay.cpp:1062-1066`).
- **Ours should do the same (inferred).** Stacking our warp on 60 rendered frames to reach 120 on the 240 Hz monitor would need:
  - the period taken from the present interval, not the 30 Hz flip;
  - a new steps rule, because every extra flip has 0 simulation steps and our rule drops 0-step frames (`notes:perf/fg_present.md`);
  - rescaled vectors: the camera vectors span half a step (seth points `+0x160` at the extra frame's camera, `:7771`), while per-vertex object history still spans a full step (inferred).
- **Not worth it.** Seth's own way above 60 is more real renders (variable rate), not frame generation.

## 6. Other reusable findings for our open perf items

- **The 45 readers, the flip-state list, adaptive pacing and the catch-up handling do nothing for `early_flip.md`, `slow_frames.md` or issue 09 unless the mode itself is ported.** They exist to hide a second flip that our port doesn't make. The items below are the exceptions.

- **The frame-generation cut rule. Worth taking (S, half a day; no lockstep risk).**
  - Ours reads floats 12–14 at view `+0x20` (`lbp3-recompiled:runtime/src/hle/videoout.cpp:618-641`). Per seth's layout that is the translation row of view×proj, not the camera position, which is at `+0xd0` (`seth:src/runtime_lbp3_60fps.c:154-157`).
  - Our thresholds (1000 and 4× the previous jump, `notes:perf/fg_gpu.md`) are tuned on that mixed quantity, and they can't see turns.
  - Seth's CUTREL (`:2083`, `:7631-7633`): a move over 150 u **and** over 6× the recent step, or a turn over 6° **and** over 6× the recent turn, or above 1000 u / 30°.
  - It catches near-shot cutscene cuts, e.g. Heroes about 5 s in: 749 u and 23° after steps of about 1 u (seth's claim, `:7617-7621`). Seth also notes that `ClearMotionBlur` alone is no cut, because scripted cameras call it every step while gliding (`:7607-7610`).
  - Fits frame-generation follow-up 1 in `notes:perf/fg_present.md`.
- **The main-loop flip and the other flip sites (S, low value).**
  - The main-loop flip is `0xbb09`. The 13 others are `seth:...:160`: loader tick `0x2360ef`, in-game loading screen `0x739c`, the rest unlabelled.
  - Tagging flips by call site would tell our generator "game frame" from "loader, movie or dialog" without image heuristics.
  - It's mostly covered already: loading screens have 0 steps and are dropped.
- **RE facts for the notes (S):**
  - `0x1202b28` is the measured frame time in ms, and the lateness test `0x254077` inside `RenderPrep` is its only reader. The flip interval is `0x1202b34`.
  - The loader tick `0x236060` flips only after 34.3 ms since the last flip (`0xaaec0`). That is why loading screens run at about 29 a second.
  - The stamp discrepancy at `0x1061a04` (section 2).
- **`perf/slow_frames.md`:** seth's "no GPU readbacks" finding (`seth:run.py:256-263`) is **already in hand**. Ours defaults readbacks off (`notes:perf/audit.md` #16, `notes:perf/reuploads.md`).
- **`perf/early_flip.md`:** not applicable. That is our command processor's stale `PatchedFlip` marker; seth's shadPS4 flips through EOP.
- **`issues/09-sim-step-bursts.md` (resolved):** nothing new. If the mode were ported, the extra flip must not move our flip-anchored clock (section 4).
- **Seth's camera motion and motion history** (`seth:gpu/shadps4/video_core/host_shaders/camera_motion.comp`, `seth:tests/test_camera_motion.cpp`, `seth:tests/test_motion_history.cpp`) are **not part of this mode**.
  - They feed his temporal upscalers and FSR 3 frame generation (`seth:docs/upscaler.md`). They compute per-pixel camera vectors from depth and the current and previous camera (the sky as a pure rotation), merged with per-draw vertex history.
  - For our warp they could fill what the game's 8-bit vectors lack: the sky (R=0) and motion beyond ±120 px. That is M work and belongs to the frame-generation or upscaler lane.

## 7. Effort and risk

| Step | What | Effort (inferred) | Main risk |
|---|---|---|---|
| A | Plumbing: hooks `0xb89c` / `0xbb09` / the 13 flips, extra pass with duplicate frames, per-flip slot period and discard, frame clock, 45-site mirror, `0x1202b28`, fade and Bink guards | S–M, 3–5 days | steps ≠ 1 regressions; perf |
| B | Mode 3 equivalent: camera midpoint with cut rules and CAMFIX, PPos buffer-B interpolation and records | M, 1–2 weeks | judder seth's T–X fixes address (Sackboy stands still for a present then jumps; limbs at 30) |
| C | Mode 23 parity: T–X, PLAYERFIX and POSEDELAY, REVEAL, particles, the UI families, LOAD60 and LOADUI, B-series | L, 3–6 weeks, mostly per-scene checks by eye | the long tail of per-draw state; stale comments in the source; about 295 knobs |
| D | Fit two renders per pass on our synchronous-submit, readback and D3D11 path | unknown (perf work) | fallback to 30 in heavy levels |
| E | Lockstep validation (two instances, sync probe) | S–M | "The game diverged"; mixed 30/60 peers |

- **Total for parity: L, about 5–9 weeks plus D (inferred).**
- **The riskiest parts:**
  1. lockstep (never run by seth);
  2. our present path's cost;
  3. per-draw state not yet found in levels seth didn't play;
  4. maintenance, since it's a 12.4k-line port of someone else's moving target.

## Verdicts per item

| Item | Seth | Ours | Verdict | Effort | Risk (lockstep incl.) | Touches |
|---|---|---|---|---|---|---|
| Engine 60 fps, mode 23 as a whole | real extra render per step | warp frame generation (60 on screen) | **not worth it now** | L | high; off online if ever | `perf/fps60.md` verdict |
| Plumbing only (mode A) | flip-rate toggling, discard flip, paced other flips | 60 Hz slots exist; no per-flip period or discard | portable with rework (only as groundwork) | S–M | low offline; frame clock | issue 09 |
| Camera and PPos interpolation (mode C) | midpoint camera and render matrices | rejected camera-only (`fps60.md`) | portable with rework, but judders without T–X | M | medium | `perf/fps60.md` |
| RE addresses and layouts (camera block, PPos A/B, flip state, counter readers, mid-call list) | documented in code | partly (camera `+0x20` / `+0x160`, SimFrame) | **portable as-is (knowledge)** | S (notes) | none | `fps60_render_re.md`, `fps60_timing_re.md` |
| Frame-generation cut rule (CUTREL, position `+0xd0`) | relative move and turn rule | absolute jump on the view×proj row | **portable as-is** | S | none | `fg_present.md` follow-up 1 |
| Flip-caller tagging | 13 sites known | image and steps heuristics | portable as-is, low value | S | none | `fg_present.md` |
| ADAPT=3 fallback | catch-up and windowed fallback | frame-generation drop rules | already have (equivalent) | – | – | – |
| GPU readbacks off | `LBP3_READBACKS=0` | off by default | already have | – | – | `perf/slow_frames.md` |
| Early flip at the Pod's first frame | EOP flips through shadPS4, no `PatchedFlip` marker | stale-marker theory, fix pending | not applicable | – | – | `perf/early_flip.md` |
| Counter readers and flip-state list for our open items | hide the extra flip | no extra flip | not applicable unless the mode is ported | – | – | `perf/slow_frames.md`, issue 09 |
| `0x1061a04` stamp reading | per-RenderPrep stamp | "copy selector = 1" | fix our note (recheck) | S | none | `fps60_timing_re.md` |
| Camera-motion fill for sky and fast motion | upscaler path, not this mode | 8-bit game vectors | not this lane; maybe M later | M | none | `fg_warp.md` |
| Two-instance sync-probe test recipe | n/a | `LBP3_SYNC_PROBE` exists | use it if anything here goes online | S | – | online co-op |

## Not checked

- The missing R12 and the 60FPS_FIXES documents: the probe report, measured shares and costs.
- Whether `0x25485f` and `0x254872` really belong to the confirmed step (range-inferred) and what they do with the flip state. Also who else reads the stamp `0x1061a04`. Needs Ghidra.
- Which function and stage contain the sync-hash call `0x256db6`. The "no path into the sync hash" conclusion rests on it running in the synced stage.
- Seth's real per-pass cost (no figures in code beyond the 51% → 97%).
- Whether `GetNumPredictedFrames` has side effects.
- Whether ropes, fluid and decals look wrong in seth's extra frames. Nothing names them.
