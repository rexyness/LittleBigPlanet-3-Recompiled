# 1440p: a full scan of the game's shaders for 1080p literals (2026-11-12)

The 1440p shader swap (`LBP3_RES_SHADERS=1`, shadPS4 `56120048`) rescales the 1080p render-size float literals of 17 shaders, listed by hash. The list came from a scan of the translated cache, i.e. only shaders the game had already compiled. This scan covers every shader in the game data, found offline without running the game.

**Result:** 44 shaders carry such literals: the 17 already listed plus **27 new ones**. None of the 27 had been compiled yet in either shader cache. All 44 are now listed on branch `fix/res1440-shaders`:
- shadPS4 `b9eaec0f` (from `port` `56120048`): one file, `src/shader_recompiler/recompiler.cpp`;
- runtime `2ca9761` (from `port` `2cd464a`): the submodule pointer.

Both are pushed to the forks and not merged. The build passes all 9 offline test programs. Merging waits for an in-game check.

## How the hashes are found (reproducible)

- **Where the hash comes from:** shadPS4 keys a shader by the `shader_hash` in its `OrbShdr` footer (`regs_shader.h`, `BinaryInfo`). Sony's compiler writes that footer into the binary, so the hash is in the game data itself.
- **Script:** [`tools/res1440_fullscan.py`](../tools/res1440_fullscan.py). It walks every `.farc` (FARC, and the FAR4 level archives), every loose file in `gamedata_orbis`, and the eboot. It decompresses each resource and finds each footer, its code, the literal constants (a GCN decoder) and the 1080p test (`Lbp3ResFactor`'s exact rule).
- **Two resource layouts hold shaders:**
  - **material resources** (`GMTb` and other `…b` binaries): a header, then a chunk table of zlib or stored chunks;
  - **the engine's compiled programs** (`.fpo`/`.vpo`): files starting `00 01 00 01`, followed by the same chunk table. All the post-processing shaders live here: finalcomp, SMAA, fog and the rest. The first pass missed this layout and found none of the 17.
- **Checks:**
  - 54 shaders extracted from the intro archive are byte-identical to the runtime's own dumps (`analysis\shaders\dumps`). All 156 dumped shaders are found.
  - **Every** program the game has ever compiled into either cache is in the scanned set: 1,641 of 1,641 in `CUSA00473`, 2,361 of 2,361 in `CUSA00473_res1440`.
  - The GCN literal decoder agrees with a plain dword scan on all 171 dumps and finds every listed shader present there.
- **Totals:** 135,428 unique shaders in 122,806 resources. Run data is in the clone's `analysis\res1440_scan\` (`shaders.json`, `hits.json`, `bin\`; gitignored).
- **Names:** from `output/orbisguids.map`.

## The 27 new shaders

| Shaders | Source | Literals | What it is (confidence) |
|---|---|---|---|
| 10 variants | `gamedata_orbis/gmat/proc_base_outline_special.gmat` | 1/1920, 1/1080 | Object outline effect. A `v_mov_b32` of each constant, i.e. pixel position to screen UV, sampling a screen-sized buffer (high: the pair together is the screen-UV pattern). |
| 10 variants | `gamedata_orbis/gmat/proc_base_xray_special.gmat` | 1/1920, 1/1080 | X-ray silhouette (characters seen through scenery), same pattern (high). |
| `718ff304776166d7` | `pshaders/finalcomp_lowres.fpo` | 1/1920 | Final-composite variant (high). |
| `4bf756f86d15c8aa`, `878c01b81d5beca6`, `dda99281e22a497e`, `f90614b94fe06470`, `fee8b1e66a624a1e` | engine programs, unnamed in the map | 1/480 | Final-composite variants: each shares 51–86% of its instruction words with a named finalcomp variant, and 1/480 is the same quarter-width bloom step as the listed `finalcomp` (high). |
| `92c7741539b51796` | `data/tgpshaderblur.fpo` | 1/960, 1/540 | A blur on a half-resolution target (medium: half-size texel steps; which target isn't traced). |

Hashes, in full:
- **outline:** `01bdeb8c981d9707 0b6ad5b77f97f53d 28fa0138ced530cf 348dfe0656492033 45ebb09b18f0cad9 48127f9846f6d5bd 99c9d482d4b1ae47 c0b6e8311f62243f cb7aeb628c7e0259 eeb805869a0cb137`
- **x-ray:** `6fb557919f13d7dd 81806190305e4920 84d76a355aa6d207 8a7941e68761e0b8 afa557bb8761e0b8 c4840ca6ae16ca10 d4973da9b6e3f34b e22754605aa6d207 e8a99293f879f3d7 fc0d6d669f13d7dd`

**What they'd do at 1440p unscaled (inferred):** the outline and x-ray effects would sample their screen buffers with UVs running to 1.33 instead of 1.0, misplacing or stretching the effect by 4/3. The composite variants would take a 1080p-sized bloom step on the 1440p bloom target, making the glow slightly tighter. The game hasn't compiled any of them yet in hours of play, so they belong to less common situations, e.g. the x-ray silhouette or the outline highlight.

**Cache note:** none of the 27 is in either shader cache now, so merging `fix/res1440-shaders` before the game first compiles them needs no cache cleanup. If the game compiles one first (unscaled, under the old list), its `0x<hash>_*.spv`/`.meta` files in `user\shader_cache\CUSA00473_res1440` must be deleted for the new list to take effect.

## The 17 already listed, named

`vshader_sprite.vpo` (03101c0e), `smaa_edgedetect.vpo` (0b2a3346), `smaa_neighbourblend.vpo`/`.fpo` (2949baf9, 8e1db4f9), `smaa_blendweight.fpo`/`.vpo` (3a0615bd, c9311bbd), `fog_calc2.fpo` (33c89263), `finalcomp.fpo` (5ed16706), `finalcomp_planetscrossfade` (3d19e625), `finalcomp_icon` (8079a39d), `finalcomp_blur` (8f2b909c), `finalcomp_vcr` (b0bb8b37), `gooeyps_blur.fpo` (4d3d6de4), `atmospheric_impactng`/`_rainng.fpo` (d7506ff3, ec6a8d0b), `cloudcomp.fpo` (ebb656d6), `sphere_shadow.fpo` (fdf2b5d5).

## In-game check before merging

Build `D:\Recomps\lbp3-wt\res-shaders-rt` and run at 1440p with `LBP3_RES_SHADERS=1`. Find a spot that shows the x-ray silhouette (Sackboy behind foreground scenery) or the outline highlight, and compare it against 1080p. The log should show `[res] shader … scaled` for the new hashes as they compile. Also run m1_checks at 1440p.

**Not covered:** render sizes passed in constant buffers rather than literals, which would need tracing the game's draw constants; and integer literals, which the recompiler's rule doesn't touch either.

## Trying to trigger the 27 in game (2026-11-13, about 01:00–01:45; not reached)

Goal: get a held outline or x-ray shader to compile in game, then compare the 44 build against the 17 build. Proof would be a `[res] … NOT on the list` line. None came up, so the 27 stay held.

- **Create mode, Thruster level, 1440p with the shader swap, on a cache copy** (`oxA.sh`, job tmp): hovered the popit cursor over the Thruster, grabbed it with Cross, moved it and released it.
  - The grab shows a green/cyan **bounding box**, not the outline material (`analysis\screenshots\m2_ox_grab.png`).
  - Only listed shaders compiled (2 `scaled for 1440p` lines), and there was no `NOT on the list` line.
  - The earlier runs at 1440p hadn't hit them either: the Thruster recipe's popit cursor and tweak menu, `t13_coop`, story walks, the costume browser.
- **Static leads** (Ghidra, `ghidra_probe.sh`):
  - The outline material GUID `0x54c30` appears only in `FUN_000b2330`, as `cmp eax, 0x54c30` at `0xb3e6a` → category `0x11`. It classifies a thing by the GUID of its material (`thing+0x98` → `+0x24`) alongside the neighbouring special materials `0x54c2e`, `0x54c32`–`0x54c36`. So it's a material a thing can carry, not a selection effect (inferred).
  - The x-ray GUID `0x54c39` is nowhere in the eboot, neither as an operand nor as data.
  - The `intro_001.farc` bundle hit (`87d34411…`) is a catalogue with GUID, size, hash and flags per gmat, not a placement.
  - A **"Silhouette" render pass** exists in `FUN_0047a640`, the character render loop. It draws a stencil silhouette of a character when the object at `character+0x1e0` has state byte `+0xd2 == 0x12` (inferred: some held, carried or hidden state). This is the most promising x-ray trigger, but the state isn't identified.
  - Strings `DrawEmitObjectOutline__Q5Thingr` (a script native: an emitter's preview outline in Create?) and `OBJECT_MESH_GENERATED_OUTLINE/_SILHOUETTE` are further candidates, also unidentified.
- **Next, if anyone continues:**
  - identify state `0x12` from the writers of `+0xd2` near the player code;
  - or place an emitter in Create and tweak it, for the emit-object outline;
  - or keep watching play logs for `NOT on the list`.
  The 44 list stays on shadPS4 `fix/res1440-shaders-44` (`b789a896`).
