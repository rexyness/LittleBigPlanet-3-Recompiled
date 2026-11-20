# launch2.sh <tag> [NAME=value ...]: launch3.sh for two instances (A hosts, B joins), with settings
# for one instance only in RIG_A_ENV and RIG_B_ENV (space-separated NAME=value), e.g. a loss test
# on the host's incoming direction alone:
#   EXE_SRC=/d/Recomps/lbp3-wt/netdelay/build RIG_A_ENV="LBP3_P2P_TEST_LOSS=217" launch2.sh fd1 LBP3_P2P_PATH=lan LBP3_P2P_DATA3=0
# pids.txt gets a third, empty pid so that lib.sh's helpers work unchanged.
. "$(dirname "$0")/lib.sh"
tag=$1; shift
[ -e "$A/stderr_$tag.txt" ] && { echo "tag $tag already has logs: pick another"; exit 1; }
[ "$(game_lock_holder | cut -d' ' -f1)" = "$OWNER" ] || game_lock_acquire $OWNER 60 || exit 1
if tasklist | grep -qi '^lbp3.exe'; then echo "another lbp3.exe is running: stop"; game_lock_release $OWNER; exit 1; fi
cp -f "$EXE_SRC/lbp3.exe" "$EXE_SRC/SDL3.dll" "$RUN/exe/" || exit 1
EXE="$(cygpath -w "$RUN/exe/lbp3.exe")"
COMMON="LBP3_ONLINE=1 LBP3_ONLINE_SERVER=https://play.lbppc.com LBP3_SHADNET=1 LBP3_SHADNET_SERVER=play.lbppc.com LBP3_GAME_DIR=D:\Recomps\ROMs\PS4 LBP3_SYNC_PROBE=1 LBP3_P2P_DIAG=1 LBP3_DISPLAY=165hz LBP3_STATUS_FLIPS=30 LBP3_ASYNC_PIPELINES=1 LBP3_VOLUME=0 $*"
: > "$RUN/pids.txt.new"
for i in A:Kistopawa B:Kistopawa2; do r=${i%%:*}; name=${i##*:}; root=$(eval echo \$$r); w=$(cygpath -w $root); own=$(eval echo \$RIG_${r}_ENV)
  powershell -NoProfile -ExecutionPolicy Bypass -File "$(cygpath -w "$J/inst.ps1")" -Root "$w" -Tag $tag -Exe "$EXE" $COMMON $own LBP3_ONLINE_NAME=$name "LBP3_SHADER_CACHE_DIR=$w\cache" > "$RUN/pid$r.txt" 2>&1 < /dev/null
  printf '%s ' "$(tr -d '\r\n' < "$RUN/pid$r.txt")" >> "$RUN/pids.txt.new"; sleep 15
done
mv -f "$RUN/pids.txt.new" "$RUN/pids.txt"; cat "$RUN/pids.txt"; echo
n=0; until grep -q -E '^\[slots\]|^\[ses\] .* state 11' "$B/stderr_$tag.txt" 2>/dev/null || [ $n -ge 150 ]; do sleep 2; n=$((n+2)); done
sleep 12
for r in $A $B; do grep -m1 -E 'signed in as|LBP3_SHADNET=1 but off' $r/stderr_$tag.txt | cut -c1-200; done
grep -q 'but off' $A/stderr_$tag.txt $B/stderr_$tag.txt && { echo "not signed in: stopping"; killall3; game_lock_release $OWNER; exit 1; }
echo ready
