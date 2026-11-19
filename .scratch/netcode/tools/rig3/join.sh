# join.sh <tag> <B|C> <label> [menu-only]: the named guest opens the Pod computer's friend entry and joins the
# first friend listed; the host (A) allows. With "menu-only" it stops at the menu (look at the shot first).
. "$(dirname "$0")/lib.sh"; pids
tag=$1; g=$(eval echo \$$2); gp=$(eval echo \$P$2); label=$3
if [ "$4" != go ]; then
  cmdq $g 'square 150\nwait 4000\ndown 120\nwait 300\ndown 120\nwait 300\ndown 120\nwait 300\ndown 120\nwait 800\n'; sleep 10; cap $gp ${label}_menu
  [ "$4" = menu-only ] && exit 0
fi
c0=$(grep -c 'connected (' $A/stderr_$tag.txt); echo "$label: join pressed $(date +%T)"; cmdq $g 'cross 150\n'
n=0; until [ $(grep -c 'connected (' $A/stderr_$tag.txt) -gt $c0 ] || [ $n -ge 40 ]; do sleep 1; n=$((n+1)); done
# The host is asked once its host thread has the guest at state 4 (the session log's line); on a slow or lossy
# path that comes later than the 5 s this script used to wait, and an early press is lost.
m=0; until grep -q "\[1\] state [4-9]" $A/stderr_$tag.txt || [ $m -ge 40 ]; do sleep 1; m=$((m+1)); done
sleep 3; cap $PA ${label}_prompt; echo "$label: allow $(date +%T) (waited $n s)"; cmdq $A 'triangle 200\n'
