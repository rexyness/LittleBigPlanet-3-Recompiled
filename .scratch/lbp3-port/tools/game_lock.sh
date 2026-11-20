# game_lock.sh: one game at a time across parallel lanes. Source it, then:
#   game_lock_acquire <owner> [timeout_s]   wait (default 2 h) until the game is free, then hold it
#   game_lock_release <owner>               release (only the holder can)
#   game_lock_holder                        print who holds it, or "free"
# Hold it for the whole in-game step (saves.sh swaps, launches, cleanup) and release right after
# tools/stop_cleanup.sh-style checks pass. Never launch lbp3.exe without holding it, and never while
# a lbp3.exe you didn't start is running (the human may be playing): then release and wait.
LOCK=/d/Recomps/lbp3-recompiled/analysis/game.lock.d
game_lock_acquire() {
  owner=$1; t=${2:-7200}; n=0
  while ! mkdir "$LOCK" 2>/dev/null; do
    [ "$n" -ge "$t" ] && { echo "lock: timeout after ${t}s, held by $(cat "$LOCK/owner" 2>/dev/null)"; return 1; }
    sleep 15; n=$((n + 15))
  done
  echo "$owner $(date +%F_%T)" > "$LOCK/owner"
  if tasklist 2>/dev/null | grep -qi "lbp3.exe"; then
    echo "lock: a game is already running (not ours: maybe the human); releasing"
    rm -f "$LOCK/owner"; rmdir "$LOCK"; return 1
  fi
  echo "lock: acquired by $owner"
}
game_lock_release() {
  [ -d "$LOCK" ] || return 0
  if [ "$(cut -d' ' -f1 "$LOCK/owner" 2>/dev/null)" = "$1" ]; then
    rm -f "$LOCK/owner"; rmdir "$LOCK" && echo "lock: released by $1"
  else echo "lock: not held by $1 (holder: $(cat "$LOCK/owner" 2>/dev/null))"; fi
}
game_lock_holder() { cat "$LOCK/owner" 2>/dev/null || echo "free"; }
