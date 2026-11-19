# sl_run.sh <tag> [NAME=value ...]: the session-log check. Two instances on the LAN path; the guest joins,
# both move for 40 s, the host starts the Introduction (the Pod computer's Play, then the first level of the
# story map), 40 s in the level, then the guest's game is killed and the host is left 16 s to end the
# connection. Every kind of [ses] line should be in the two logs after it. Takes and releases the game lock.
cd "$(dirname "$0")"
export OWNER=netcode-seslog EXE_SRC=${EXE_SRC:-/d/Recomps/lbp3-wt/netdelay/build}
tag=$1; shift
# SL_PLAIN=1: the probes off (no [slots], [sync] or [sim] lines), as a player runs it; launch2.sh then waits its full 150 s.
if [ -n "$SL_PLAIN" ]; then extra=LBP3_SYNC_PROBE=0; else extra=LBP3_TRACE_SIM=1; fi
bash launch2.sh $tag LBP3_P2P_PATH=lan $extra "$@" || exit 1
. ./lib.sh; pids
bash join.sh $tag B j_$tag
# move <seconds> [host]: the guest moves; the host only with "host" (in the Pod it stays at the Pod computer).
move() { end=$(( $(date +%s) + $1 )); while [ $(date +%s) -lt $end ]; do
  [ "$2" = host ] && cmdq $A 'lstick 1 0 700\nwait 300\nlstick -1 0 700\ncross 150\n'
  cmdq $B 'lstick -1 0 600\ncross 150\nwait 300\nlstick 1 0 600\n'; sleep 4; done; }
move 40; cap $PA ${tag}_pod_a; cap $PB ${tag}_pod_b
echo "level start $(date +%T)"
cmdq $A 'square 150\nwait 2500\nup 100\nwait 700\ncross 150\nwait 3000\ncross 150\nwait 6500\ncross 150\nwait 4500\nleft 100\nwait 1500\ncross 150\nwait 1000\n'
sleep 30; move 40 host; cap $PA ${tag}_level_a; cap $PB ${tag}_level_b
echo "guest killed $(date +%T)"; taskkill //f //pid $PB >/dev/null; sleep 16
killall3; sleep 2; game_lock_release $OWNER; echo done $(date +%T)
