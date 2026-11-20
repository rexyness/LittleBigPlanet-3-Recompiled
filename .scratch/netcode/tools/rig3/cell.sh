# cell.sh <tag> <pod seconds> [NAME=value ...]: one cell of the set-conditions matrix. Two instances on the LAN
# path (Kistopawa hosts, Kistopawa2 joins) with the settings given to both; RIG_A_ENV / RIG_B_ENV for one only.
#   the guest joins; <pod seconds> in the host's Pod (the guest moves, the host stays at the Pod computer);
#   the host starts the Introduction; 30 s to load and 60 s in the level, both moving; both games are closed.
# CELL_FREEZE="1000 3500": during the Pod time the guest's frame thread is stopped for each of these many
#   milliseconds, 20 s apart, from 20 s in (LBP3_TEST_FREEZE, set here on both).
# CELL_LEVEL_FIRST=1: the host starts the Introduction alone first and the guest joins into the level.
# CELL_SUSPEND_AT_BLOCK=<seconds>: the guest's whole process is suspended for that long as soon as the host's
#   log shows the join's block (the hung-join run).
# CELL_B_FREEZE_AT=<pod second>:<ms>: one freeze of the guest's frame thread at that second of the Pod time.
# Marks (seconds since the epoch) go to $RUN/cell_<tag>.marks for cell_sum.py, which prints the cell's row.
# Every exit path closes the games and frees the game lock.
cd "$(dirname "$0")"
export OWNER=netcode-cells EXE_SRC=${EXE_SRC:-/d/Recomps/lbp3-wt/netdelay/build}
tag=$1; pod=$2; shift 2
. ./lib.sh
marks="$RUN/cell_$tag.marks"; : > "$marks"
mark() { echo "$1 $(date +%s) $(date +%T)" >> "$marks"; echo "$1 $(date +%T)"; }
finish() { pids 2>/dev/null; for p in $PA $PB; do taskkill //f //pid $p >/dev/null 2>&1; done; sleep 2; game_lock_release $OWNER; }
trap finish EXIT
bash launch2.sh $tag LBP3_P2P_PATH=lan LBP3_TRACE_SIM=1 LBP3_TEST_FREEZE=1 "$@" || { echo "CELL $tag: launch failed"; exit 1; }
pids
suspend() { powershell -NoProfile -Command "\$s='[DllImport(\"ntdll.dll\")] public static extern int NtSuspendProcess(IntPtr h); [DllImport(\"ntdll.dll\")] public static extern int NtResumeProcess(IntPtr h);'; \$t=Add-Type -MemberDefinition \$s -Name N -Namespace W -PassThru; \$p=Get-Process -Id $1; [void]\$t::NtSuspendProcess(\$p.Handle); Start-Sleep -Milliseconds $2; [void]\$t::NtResumeProcess(\$p.Handle)"; }
level() { cmdq $A 'square 150\nwait 2500\nup 100\nwait 700\ncross 150\nwait 3000\ncross 150\nwait 6500\ncross 150\nwait 4500\nleft 100\nwait 1500\ncross 150\nwait 1000\n'; }
# move <seconds> [host]: the guest moves; the host only with "host".
move() { end=$(( $(date +%s) + $1 )); while [ $(date +%s) -lt $end ]; do
  [ "$2" = host ] && cmdq $A 'lstick 1 0 700\nwait 300\nlstick -1 0 700\ncross 150\n'
  cmdq $B 'lstick -1 0 600\ncross 150\nwait 300\nlstick 1 0 600\n'; sleep 4; done; }
if [ -n "$CELL_LEVEL_FIRST" ]; then mark level_cmd; level; sleep 45; cap $PA ${tag}_hostlevel; fi
mark join_pressed
if [ -n "$CELL_SUSPEND_AT_BLOCK" ]; then
  ( n=0; until grep -q '\[1\] state 9' "$A/stderr_$tag.txt" || [ $n -ge 600 ]; do sleep 0.1; n=$((n+1)); done
    echo "suspend $(date +%T)"; suspend $PB $(( CELL_SUSPEND_AT_BLOCK * 1000 )) ) &
fi
bash join.sh $tag B j_$tag
n=0; until grep -q '\] Kistopawa state 11' "$B/stderr_$tag.txt" || [ $n -ge 90 ]; do sleep 1; n=$((n+1)); done
grep -q '\] Kistopawa state 11' "$B/stderr_$tag.txt" || echo "CELL $tag: the guest did not get in within 90 s"
mark pod_start
if [ -n "$CELL_FREEZE" ] || [ -n "$CELL_B_FREEZE_AT" ]; then
  ( at=20; for ms in $CELL_FREEZE; do sleep 20; echo "freeze $ms $(date +%s) $(date +%T)" >> "$marks"; echo $ms > "$B/analysis/freeze_cmd.txt"; done
    if [ -n "$CELL_B_FREEZE_AT" ]; then sleep ${CELL_B_FREEZE_AT%%:*}; echo "freeze ${CELL_B_FREEZE_AT##*:} $(date +%s) $(date +%T)" >> "$marks"; echo ${CELL_B_FREEZE_AT##*:} > "$B/analysis/freeze_cmd.txt"; fi ) &
fi
if [ -n "$CELL_LEVEL_FIRST" ]; then move $pod host; else move $pod; fi
mark pod_end; cap $PA ${tag}_pod_a; cap $PB ${tag}_pod_b
if [ -z "$CELL_LEVEL_FIRST" ]; then
  mark level_cmd; level; sleep 30; mark level_play; move 60 host; cap $PA ${tag}_level_a; cap $PB ${tag}_level_b
fi
mark end
python -I "$(cygpath -w "$J/cell_sum.py")" $tag "$(cygpath -w "$marks")" "$(cygpath -w $A/stderr_$tag.txt)" "$(cygpath -w $B/stderr_$tag.txt)"
