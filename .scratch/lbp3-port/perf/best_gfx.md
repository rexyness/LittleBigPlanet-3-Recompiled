# Best-of-both graphics lane (2026-11-13 night)

This lane covers the graphics part of the "best of both worlds" branch ([best/README.md](../best/README.md)). The ideas come from LBPPC ([research/lbppc_graphics.md](../research/lbppc_graphics.md)). Branch `feat/best-gfx`, worktree `D:\Recomps\lbp3-wt\bgfx`, from `port` `34e3e8d`; runtime only, using the clone's shadPS4 `553be3ba`. Brightness (ticket 17) is out of scope. Tests ran on shader-cache copies (`LBP3_SHADER_CACHE_DIR`) and test saves, with the game lock held.

## 1. Bloom chain at 1440p (`c3242e5`)

- **What:** six more checked same-length patches in the `LBP3_RES=1440` set, in a new group, `bloom`. The render-target table's bloom chain becomes 1280x720 → 1707x960, 640x360 → 853x480 and 320x180 → 427x240, each size × 2560/1920 or 1440/1080, rounded.
  - The six instructions are at `0x427d1c`/`21`, `0x427d3d`/`42` and `0x427d61`/`66` (`MOV ESI`/`MOV EDX` immediates). Their original bytes were checked against `analysis\eboot.elf`.
  - The sites are the ones LBPPC scales (`src/probe.c` `targets[]`, its "grow, never shrink" rule); only the addresses are taken from there.
  - `LBP3_RES_SKIP=bloom` leaves the chain at its 1080p sizes.
- **Shader literals:** a scan of all 135,428 shaders for bloom-size literals (1280, 720, 640, 360, 320, 180 and their reciprocals; `tmp\bgfx\bloomscan.py`) found 16. The relevant ones are five engine programs in `intro_001.farc`:
  - `720ccb3d006e5139`, `aabfa9ad7cf74dc1`, `eb916cbab6764e5b`: 1/1280 and 1/720;
  - `3d4c9f8cd07a337c`: 1/640 and 1/360;
  - `5ea5e7749196e777`: 1/640;
  - plus 1/320 in `3a0615bd0d8d3a5e` (the composite, already on the 1440p list for its 1080p literals) and in `3d4f881fe226fe5d`.
  - The rest are `GMTb` materials with 180 and 1/180, probably angles.
  - If these are texel steps in UV space, scaling the targets keeps the same on-screen blur. If they turn pixel positions into UVs, the glow would land offset or stretched. The in-game comparison decides.
- **In game:** see Results.

## 2. LBPPC's buffer-write bugs on our port

See Results.

## 3. Low-latency limiter (`LBP3_LOW_LATENCY`, `0073a60` then `91a8089`)

- **Finding:** LBP3's frame thread submits a frame and starts the next one straight away. The next submit's pre-submit wait (`gpu::WaitIdle`) then blocks until the previous frame has flipped, which with our pacing is its slot. So the CPU runs a frame ahead, and input read at the start of a frame reaches the screen about two slots later.
  - The first version hooked the flip event on the main thread, but the frame thread is the submitting thread and blocks in the pre-submit wait, so that hook was the wrong place.
- **The limiter (opt-in):** right after each submit, the frame thread is held back by most of that wait:
  - the delay is the 10th percentile, over 60 frames, of (the pre-submit wait plus the delay that frame had), minus a 4 ms margin, at most 25 ms;
  - it moves by at most 0.5 ms a frame;
  - a submit that barely waited halves it.
  - `LBP3_LOW_LATENCY=1` turns it on; `=0` only measures (the `[lowlat]` line, including "frame start to slot", the part of input latency this changes).
  - It's the idea of LBPPC's limiter, reimplemented; none of its code is used.
- **Risk:** frame starts move later than the frame clock's 8 ms dead zone (`kernel_time.cpp`), so the game's clock reads at frame start become slot + delay − 8 ms instead of the slot. That is a constant offset if the delay is steady, but it touches ticket 09's area. The gates are steps ≠ 1 under 1% in the Introduction ×2, the Thruster recipe 3/3, and frame-generation cadence and latency (`framegen.csv`).

## Results

(filled in below as the runs finish)

### Bloom (in game, 2026-11-13 ~05:45)

Bunkum route plus the Pod, on a shader-cache copy, the bgfx build. Three arms: 1080p; 1440p with `LBP3_RES_SKIP=bloom` (B); 1440p with the bloom group (C). Native frame dumps (`LBP3_FG_DUMP=3`, `fgdump`) were taken in the Pod and at 35/50/70 s into the cutscene; run data is in `analysis\fg_dump\bl*`.

- **Applied cleanly:** all 6 bloom sites patched, with no fatal stop at boot, so the MainRenderTargets block (68 MB at 1440p) still fits. 0 GPU warnings, and nothing compiled.
- **The glow lines up:**
  - **Pod lamp** (a static scene): B against C differs by a mean 1.15 levels in the native crop and 2.90 in the window shot. That's animation-level noise, and the glow sits on the cushion in both.
  - **WELCOME banner and bulbs** at 35/50/70 s: the same luma statistics (p50 116–118, p90 239–247, clipped share 6.2% / 12.9% / 1.1% in both arms). No offset or stretched glow, so the bloom passes' 1/1280, 1/720 and 1/640 literals behave as UV-space steps.
  - Stills: `analysis\screenshots\bloom_crops.png` and `bloom_sheet_bk35.png`.
- **Visible difference:** none at this viewing scale. The chain is just finer, as expected.
- **The 1080p arm** reached the Pod late (its window was still black at 28 s), so the scripted route never entered Bunkum. Earlier 1440p work (`res1440_experiment.md`) already compared 1440p against 1080p.
- **Verdict:** merge-ready, on by default within `LBP3_RES=1440` (`LBP3_RES_SKIP=bloom` restores the old sizes). The limiter and repro runs below also ran with it on, at `play.bat`'s settings.
- **Withdrawn 2026-11-14 (perf/capture_pitch.md, runtime `9d3404c`):** the bloom chain is also the capture chain, which the CPU reads back with a stride of the table's width. 1707/853/427 are not multiples of 16, the GPU pads rows to 64 bytes, and every photo, costume icon and profile picture taken at 1440p from 11/13 on was sheared into streaks (and cropped to the middle 3/4). This lane never took a photo. The chain is back at its 1080p sizes at every 16:9 size; the glow measured the same before and after.

### LBPPC's buffer-write bugs on our port (2026-11-13 ~06:00)

- **Run:** the Needlepoint route (`m5_np_route.sh`: the walk collects score bubbles and a ×2 multiplier, then stands 30 s at the picnic basket) on `port` `34e3e8d`'s own exe. Once at `play.bat`'s settings (1440p, frame generation), once at 1080p. A window shot every ~1.5 s from the level start (`_tools/gfx_repro.sh`), scored by `_tools/gfx_repro_score.py` and by eye on contact sheets.
- **Orange flares** (glow piling up after a lost clear, LBPPC's score-bubble case): **doesn't reproduce.**
  - The score went from 560 to 1190 (1440p) and to 1140 (1080p) during the shots, with no orange shape building up.
  - The orange share stays at the level's own colours: median 3.4% at 1440p (the yellow basket, the bunting), 0.3% at 1080p, with no growth over time.
  - Stills: `analysis\screenshots\rep_port1440\`, `rep_port1080\`.
- **The 3D scene going black and streaky on pickups:** **doesn't reproduce** on this route. The near-black share is at most 1.0% and the frame-to-frame change is in line with the camera's moves.
  - The route has no sticker-bubble pickup (LBPPC's case is "Killer Whale Survival" or a sticker bubble in Needlepoint), so that exact trigger isn't covered.
- **The "Gates of Stitchem Manor" movie** (LBPPC's no-input repro) wasn't run; reaching it needs the Prologue map navigated past Needlepoint.
- **Why ours is different:** LBPPC hit both because it threw away buffer-to-image coherence (`LBPPC_NO_BUFFER_IMAGE_INVALIDATE`). Ours keeps the old path (a copy back to the buffer before the masked fill, a re-upload at the next bind) plus `fix/rt-fills`. So their `BufferWriteImageSync` (2.4 ms/frame) isn't needed, and nothing was ported.
- **Caveat:** these runs and the ones below were taken while other lanes were building, so frame times are noisier than usual. The 1080p walk's work was 29.9 ms, against the usual ~19.

### Low-latency limiter (in game, 2026-11-13 06:00–07:30)

All runs at `play.bat`'s settings (1440p, frame generation) on the Needlepoint route, bgfx build, shader-cache copy. The `[lowlat]` lines are in `analysis\m5_ll*_stderr.txt` and `m5_fe*_stderr.txt`.

1. **First version (delay after each submit, `91a8089`):** it never engaged.
   - The pre-submit wait is 0.00 ms: the GPU has always finished by the time the frame thread submits, so LBP3 doesn't run a frame ahead. "Frame start to slot" was 33.3 ms, and the delay stayed at 0.
   - The gates passed only because nothing changed: Introduction 0.26% / 0.13%, Thruster 3/3.
2. **Where the frame thread waits (`bc4dfc5`):** effectively nowhere.
   - Per frame it takes one flip event with 0.00 ms of wait, one event flag (0.00 ms), and semaphores at 0.06–0.14 ms. Condition variables and usleep are 0.
   - It paces itself on the frame clock and starts each frame at the slot.
3. **At the flip event, inside the frame clock's dead zone (`46686c7`):** measured with A/B runs (measure, on, measure, on).

   | | measure | on |
   |---|---|---|
   | flip event taken after its slot | 6.1–6.7 ms | 5.9–7.0 ms |
   | frame start to slot | 26.7–27.2 ms | 26.3–27.2 ms |
   | GPU slot wait | 13.3–14.1 ms | 12.3–13.9 ms |
   | walk work | 23.4 / 21.5 ms | 23.4 / 21.7 ms |

   The controller asks for its maximum, 6 ms, but the frame thread only gets the flip event about 6.5 ms after the slot. That's because SignalFlip runs after the readback and the hand-over to the presenter or frame generation. So the target time has already passed, and the delay taken is about 0. **No latency change.**
   - The gates passed with it on: Introduction 0.00% / 0.26%, Thruster 3/3.
4. **Why there's nothing to gain safely:**
   - Our frame already starts about 27 ms before its slot (one period, minus the 6 ms flip hand-over), not two periods as in LBPPC's case (their 84 → 29 ms came from a two-frame queue).
   - The remaining slack is the GPU's 12–14 ms slot wait. Using it means starting the frame later than the frame clock's 8 ms dead zone, which shifts the game's clock reads off the slot: ticket 09's territory.
   - Not done.
- **Verdict:** NOT merge-ready. It has no effect, so it stays off and unmerged. The three commits on `feat/best-gfx` after `c3242e5` (`0073a60`, `91a8089`, `bc4dfc5`, `46686c7`) are kept as a measurement record (`LBP3_LOW_LATENCY=0` prints where the frame thread waits). The integrator should take `c3242e5` only.

## Summary for the integrator

| Part | Commit | Verdict | Switch / default | Evidence |
|---|---|---|---|---|
| Bloom chain scaled at 1440p | `c3242e5` (on `34e3e8d`, runtime only) | **merge-ready** | `bloom` group of `LBP3_RES=1440`; `LBP3_RES_SKIP=bloom` restores | 6 sites patched, boots, glow aligned, Pod native diff 1.15; offline 10/10 |
| LBPPC buffer-write bug repros | none | nothing to port; neither bug reproduces | — | Needlepoint at 1440p and 1080p: score bubbles collected, no orange build-up, no black streaks |
| Low-latency limiter | `0073a60`…`46686c7` | **not merge-ready** (no effect) | `LBP3_LOW_LATENCY` (unset = off) | A/B above; gates pass trivially |
