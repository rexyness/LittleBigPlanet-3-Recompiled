# rf_run.sh <tag> <minutes> [NAME=value ...]: the RTT-on-a-fast-path run. Two instances on the LAN path at no
# added delay with gap-free delivery off, the connection probe and the raw RTT sample spread on; the settings
# given go to both. The guest joins, both move every 4 s, and it stops at the end or when the host's game ends
# the connection. Takes and releases the game lock.
#   bash rf_run.sh rf1 6 LBP3_P2P_TEST_LOSS=19 LBP3_RTT_FLOOR_MS=33.4
cd "$(dirname "$0")"
export OWNER=netcode-rtt EXE_SRC=${EXE_SRC:-/d/Recomps/lbp3-wt/netdelay/build}
tag=$1; min=$2; shift 2
. ./lib.sh
finish() { pids 2>/dev/null; for p in $PA $PB; do taskkill //f //pid $p >/dev/null 2>&1; done; sleep 2; game_lock_release $OWNER; }
trap finish EXIT
bash launch2.sh $tag LBP3_P2P_PATH=lan LBP3_P2P_DATA3=0 LBP3_CONN_PROBE=1 LBP3_RTT_HIST=1 LBP3_TRACE_SIM=1 "$@" || exit 1
bash join.sh $tag B j_$tag
end=$(( $(date +%s) + min * 60 ))
while [ $(date +%s) -lt $end ]; do
  grep -q 'the game ends connection' $A/stderr_$tag.txt && { echo "connection ended $(date +%T)"; sleep 5; break; }
  cmdq $A 'lstick 1 0 700\nwait 300\nlstick -1 0 700\ncross 150\n'; cmdq $B 'lstick -1 0 600\ncross 150\nwait 300\nlstick 1 0 600\n'; sleep 4
done
capall ${tag}_end; echo done $(date +%T)
