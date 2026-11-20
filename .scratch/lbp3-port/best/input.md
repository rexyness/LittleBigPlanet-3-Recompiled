# Best-of-both-worlds: input lane (`feat/best-input`)

**Branch:** runtime `feat/best-input` from `port` `34e3e8d`, worktree `D:\Recomps\lbp3-wt\binput`. The submodule is unchanged (`553be3ba`). Pushed to the fork.

**Licence:** everything here is written for this port. The ideas come from LBPPC's `docs/input-research.md` (emurphy04/LBPPC `aad6a91`):
- learning the gyro offset while the pad is still;
- a virtual tilt for pads without sensors;
- the mouse as a relative touch-pad finger, with wheel gestures.

Their `runtime_motion.c`, `runtime_pad.c` and `runtime_mouse_touch.c` carry no SPDX line, so they fall under the repo's GPL-3.0-or-later and were not copied. `src/runtime_input_config.c` (the remap parser) is GPL-2.0-or-later and reusable, but wasn't ported; see "Not done".

## What it adds

| Feature | Switch / default | Notes |
|---|---|---|
| **Motion filter** (`input/motion.*`): the gyro offset is learned after 0.5 s still and then tracked (1 s time constant); corrected rates under 0.008 rad/s read 0; the orientation integrates the corrected gyro from identity at reset, leaning toward the measured gravity relative to the gravity at reset (1/s gain, not while shaken). | on; `LBP3_MOTION=raw` restores the old pass-through exactly | The acceleration stays raw: LBP3 leans Sackboy's head from it (LBPPC's in-game finding). One filter per slot, reset whenever the slot's occupant changes. |
| **Rest gravity for pads without sensors** (keyboard player, Xbox pads, virtual pads) | changes with the filter; `raw` keeps the old value | Now +1 G on Y, as a flat DualShock 4 / DualSense reports (SDL's axes, and shadPS4's ×0.098 scaling of m/s²). The old default was −1 G on Y. |
| **Tilt keys** (actions `tilt_left/right/forward/back`) | Keypad 4/6/8/2 by default, rebindable in `user\config\input.ini`; `LBP3_TILT_KEYS=off` disables | Player 1 only. A full 35° tilt, eased at 4 rad/s. While away from level it replaces the sensors' tilt: the acceleration shows gravity tilted, the orientation the tilt. Remote test input: `action tilt_right <ms>`. |
| **Mouse as touch pad** (`input/mouse_touch.*`) | off; `LBP3_MOUSE_TOUCH=1`, then F7 captures and releases the mouse | **Move** slides a finger, relative, down at the centre, lifting after 0.3 s still or near an edge. **Left button** presses the pad. **Wheel** spreads or pinches two fingers; **Ctrl + wheel** twists them. A system dialog or the on-screen keyboard opening releases the capture. `LBP3_MOUSE_TOUCH_SPEED` (default 1) scales it. Remote test input: `touch on|off|down|up`, `touch move dx dy`, `touch wheel n`, `touch twist n`. |
| `LBP3_MOTION_LOG=1` | off | A `[motion]` line per slot every 5 s (sensors, still, offset, orientation, degrees from reset up, accel, rate). The "gyro offset learned" line is always logged once per pad. |

## Offline

- **`tests/motiontest`: 41 checks, 0 failed.**
  - **Offset learning:** offset (0.02, −0.015, 0.01) rad/s with noise, learned to within 0.002.
  - **A minute idle:** 0.43° from reset, against 94° with raw integration of that offset.
  - **Tracking:** yaw 1 rad at 1 rad/s comes out as 57.3°; a 30° pitch is tracked.
  - **Drift correction:** a 20° tilt the gyro missed is pulled back to the measured gravity within 10 s.
  - **Shaking and reset:** shaking makes no NaN; reset returns to identity.
  - **Virtual tilt:** right gives accel (−0.573, 0.820, 0), front up gives −sin 35° on z, and it eases back to level.
  - **Mouse:** a relative slide; lifting when still and at the edge; a new id per touch; the button press; spread 400 → 520 units; twist 14.8° per notch; release.
- **All 11 existing offline test programs pass:** cooptest 81, dlgtest 154, osktest 263, dialogtest 368, jpegtest 157, imetest 403, framegentest 103, audiotest 32, addconttest 55, rescachetest 68, unlocktest 21. `padtest` waits for a real pad, so it wasn't run.

## In game (2026-11-13, 05:47–05:58)

Two short holds on the game lock, both on the 165 Hz display, with test saves through `saves.sh` and a shader-cache copy (`LBP3_SHADER_CACHE_DIR`; `user\shader_cache` untouched). Scripts are in the job's tmp folder (`binput_ingame.sh`, `binput_touch2.sh`), logs and progress in the clone's `analysis\binput\`, stills as `analysis\screenshots\m2_binput_*`.

- **Co-op, `t13_coop.sh`:** run with the input build swapped into the clone, then the original restored by md5 (`d5d335ad…`). **PASS, 9 PASS and 0 FAIL**, covering user 2's login, logout and login, their own saves, no GPU warnings, and frames continuing.
- **Motion, `LBP3_MOTION_LOG=1`, in the Pod:**
  - At rest, slot 0 (keyboard, no pad attached overnight) logged orientation (0,0,0,1), accel (0, 1, 0) and rate 0 for the whole session: no drift.
  - The virtual tilt through remote `action tilt_right/left/back/forward`:
    - right: accel (−0.573, 0.820, 0), orientation (0, 0, −0.300, 0.954), 34.95° from reset up;
    - left: accel (+0.573, 0.820, 0);
    - back (front up): accel (0, 0.820, −0.573);
    - forward: accel (0, 0.820, +0.573);
    - back to level afterwards.
  - **Sackboy's head leans with each tilt** in the stills (`m2_binput_motion_tilt*.png` against `_rest.png`).
- **The −1 G versus +1 G rest gravity:** Pod stills at rest with `LBP3_MOTION=raw` (−1 G) and the filter (+1 G) are alike. Sackboy stands upright in both first shots, and both second shots catch the same idle look-around. The sign change makes no visible difference at rest.
- **The mouse as the touch pad, in the Introduction** (`LBP3_MOUSE_TOUCH=1`, remote `touch on`, then a press):
  - **The first press opened the Popit** (`m2_binput_t2_click.png`, "Customize Character" highlighted, "Close Popit").
  - **A second press picked the highlighted entry:** the Costumes page opened (`m2_binput_t2_click2.png`).
  - Circle closed it. Keyboard Square opens the same Popit, for comparison (`m2_binput_t2_square.png`).
  - In the Pod, a touch-pad press does nothing visible, with a mouse or with a real pad.
- **No real pad with sensors was connected overnight** (`sensors no` in every line), so the gyro-offset learning and drift on a real DualSense are covered by `motiontest` only. On the next session with the DualSense, `LBP3_MOTION_LOG=1` shows "gyro offset learned" and the drift.
- **After both holds:** the human's saves are identical to `human_play_20261113_0046`, the clone's exe md5 is `d5d335ad…`, no game is running, and the lock is released.

## Not done

- **Keyboard and mouse remapping** of pad buttons and sticks, from LBPPC's `src/runtime_input_config.c`. That file is GPL-2.0-or-later and can be reused, but it only parses `input.ini`. Applying the bindings lives in their untagged GPL-3.0 `runtime_pad.c`, so it would have to be reimplemented against our pad layer: about half a session. Our `input.ini` keeps its keyboard actions, plus the four new tilt actions. That's low value for a DualSense player.
- **A check against a real pad:** that tilting a DualSense right leans the head right, the same as the virtual tilt. That needs the human's pad.

## Merge

**Merge-ready:** runtime `feat/best-input` `3acfa0c`, submodule unchanged. The defaults:
- the motion filter is on, and `LBP3_MOTION=raw` gives the old behaviour;
- the tilt keys are on (they do nothing unless pressed), and `LBP3_TILT_KEYS=off` disables them;
- the mouse touch pad is off; turn it on with `LBP3_MOUSE_TOUCH=1`.
