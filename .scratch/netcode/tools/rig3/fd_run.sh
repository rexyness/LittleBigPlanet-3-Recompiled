# fd_run.sh <tag> <loss period on A> <minutes> ["A-only env"] ["B-only env"]: the fatal-datagram run. Two instances
# with gap-free delivery off and the byte dump, connection probe and step trace on; the guest joins, both move
# every 4 s, and it stops at the end or when the host's game ends the connection. Takes and releases the game lock.
cd "$(dirname "$0")"
export OWNER=netcode-fatal EXE_SRC=/d/Recomps/lbp3-wt/netdelay/build
tag=$1; export RIG_A_ENV="LBP3_P2P_TEST_LOSS=$2 $4" RIG_B_ENV="$5"
bash launch2.sh $tag LBP3_P2P_PATH=lan LBP3_P2P_DATA3=0 LBP3_P2P_TEST_DUMP=1 LBP3_CONN_PROBE=1 LBP3_TRACE_SIM=1 || exit 1
. ./lib.sh
bash join.sh $tag B j1
end=$(( $(date +%s) + $3 * 60 ))
while [ $(date +%s) -lt $end ]; do
  grep -q 'the game ends connection' $A/stderr_$tag.txt && { echo "connection ended $(date +%T)"; sleep 5; break; }
  cmdq $A 'lstick 1 0 700\nwait 300\nlstick -1 0 700\ncross 150\n'; cmdq $B 'lstick -1 0 600\ncross 150\nwait 300\nlstick 1 0 600\n'; sleep 4
done
capall ${tag}_end; killall3; sleep 2; game_lock_release $OWNER; echo done $(date +%T)
