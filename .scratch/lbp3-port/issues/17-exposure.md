# Exposure: mid-tones too bright, emissive and bright things go flat white

Type: research, then a fix if a bug is confirmed
Status: parked (2026-11-12): waiting for the human's PS4 footage
Blocked by: the game (the human is playing)
Map: [LBP3 native port](../map.md). Reported by the human on 2026-11-12 while playing the 1440p build: "the game lighting exposure seems to be a bit too much in certain areas". They said it is not related to frame generation or the new resolution, and the 1080p captures agree (below).

## Question

The port looks over-exposed in places. Bright and emissive things clip to flat white or saturated colour with a wide glow, and the image looks contrasty and saturated. Is that the port or the game's own look? If it is the port, which pass causes it, and what fixes it?

## Done when

- The Bunkum WELCOME shot matches the PS4 footage's luma distribution, within capture noise.
- The cause is named with evidence.
- A fix ships behind an environment switch that restores the old behaviour. Screenshots are checked at the examples and at the earlier risky spots (the dark Pod, the Introduction fog, Needlepoint), and the regression passes.

## The examples

All were taken from the 1440p build with frame generation, on the 240 Hz display, while the human said "now". Each shot is stored in `D:\Recomps\lbp3-recompiled\analysis\screenshots\`.

| Shot | Scene | What's wrong | Clipping inside the letterbox [measured] |
|---|---|---|---|
| `exposure_182115.png` | The Crypts intro cutscene (`02_manglewood/05_crypts`) | The cyan neon strips are flat (35–63, 238–255, 255) with no texture; the light-shaft cores are near-white or blue-clipped | 15.6% of pixels have a channel ≥ 250; 6.9% have two |
| `exposure_183014.png` | The Pod's travel cutscene in space (`01_prologue/00_craftworld`) | The beam is a flat white band; the porthole is blown to white/pink; the pale stars are washed out | 15.7% / 13.4%; 12.2% near-white |
| `exposure_183056.png` | Bunkum's opening, "Welcome to Bunkum, chummy chum chum…" (`01_prologue/c01_intro`) | Newton's head is blown to white; the sky is very bright cyan; everything looks contrasty and saturated | see the comparison below |

## The PS4 comparison of the exact Bunkum shot (the strongest evidence)

- **PS4 source:** hyperdude95's PS4 recording of the Bunkum cutscene (`zBMwCD_dhC8`, 149 s). We used its YouTube storyboard, level L2: 160×90 frames every 2 s. Tiles 17–19 of sheet `M0` (34–38 s) are the WELCOME shot with the same subtitle. The sheets are kept in the job's temp folder `ps4ref\sb_M*.jpg`, not committed.
- **Port source:** the 1080p capture `analysis\fg_dump\bk_b\f030_final.raw` (BGRA, made by the 1080p `port` build), and the human's 1440p shot.
- **Method:** the port frame was downscaled and passed through the same kind of JPEG round-trips: 1280×720 at quality 75, 4:2:0, then 160×90 at quality 40. Luma is Rec.709 over the picture band; the sky and banner are colour-keyed. The cameras differ by up to 2 s.
- **Reliability:** medium. The PS4 capture's levels are full range: its black is 1–2 and 18% of pixels are ≥ 245, so a limited-range capture doesn't explain the gap.

| | Sky, median RGB | Banner, median RGB | Luma p10 / 25 / 50 / 75 / 90, band | Pixels ≥ 245 | Lower band (stage, grass) luma p25 / 50 / 75 |
|---|---|---|---|---|---|
| PS4 tiles 17–19 | 161–166, 242–244, 236–238 | 203–214, 121–137, 94–108 | 1–2 / 56 / 123–134 / 215 / 236 | 18% | 37–43 / 86–93 / 129–135 |
| Port 1080p, compressed the same way | 162, 247, 244 | 222, 134, 96 | 7 / 90 / 166 / 227 / 239 | 27% | 67 / 124 / 181 |
| Port 1440p, human's shot (no compression) | 159, 253, 254 | 235, 122, 65 | 0 / – / 165 / – / 240 | 33% | – |

**Reading.**
- The sky and the brightest areas are about the same: both clip. The banner is close after compression.
- The mid-tones are about 1.3× brighter on the port (median 166 against 128), and the darker mid-tones about 1.6–1.8× (p25 90 against 56; stage/grass p25 67 against 37–43). Half again as many pixels clip.
- 1080p and 1440p match, so the cause is not the new resolution or frame generation.
- An earlier check of the default Create level, against ticket 08's PS4 still, found the sky equal and the floor darker on the port. Different camera and shadowing make it inconclusive.

## Findings (offline)

### The game's display chain

Sources: the traced frames `analysis\fg_dump\fg_np_stderr.txt`, `bk_b_trace.txt` and `walk_passes.txt`, and the IR dumps in `analysis\shaders\dumps\`.

- **No HDR.** The scene `171fe8000` is a 1920×1080 R8G8B8A8Unorm, and so are the post targets: the bright pass `17a018000`, the bloom `172890000`/`1727e8000` at 480×270, and the volume `172930800`. No luminance reduction or auto-exposure appears in the traced frames [log].
- **The composite, `5ed16706`** (into the display buffer R8G8B8A8Srgb; blending off) [decompiled]:
  - It starts from c = **1.3 × scene + bloom**.
  - Two colour matrices from cb0, blended by distance to a key colour (cb[36–39]).
  - A vertical gradient tint (cb[40–46]) with a vignette.
  - Then **`lerp(graded, scene, scene.a)`**, then × cb[35] + cb[32–34].
  - So where scene alpha is 1 the raw scene shows (no 1.3×, no bloom, no grade); where it is 0 the graded value shows.
- **The masked fills** (cs `5c5e1be6`: dst = (dst & ~mask) | (value & mask)) [decompiled]. In our trace order they run before the composite:
  - one clears **the scene buffer's alpha** (mask `ff000000`, value 0);
  - one clears `17b018000`, the composite's distortion input;
  - others clear `17b818000` and velocity [log].
  - So on the port the composite always sees scene.a = 0, and **everything is graded**.
- **The bright pass, `33c89263`** [decompiled]:
  - light shafts from a 3D volume: rgb × 4·a × 0.33 × fog;
  - scene glow taps at ±0.015;
  - a temporal feedback of last frame's bloom, × 0.7·(1 − a). That alpha is 0 on the port, so the steady state is about 3.3× a single frame.
- **Display buffer.** The image is eR8G8B8A8Srgb; the composite writes through the CB format, and the presenter reads back the bytes untouched. A global double-sRGB would also lift the near-blacks, and the port's p10 is 7 against the PS4's 2, so that is unlikely [inferred].

## Hypotheses, ranked

1. **The scene alpha is cleared before the composite on the port, not on the PS4 (medium-high).** The measured pattern fits graded ÷ raw = 1.3 + bloom/scene:
   - about 1.0 where both clip (the sky, the brights);
   - about 1.3 in the mid-tones;
   - about 1.6 in the darker mid-tones, where bloom adds relatively more.

   The composite's `lerp(…, scene.a)` would also be pointless if alpha were always 0 at that point. The same holds for the distortion buffer `17b018000`, which the trace shows cleared just before the composite samples it.

   Mechanism candidates:
   - **The fills sit on a compute queue that the PS4 orders after the composite** (a label wait), and our command processor runs them early: through the runtime's early releases or learned wait addresses, or by processing the compute queue ahead.
   - **Or they are on the graphics ring in this order and alpha has another meaning.** Then hypotheses 2–4 apply.

   [measured pattern + decompiled shaders; the ordering mechanism is not shown yet.]
2. **Bloom feedback alpha (medium).** The bloom carries 0.7·(1 − a) of last frame's bloom, and a is 0 on the port. If the PS4 reads 1 there (the T#'s dst_sel W = ONE), its glow would be about 3× weaker. That would explain the glows and the white cores, but not the mid-tone lift by itself [decompiled; the mismatch is inferred].
3. **The lighting/fog volumes (low-medium).** The 3D volumes are built a slice at a time through LBP3-specific texture-cache paths (`slices_of_volume`, the `d2fdcafe` expansion path). Errors there would be local, not the global mid-tone lift measured [inferred].
4. **The game's look on a PC monitor (low now).** The exact-shot PS4 comparison shows a real mid-tone difference that the capture levels don't explain.
5. **The render-target fill change `fix/rt-fills` (low).** `17b018000` and `17b818000` are clears now, but the alpha fill (mask `ff000000`) still takes the old path. Worth one A/B with `LBP3_RT_FILLS=old` [inferred].

## Diagnostic build (not a fix)

Branch `diag/exposure`: runtime `130eee5` (worktree `D:\Recomps\lbp3-wt\exposure`) and shadPS4 `ea637f43` (worktree `D:\Recomps\lbp3-wt\exposure-shadps4`), both on `port` `7d04e28` / `55d38d87`. Built, and the 7 offline test programs pass. Local only.

- `LBP3_FILL_QUEUE_LOG=1`: an `[exposure] masked fill <dst> mask <m> value <v> … from gfx|asc vqid N` line, once per destination, mask, value and queue. It settles whether the fills come from the graphics ring or a compute queue.
- `LBP3_ALPHA_FILL=skip`: drops the alpha-only fills (mask `ff000000`) so the composite sees the alpha the scene wrote. This is the A/B for hypothesis 1, not a fix: it may leave stale alpha where the PS4 clears it later.

## In-game plan (when the game is free, about 30–45 minutes)

1. **Bunkum A/B on the scripted route.** Run `tools/t08_bunkum.sh` with the diagnostic exe (backup-and-trap swap) in three configurations: default; `LBP3_ALPHA_FILL=skip`; `LBP3_RT_FILLS=old`. Turn on `LBP3_FILL_QUEUE_LOG=1` in all of them.
   - **Capture:** the WELCOME shot, from 34–38 s into the cutscene, with screenshots or `fgdump`.
   - **Score:** luma p10/25/50/75/90, the share of pixels ≥ 245, and the sky and banner medians against the PS4 storyboard tiles 17–19, after the same compression round-trip.
   - **Match:** if `skip` brings the median to about 128 and the clipped share to about 18%, hypothesis 1 is confirmed.
2. **Queue log.** Note which queue issues the scene-alpha fill and the other fills.
   - **If a compute queue:** trace its label waits around the composite (the runtime's early-release counters and the learned addresses) and fix the ordering. That is the real fix, behind a switch.
   - **If the graphics ring:** decode what the PS4 does differently there. Check the composite's T# for the scene (dst_sel W) and the scene CB's export of alpha.
3. **If hypothesis 1 fails:** extend the dump (the scene RGBA at the composite, the bloom with alpha, the volumes, the composite's cb0, every T# dst_sel) and replay the composite offline.
4. **After any fix:**
   - re-take the human's three example scenes;
   - compare the Pod, the Introduction fog, Needlepoint, the photo and the Create sky;
   - run the full regression.
   - Saves only via `saves.sh`; the human's saves are never driven directly.

## Sources

- [hyperdude95, "Prologue – The Legend of Bunkum" (PS4)](https://www.youtube.com/watch?v=zBMwCD_dhC8): storyboard sheet `M0` (tiles 17–19, the WELCOME shot), and stills `hq1` (WELCOME, later in the shot) and `maxresdefault` (the Pod in space, "Long ago, before the Negativitron…"; its porthole and stars are not blown out).
- [Thejollymage, "Manglewood Swamp" (PS4)](https://www.letsplayindex.com/video/little-big-planet-3-walkthrough-gameplay-part-5-manglewood-DDbJqliGuIU): still `hq2`, the hub's marble pedestal, an extra comparison point.
- Ticket 08's Create-level stills ([Kaspar1111, Controls](https://www.youtube.com/watch?v=5casMpxG4io), `hq2`).
- The Crypts cutscene and the travel beam: no PS4 frames found.

## Comments

- 2026-11-12 evening, **the in-game A/B** (diagnostic build `diag/exposure`: runtime `e42bac3`, shadPS4 `24eb68d1`).
  - **Method:** the Legend of Bunkum from the story test save, via `exp_bunkum.sh` in the job's temp folder (1080p, 165 Hz display, frame generation off). Twelve window captures land in `analysis\exposure\<tag>\`. `s01`–`s03` are the WELCOME shot (the PS4 storyboard tiles 17–19); `s06`–`s08` are the "…all ceaselessly pumped…" shot, which is the PS4 `hq1` still at 480×360.
  - **Scoring:** `exp_score.py`, after a YouTube-like JPEG round trip, with p10 / 25 / 50 / 75 / 90 luma and the share of pixels ≥ 245.

  | Run | WELCOME luma p10 / 25 / 50 / 75 / 90 | ≥ 245 | Sky median | "pumped" shot sky / banner |
  |---|---|---|---|---|
  | PS4 | 1–2 / 55–58 / 123–134 / 215–217 / 235–236 | 18% | 163–167, 243–245, 238–239 | 159, 240, 236 / 193, 77, 52 |
  | default | 4–6 / 90–104 / 166–171 / 227–230 / 239 | 27–28% | 164–170, 249–252, 245–247 | 160, 249, 249 / 225, 107, 58 |
  | `LBP3_ALPHA_FILL=skip` | 5–6 / 63–70 / 118–123 / 165–167 / 177–179 | 0.6–1.5% | 111, 183–185, 191–192 | 106, 178, 185 / 175, 55–82, 21–63 |
  | `LBP3_RT_FILLS=old` | same as default (within 2 levels) | 26–28% | same | same |
  | `LBP3_BLOOM_FEEDBACK=off` (bright-pass 0.7 → 0; log: 2 literals changed) | same as default | 27–28% | same | same |

  - **Queue log:** every masked fill comes from the graphics ring (`from gfx`), including the scene-alpha clear (`171fe8000` mask `ff000000`). So the fills run in ring order, before the composite, as on the PS4 [measured]. **Hypothesis 1's mechanism (a compute queue run early) is ruled out**, and so is a port reordering.
  - **Ruled out:** bloom feedback (hypothesis 2) and `fix/rt-fills` (hypothesis 5) change nothing [measured].
  - **Skipping the alpha fill turns the whole image into the raw scene.** With no fill the composite sees alpha ≈ 1 everywhere, sky included [measured]. The darks and mid-tones then match the PS4 (median 118–123 against 123–134), but the sky and highlights fall far too dark (sky 111/184/192 against 163/244/239; p90 178 against 236; 1% clipped against 18%).
  - **The PS4 lies between "all graded" (default) and "all raw" (skip), per region** [measured, YouTube still]. Its sky is about fully graded: (159 − 106) / (160 − 106) ≈ 0.98. Its banner is about 0.4 graded, with R at 0.36 and G at 0.42 of the way from raw to graded. So the PS4 composite sees a per-pixel alpha: about 0 on the sky and about 0.6 on the banner. The port gives 0 everywhere, or 1 everywhere with the fill skipped.
  - **The composite constants** (`LBP3_COMPOSITE_CB=1`, 103 samples) look sane:
    - grade A is a mild saturation matrix (diagonal 1.04–1.16, small off-diagonals, offset −0.04 to −0.13);
    - grade B is a uniform 1.04–1.11 (offset −0.02 to −0.07);
    - the key-colour blend is off (cb[39] = 0), so A alone applies;
    - final scale 1, offset 0;
    - vignette tint about 0.15–0.19.

    So the over-brightness is not in the constants. It is the 1.3 × scene + bloom path being applied everywhere [measured].
  - **Stills:** `analysis\exposure\compare_pumped.png` and `compare_welcome.png` (PS4 | port now | port with the fill skipped).
  - **Next, open:** what the scene alpha holds at the composite on the PS4. Two checks remain:
    - Capture the scene buffer's alpha at the alpha-fill hook (pre-fill, the material alpha) and trace which draws write alpha over the sky. On the port the sky's alpha is 1 before the fill, and on the PS4 it must be about 0 at the composite.
    - Look at the per-draw alpha handling in shadPS4: CB_TARGET_MASK alpha bits, the SPI shader export formats (32_AR, FP16_ABGR and the like), and separate alpha blend factors. A pass that should leave alpha untouched but writes 1 on the port would explain "1 everywhere".

    The leading idea for "how the PS4 keeps alpha through the fill" is GCN cache behaviour: the fill writes through TC/L2 while the scene's dirty CB-cache lines are flushed after it. It is unproven, and the skip run argues the pre-fill alpha is wrong anyway.
  - **No fix branch and no user build:** none of the tested mechanisms is the cause.
  - **State:** the human's saves equal `human_play_20261112_1845`. The clone's exe is unchanged (md5 `85d55ed2…`); the runs used the worktree exe directly. No marker, kept copy or game is left, and the temporary `CUSA00473_bloomdiag` shader cache was deleted.
- 2026-11-12 night, **the alpha trace** (`diag/exposure` runtime `90417cf`, shadPS4 `648dbdec`).
  - **Captures** (`LBP3_FG_SCENE=1`): the scene buffer `171fe8000` at the WELCOME shot (`analysis\fg_dump\welcome\`), taken just before the alpha-only fill and as the composite samples it.
    - Before the fill, alpha = 255 on 2,072,611 of 2,073,600 pixels, sky and banner included [measured].
    - At the composite, alpha = 0 on 2,063,617 pixels [measured, Pod frame; the WELCOME frame matches].
    - So the port's composite grades everywhere, as found before.
  - **Draws on the scene buffer:** none between the fill and the composite. The draws after the fill (#1205–#1206) target the velocity buffer, and #1207 the display buffer [measured].
  - **Material draws:** blend off (`21000504`), target/shader mask `f/f`, export `ABGR_FP16`, CB_COLOR_INFO `0002a828`. That decodes as 8_8_8_8 UNORM, comp_swap ALT, **fast_clear 1**, blend_clamp 1, simple_float 1 [measured].
  - **The PS4 orders the fill strictly** [measured, PM4 between the draws]:
    - before each fill: `EventWriteEop 00038504` (CACHE_FLUSH_AND_INV_TS) + `WaitRegMem`;
    - after each fill: `EventWriteEos 0000062f` (CS done) + `WaitRegMem` + `AcquireMem 80c40000`;
    - before the composite: more `AcquireMem` cache syncs.
  - So on a PS4 the composite also sees scene alpha 0 everywhere, and the CB-cache and compute-race ideas are ruled out. **The alpha path is emulated faithfully and is not the cause.**
  - **Formats are not the cause either:**
    - render-target views take the CB's number format (the display CB is UNORM, `00028828`), so there is no double sRGB encode;
    - sampled-texture views match the T#'s SRGB/UNORM in all but 3 of about 2,500 bindings, and those 3 are aliases.
  - **What that leaves.** The PS4 output is graded too, so its mid-tones being ≈ the port's *raw* scene means the port's raw scene (before the composite) is about 1.3–1.4× brighter in mid-tones and darks than the PS4's, with highlights and sky clipping alike. The difference is upstream of the composite, in lighting/shading.
  - **Candidates, untested:**
    - the shadow map (2048² D32 at `170038000`: depth compare, bias, PCF). Missing or weak shadows would brighten mid-tones and darks and leave the sky alone;
    - the 3D lighting volumes (slice mapping and the expansion path; `LBP3_EXPAND=old` A/B);
    - the RGBA16F light-atlas conversion.
  - **No fix and no user build yet.** Next: an A/B of `LBP3_EXPAND=old` at the WELCOME shot, then a check of the shadow sampling (a frame with clear shadows against PS4 footage, and the shadow-compare sampler state in shadPS4).
- 2026-11-12 night, **when did it get bright** (`diag/exposure` runtime `de63901`, shadPS4 `648dbdec`; the same capture and scoring as before):
  - **Recent switches, each A/B'd at the WELCOME shot:** `LBP3_EXPAND=old`, `LBP3_GS_STAGES=0`, `LBP3_GPU_FLUSH=off`, `LBP3_SUBMIT_WAIT=all`, `LBP3_PROTECT=old`. All score the same as default within 1–3 levels: median 164–171, p25 88–104, 26–29% clipped [measured]. With `LBP3_RT_FILLS=old` (earlier), none of the last two days' changes moves the brightness.
  - **Milestone 1** (clone `18984f6` + submodule `25b48b3a`, built in `D:\Recomps\lbp3-wt\m1old`): the same (median 164–171, p25 88–100, 26–29%) [measured]. The shader recompiler is unchanged between `25b48b3a` and `55d38d87`, so sharing the shader cache doesn't bias it. Milestone 1 is upstream plus our milestone-1 fixes, and upstream alone can't reach Bunkum (gap 8's white screen).
  - **So the brightness is long-standing**, older than our work, and is not a regression.
  - **Shadows:**
    - 149 depth-only draws make a 2048² D32 shadow map (`170038000`, ps `462ef291`).
    - `8acd0a6c` converts it, reading depth through a (0, 0, 1, R) swizzle (T# dst_sel 0014; the depth arrives in .w), into a 1024² RGBA16F packed-depth texture `16f020000`: RG coarse, BA fractional.
    - `4750ad22` blurs that into `16f830000`, which the lighting passes sample (180 bindings). Nothing samples the shadow depth with a compare sampler.
    - The captured `16f830000` at the WELCOME shot (`LBP3_FG_SHADOW=1`, `analysis\exposure\shadow_channels.png`) holds the casters seen from the light: the mountains, the stage with the gramophone and bulbs, the flags, the clouds and the balloon. Values are finite, RG 0.09–1 and BA 0–1 [measured]. **So shadows are generated.** Whether the lighting applies them at full strength is not verified.
  - **No SSAO or AO buffer exists in this frame.** The `1899c0000`/`18a1c0000` chain (ps `e67468f6`, `bf58c824`, `3f2f2c7e`, then `e1d50faa` blended into the scene with src-alpha) is depth of field: CoC in alpha, blurred, blended back [decompiled shape + trace].
  - **Narrowed to:** a long-standing difference in the scene's own lighting/shading (mid-tones about 1.3–1.4× brighter before the composite), present since at least milestone 1. That points to a shadPS4 translation issue in the material/lighting shaders: the lighting volumes as 3D textures, the packed-depth shadow lookups, an export or packing conversion. Or the PS4 reference footage is processed darker in the mid-tones; it is one YouTube capture.
  - **Next, if pursued:**
    - a second, independent PS4 reference (an official screenshot or another capture) to rule out the capture;
    - then per-pass scene snapshots (after the opaque pass, after lighting) compared against a hand-evaluated material shader for one surface (the banner) using the dumped constants.
  - No fix.

- 2026-11-12 evening, **parked at the human's request** ("think we can park this for now then, I'll need to collect footage from PS4"). It resumes when they have PS4 references. Most useful:
  - Share-button screenshots, which are lossless PNGs, of the Legend of Bunkum's "Welcome to Bunkum" shot, the Crypts intro cutscene and the Pod's travel beam;
  - with the PS4's HDR off, RGB range noted, and brightness settings at default.
  Then compare the same regions as before; if the gap holds, check the banner's material maths by hand (step 2 above). The diagnostic branches `diag/exposure` (runtime `de63901`, shadPS4 `648dbdec`) are local, as are the `exposure*` and `m1old*` worktrees.

- 2026-11-13, **two leads from the LBPPC graphics study** (`research/lbppc_graphics.md`), for when this ticket resumes. Parked; no game time spent:
  1. Dump EVERY input to the final composite (`5ed16706`) across consecutive frames and look for one that only ever gets brighter. That's how LBPPC found their glow pile-up from a lost compute clear ("orange flares"). We never did that complete check.
  2. LBP3 has a first-boot brightness calibration screen. If its stored value feeds the composite's 1.3× factor or the colour grade, the profile may simply hold a brighter setting than the PS4 default in the YouTube reference. Check where the calibration value lives and what the user's profile holds.
