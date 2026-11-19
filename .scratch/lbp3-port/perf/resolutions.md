# Render sizes: LBPPC's resolution list (feat/resolutions)

> Status: built and tested on branch `feat/resolutions` (runtime and shadPS4). Not merged, not pushed. Results below.

## What the user asked

"for the resolutions allowed i think you can go crazy with it, the same resolutions that lbppc has" (2026-11-14). LBPPC's launcher (emurphy04, aad6a91, `launcher/lbppc_launcher.py` RESOLUTIONS) offers:

| Size | Aspect |
|---|---|
| 1280x720 | 16:9 ("lighter on the GPU") |
| 1920x1080 | 16:9 (the PS4's own) |
| 2560x1440 | 16:9 |
| 3840x2160 | 16:9 (4K) |
| 2560x1080 | 21:9 |
| 3440x1440 | 21:9 |
| 3840x1600 | 21:9 (2.4:1) |
| 3840x1080 | 32:9 |
| 4480x1260 | 32:9 |

## How

**One mapping** (shadPS4 `src/common/lbp3_res.h`): the size `LBP3_RES` asks for (`1440` as before, `1080`, or `WxH` from 1280x720 up to 4480 wide, no narrower than 16:9) and the function from a 1080p size to it:
- full-screen widths and heights scale (rounded to the nearest pixel);
- the smaller targets (the quarter-size 480x270 ones and the bloom chain's 1280x720, 640x360, 320x180) grow but never shrink (LBPPC's rule: the game cuts a centred 256x256 square from one of them at 0x4014f0);
- shader literals follow their own axis and target: widths 1920/960/3840/7680 and the quarter target's 480; heights 1080/540/2160/4320 and 270.

Every consumer uses it: the eboot patches, the shader-literal rescale, the cache salt and folder, the frame-generation velocity hook and the capture tool's buffers, the launcher.

**Eboot patches** (runtime `src/core/res_patches.cpp`, replacing the hard 1440p table in `patches.cpp`): one table of the game's size sites, each naming its 1080p value and kind. At 2560x1440 the 86 edits are byte for byte the old table (`tests/restest.cpp` pins it against `main` 330a1bc). The MainRenderTargets block stays 68 MB at 1440p, else grows with the pixel count plus 6% (never below 0x2441400).

**Wider than 16:9** (group `aspect`, 8 sites; addresses as found by LBPPC, our code):
- the camera's aspect (0x3fdbc3, the constructor 0x3fdba0);
- the 2D canvas width (0x1060a04: 1280 -> 720 x aspect);
- six built-in layout constants (0xe675c0, 0xe8f668, 0xe90394, 0xe903b8, 0xe90448, 0xe9046c) scaled by canvas/1280.

**Online with a wide size:** the simulation's camera keeps 16:9 (players' worlds must step alike; LBPPC saw "The game diverged" when one player was ultrawide). Only the projection (0x3fe160) reads the wide aspect: its `vmovss xmm2,[rbx+8]; vucomiss xmm2,xmm1` at 0x3fe17f becomes `jmp stub; nop4`, and the stub (near the image) loads the screen's aspect, redoes the comparison and jumps back to the `jbe` at 0x3fe188. No stack is used. `LBP3_RES_ASPECT_SPLIT=1/0` forces it; `LBP3_RES_SKIP=aspect` keeps the old stretched picture.

**Shaders:** the 17 listed shaders get their literals rescaled per axis. At 2560x1440 the factors are exactly 4/3 and 3/4 and the salt is the original "RES1440" one, so existing 1440p caches still match. Every other size has its own salt (`width << 20 | height` under "RES"), so a size never picks up another size's translations. `LBP3_RES_CACHE=separate` uses `<serial>_res<W>x<H>` (`_res1440` unchanged).

**Presenter:** an image whose aspect differs from the window's by more than 0.5% is letterboxed (a 21:9 size on a 16:9 screen) instead of stretched; matching aspects draw exactly as before.

**Launcher:** a dropdown with the nine sizes (Home's quick settings and Graphics). `launcher.ini` writes `res=WxH`; older files' `res=1440`/`res=1080` still read. 2560x1440 still sets `LBP3_RES=1440`. The "Finer glow" toggle applies above 1080p. `play.bat`'s `RES` takes a WxH too. `play_best.bat` is untracked (the user's own copy in the best worktree) and was not touched; it passes `RES=1440` only.

**Runtime defaults unchanged:** no `LBP3_RES` = 1920x1080, the game untouched.

## Tests

**Offline** (res worktree build): `restest` 2614 checks, 0 failed (all 86 sites at 2560x1440 identical to `main`'s table, in order; every size's display/pitch/stacked/small-target/region values; the shader literals agree with the targets; salts distinct per size, 1440 = the original). `rescachetest` 68/0, `launchertest` 391/0 (the dropdown, old `res=1440/1080` files, round trip), and framegentest, uxtest, osktest, dlgtest, motiontest, clocktest, addconttest, unlocktest all 0 failed.

**In game** (2026-11-14 02:07–02:44, `D:\Recomps\lbp3-wt\_tools\res_chain.sh`, the res worktree's exe run from its own folder with the clone as root, under the game lock, test saves, a copy of the best shader cache in `analysis\res_lane_cache`, frame generation on, `LBP3_RES_SHADERS=1`, the 165 Hz test display, windowed 1280x720 so wide sizes are letterboxed). Per size:
- **A** (`res_lane.sh`): the Pod, the Pod computer's Home, Edit Now, the pause menu's fade, a photo (Popit, Take A Photo).
- **B** (`res_run2.sh`, the story save): the Prologue map, Needlepoint Peaks, the heavy walk and 20 s standing at the basket (the frame times are this route's).

| Size | Sites | Pod / Home / Create / pause | Photo (JPEG size) | Needlepoint | Heavy walk work (frames > 40 ms) | Basket work |
|---|---|---|---|---|---|---|
| 1280x720 | 75 | right (small targets kept) | 640x360 | loads | 17.6 ms (1) | 15.6 ms |
| 2560x1440 | 86 | right | 640x360 | loads | 21.8 ms (0) | 19.7 ms |
| 3840x2160 | 86 | right | 640x360 | loads | **35.7 ms (120; worst 83 ms)** | 32.0 ms (6) |
| 2560x1080 | 55 | right, wide view | 853x360 | loads | 21.1 ms (1) | 18.9 ms |
| 3440x1440 | 94 | right, wide view | 860x360 | loads | 27.3 ms (1) | 24.7 ms |
| 3840x1600 | 94 | right, wide view | 864x360 | loads | 28.7 ms (2) | 26.4 ms |
| 3840x1080 | 55 | right, wide view | 1280x360 | loads | 25.1 ms (1) | 22.9 ms |
| 4480x1260 | 94 | right, wide view | 1280x360 | loads | 28.9 ms (2) | 26.6 ms |

Every run: 0 GPU warnings, 0 crashes, 0 asserts, 0 "NOT on the list", no "[res] ... NOT applied"; the capture readback ran for every photo. "Sites" counts the changed ones (heights keep their value at 2560x1080 and 3840x1080). The 1440p session compiled **no** rescaled shader: the copied best cache's 1440p entries (the original salt) were picked up as before. Each other size compiled its 12 + 2 rescaled shaders once.

- **Read-back textures** (the Pod computer and the pause fade, the "zoomed corner, giant Sackboy" case LBPPC describes) look right at every size.
- **Wide sizes:** the view widens (the house left of the Pod, more of Needlepoint), the menus and HUD stay centred on the widened canvas, and the 16:9 test window shows black bars.
- **Online aspect split, forced on offline** (`LBP3_RES_ASPECT_SPLIT=1`, 3440x1440): no camera-site edit, the stub installed. The frames match the plain wide run (mean pixel difference 0.0–2.3, animation noise), so the projection gets the wide aspect while the camera keeps 16:9. Photo, readback and GPU are as above.
- **Screenshots:** `analysis\screenshots\m2_reslane_a_<W>_<H>_*.png` and `m2_res_b_<W>_<H>_*.png`. Logs: `analysis\reslane_a_*`, `analysis\res_b_*`. Chain output: `analysis\res_chain1.txt`.

**1920x1080 on the new build** (`LBP3_RES=1920x1080`): no `[res]` line (the game untouched), the route as at the other sizes, heavy walk 19.7 ms of work and 0 frames over 40 ms. The 720p glow on the Needlepoint bottle matches 1080p; 4K's is tighter, because the bloom chain grows with the size (the existing "finer glow" behaviour at 1440p).

**Regression** (`_tools/res_regress.sh both`, 03:14–03:41: the res exe swapped into the clone's `build\lbp3.exe`, then put back by md5 `95d8af03…`; cache copies; `analysis\res_chain2.txt`):

| | Defaults (no LBP3_RES) | 1440p + frame generation (play_best's) |
|---|---|---|
| Introduction, frames with 0 or 2 sim steps | 0.13% / 0.27% | 0.75% / 1.01%, reruns 0.85% / 0.63% |
| Thruster recipe | 3/3 | 3/3 |
| Bunkum, then Needlepoint | 16/16 voice lines, loads | 16/16 voice lines, loads |
| Create sky | cyan | cyan |
| `[res]` | none | 86 sites, the summary line |
| nvlddmkm/Display events | none | none |

**The 1440p Introduction numbers are load noise, not a regression.** The first four runs overlapped the other lane's builds (best's exe was rebuilt at 03:46). An interleaved A/B with no compiler running (`_tools/res_ab_intro.sh 3`, `analysis\res_chain5.txt`) read:

| Round | feat/resolutions | v0.1.1 (release worktree) |
|---|---|---|
| 1 | 0.13% | 0.88% |
| 2 | 0.00% | 0.25% |
| 3 | 0.13% | 0.00% |

Earlier v0.1.1 runs that night (03:49–03:53) read 0.13% and 0.50%. Both builds land in the same band once nothing else loads the CPU.

## Open issues

- **4K is over budget in heavy scenes:** 35.7 ms of work on the Needlepoint walk, with 120 frames over 40 ms (worst 83 ms). The Pod and Create mode hold 30 fps. The likely costs are the per-frame CPU readback and SDL upload of a 33 MB frame, plus GPU time. Not measured further. Every other size stays under 30 ms on the walk.
- **Online with a wide size: untested with two players.** The split keeps the camera's own aspect (what the simulation reads) at 16:9, as LBPPC's does, and its offline test shows the projection takes the wide aspect. Whether anything else in the simulation reads the projection (0x3fe160 writes a matrix into the camera) and still diverges was not checked. That needs a two-player online session with one wide player. `LBP3_RES_ASPECT_SPLIT=0` would turn the split off and use the plain wide camera (that diverges, per LBPPC). `LBP3_RES_SKIP=aspect` keeps a stretched 16:9 view for everyone.
- **Wide sizes take wide photos:** 853x360 to 1280x360 instead of 640x360, also under the split. The photo's width follows the projection's aspect. They encode, save and show fine; they are just wider stickers and level icons.
- **The 27 held literal shaders** (`fix/res1440-shaders-44`) are unchanged: none showed up ("NOT on the list" 0 at every size on these routes).
- **Not run at the new sizes:** the heavy co-op, the on-screen keyboard and the dialogs (their overlays draw in window pixels and are size-independent), a real wide monitor (the tests letterbox on 16:9), and fullscreen or borderless at a wide size.
- **`play_best.bat`** (untracked, the user's) still passes only `RES=1440`. The launcher and the tracked `play.bat` take every size.

## State left

The clone's `build\lbp3.exe` was restored by md5 after each swap (`95d8af03…`). No `savedata_live_is_test` marker and no kept copy are left; saves.sh put the player's saves back after every run. The game lock is released, and the best worktree was not touched. The test caches `analysis\res_lane_cache` (a copy of the best cache, now holding every size's salted shaders) and `analysis\res_lane_cache_port` are test-only and can be deleted.
