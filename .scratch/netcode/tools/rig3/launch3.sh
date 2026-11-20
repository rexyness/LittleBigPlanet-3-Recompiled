# launch3.sh <tag> [NAME=value ...]: takes the game lock, starts A, B and C 15 s apart and waits for
# the sync probe's first line on C. Logs: <instance>/stderr_<tag>.txt. Afterwards: drive with cmdq,
# look with capall, end with killall3 and game_lock_release $OWNER (all in lib.sh).
#   launch3.sh tp1 LBP3_P2P_PATH=lan LBP3_MSG_PROBE=1 LBP3_JOIN_ALLOW=Kistopawa,Kistopawa2,Kistopawa3
# LBP3_P2P_PATH=lan keeps the three pairs off LBPPC's relay. Without LBP3_JOIN_ALLOW the link refuses
# any player who isn't a shadNet friend (join protection), which cuts a pair with no change to the link.
. "$(dirname "$0")/lib.sh"
tag=$1; shift
[ -e "$A/stderr_$tag.txt" ] && { echo "tag $tag already has logs: pick another"; exit 1; }
[ "$(game_lock_holder | cut -d' ' -f1)" = "$OWNER" ] || game_lock_acquire $OWNER 60 || exit 1
if tasklist | grep -qi '^lbp3.exe'; then echo "another lbp3.exe is running: stop"; game_lock_release $OWNER; exit 1; fi
cp -f "$EXE_SRC/lbp3.exe" "$EXE_SRC/SDL3.dll" "$RUN/exe/" || exit 1
EXE="$(cygpath -w "$RUN/exe/lbp3.exe")"
COMMON="LBP3_ONLINE=1 LBP3_ONLINE_SERVER=https://play.lbppc.com LBP3_SHADNET=1 LBP3_SHADNET_SERVER=play.lbppc.com LBP3_GAME_DIR=D:\Recomps\ROMs\PS4 LBP3_SYNC_PROBE=1 LBP3_P2P_DIAG=1 LBP3_DISPLAY=165hz LBP3_STATUS_FLIPS=30 LBP3_ASYNC_PIPELINES=1 LBP3_VOLUME=0 $*"
: > "$RUN/pids.txt.new"
for i in A:Kistopawa B:Kistopawa2 C:Kistopawa3; do r=${i%%:*}; name=${i##*:}; root=$(eval echo \$$r); w=$(cygpath -w $root)
  powershell -NoProfile -ExecutionPolicy Bypass -File "$(cygpath -w "$J/inst.ps1")" -Root "$w" -Tag $tag -Exe "$EXE" $COMMON LBP3_ONLINE_NAME=$name "LBP3_SHADER_CACHE_DIR=$w\cache" > "$RUN/pid$r.txt" 2>&1 < /dev/null
  printf '%s ' "$(tr -d '\r\n' < "$RUN/pid$r.txt")" >> "$RUN/pids.txt.new"; sleep 15
done
mv -f "$RUN/pids.txt.new" "$RUN/pids.txt"; cat "$RUN/pids.txt"; echo
n=0; until grep -q '^\[slots\]' "$C/stderr_$tag.txt" 2>/dev/null || [ $n -ge 150 ]; do sleep 2; n=$((n+2)); done
sleep 12
for r in $A $B $C; do grep -m1 -E 'signed in as|LBP3_SHADNET=1 but off' $r/stderr_$tag.txt | cut -c1-200; done
grep -q 'but off' $A/stderr_$tag.txt $B/stderr_$tag.txt $C/stderr_$tag.txt && { echo "not signed in: stopping"; killall3; game_lock_release $OWNER; exit 1; }
echo ready
