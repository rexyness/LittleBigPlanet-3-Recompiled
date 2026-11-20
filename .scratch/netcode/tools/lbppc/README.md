# LBPPC test tools (the LBPPC co-op mode, docs/research/lbppc-coop.md)

For checking this port against LBPPC's own client on one PC, as on 2026-11-20:

- `lbppc_start.ps1 -Tag <t>`: the installed LBPPC (`D:\Recomps\lbppc`) from a separate data folder
  (`D:\Recomps\lbp3-wt\lbppc-test`: a copy of its `out\` and `user\cache`, `lbppc.ini` with
  `mute_unfocused=1`, no sign-in file), signed in as a test account read from an account file (the
  password only goes into the process's environment), UPnP off, its sync log on, input from
  `pad.txt` (`BB_PAD_FILE`: button names held while listed, `lx=`..`ry=` 0..255; empty releases).
- `lbppc_stop.ps1`: **unmutes first**, then stops it. Windows keeps a per-app mute by the executable's
  path, so a muted test LBPPC otherwise leaves the player's own LBPPC muted.
- `mutepid.cpp`: `mutepid <exe name> <seconds> [unmute]`, mutes (or unmutes) that program's audio
  sessions as they appear, printing each one's state when first seen.
- `fpsample.cpp`: `fpsample <pid> [samples] [ms]`, each thread's MXCSR and x87 control word, sampled
  (suspend, read the context, resume). Build both with clang-cl (`/std:c++20`, mutepid with `ole32.lib`).

Set `LBPPC_TEST_TMP` to a scratch folder first (logs go there; the two exes in `$LBPPC_TEST_TMP\fpsample`).
Run under the game lock, never while the user plays, and keep logs out of the repo (they hold the
PC's public address).
