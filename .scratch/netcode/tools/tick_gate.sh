#!/bin/sh
# tick_gate.sh <prefix> <build dir>: the tick gate ("keep our tick") for a runtime build from a worktree,
# online off: the Introduction twice (steps != 1 under 1%) and the Thruster recipe three times, which are
# checks 1 and 2 of .scratch/lbp3-port/tools/m1_checks.sh. Those scripts run the clone's build\lbp3.exe, so
# the worktree's exe is put there for the run and the clone's own put back at the end, by any path. Runs
# under the game lock, on a copy of the shader cache, and never while another lbp3.exe is running.
p=$1; src=$2
T=/d/Recomps/lbp3-recompiled/.scratch/lbp3-port/tools; S="$(cd $T && pwd -W)"
cd /d/Recomps/lbp3-recompiled || exit 1
. $T/game_lock.sh
OWNER=netcode-gate
game_lock_acquire $OWNER 60 || exit 1
[ -e build/lbp3.exe.gate_kept ] && { echo "an earlier gate left build/lbp3.exe.gate_kept: put it back first"; game_lock_release $OWNER; exit 1; }
cp -p build/lbp3.exe build/lbp3.exe.gate_kept || { game_lock_release $OWNER; exit 1; }
restore() { taskkill //f //im lbp3.exe >/dev/null 2>&1; sleep 1; cp -pf build/lbp3.exe.gate_kept build/lbp3.exe && rm -f build/lbp3.exe.gate_kept
  echo "clone exe back: $(md5sum build/lbp3.exe | cut -c1-8)"; game_lock_release $OWNER; }
trap restore EXIT
echo "clone exe: $(md5sum build/lbp3.exe | cut -c1-8); under test: $(md5sum "$src/lbp3.exe" | cut -c1-8)"
cp -f "$src/lbp3.exe" build/lbp3.exe || exit 1
cache=/d/Recomps/lbp3-wt/gate_cache_$p; rm -rf "$cache"; mkdir -p "$cache" && cp -r user/shader_cache/CUSA00473 "$cache/" || exit 1  # the live title only, not the kept copies beside it
export LBP3_SHADER_CACHE_DIR="$(cygpath -w "$cache")"
unset LBP3_ONLINE LBP3_SHADNET LBP3_ONLINE_SERVER
export LBP3_VOLUME=0  # muted: the human asked for no sound from test runs (2026-11-17)
echo "== the Introduction, 2 runs"
for i in 1 2; do
  powershell -NoProfile -ExecutionPolicy Bypass -File "$S/pace_run_intro.ps1" -Tag "${p}_intro$i" >/dev/null 2>&1
  f="analysis/pace_${p}_intro${i}_stderr.txt"
  python "$T/sim_steps_level.py" "$f"
  grep -c '^\[ses' "$f" | sed 's/^/  [ses] lines (0 = the session log stayed off): /'
done
echo "== the Thruster save recipe"
sh "$T/t10_ab.sh" "$p":3:-
rm -rf "$cache"
ls analysis/savedata_live_is_test analysis/savedata_human_kept 2>/dev/null | sed 's/^/LEFT BEHIND: /'
echo "gate done $(date +%T)"
