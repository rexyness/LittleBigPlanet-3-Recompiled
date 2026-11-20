# Three-instance co-op rig. A = host (Kistopawa), B = Kistopawa2, C = Kistopawa3, each from its own
# working directory under D:\Recomps\lbp3-wt\coop. Adapted from the two-instance harness of the
# joining-and-leaving lane (seq.sh / run2.sh, 2026-11-15).
# Sign-in: each instance reads user\config\online_account.txt in its working directory, or
# LBP3_SHADNET_PASSWORD from the environment this is run in. The rig holds no password.
J="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RUN="${RIG_RUN:-${TMPDIR:-/tmp}/rig3}"; mkdir -p "$RUN/shots" "$RUN/exe"   # pids, screenshots, the exe copy
A=/d/Recomps/lbp3-wt/coop/A; B=/d/Recomps/lbp3-wt/coop/B; C=/d/Recomps/lbp3-wt/coop/C
# A private copy of the exe, so that a build in the lane it comes from isn't blocked by a running game.
EXE_SRC="${EXE_SRC:-/d/Recomps/lbp3-wt/res/build}"
OWNER=${OWNER:-netcode-rig3}
. /d/Recomps/lbp3-recompiled/.scratch/lbp3-port/tools/game_lock.sh
# cmdq <root> '<remote input lines with \n>' (LBP3_REMOTE_INPUT, runtime/src/input/gamepad_sdl.cpp)
cmdq() { r=$1; n=0; while [ -f "$r/analysis/input_cmd.txt" ] && [ $n -lt 100 ]; do sleep 0.1; n=$((n + 1)); done
  printf "$2" > "$r/analysis/input_cmd.tmp" && mv -f "$r/analysis/input_cmd.tmp" "$r/analysis/input_cmd.txt"; }
cap() { powershell -NoProfile -ExecutionPolicy Bypass -File "$(cygpath -w "$J/cap.ps1")" -ProcId "$1" -Out "$(cygpath -w "$RUN/shots/$2.png")" >/dev/null; }
pids() { read PA PB PC < "$RUN/pids.txt"; }
capall() { pids; cap $PA $1_a; cap $PB $1_b; cap $PC $1_c; }
killall3() { pids; for p in $PA $PB $PC; do taskkill //f //pid $p >/dev/null 2>&1; done; }
