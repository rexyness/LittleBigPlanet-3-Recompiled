#!/bin/sh
# Watch a human freeze test: report the device-lost report, flip stalls (12 s, with an F10
# whose own report file is copied), recovery, and process exit.
#   watch_freeze.sh <tag>
# It follows one game process (the lbp3.exe running when it starts, by PID) and keeps
# analysis/<tag>_stderr.txt up to date as it goes, never replacing a copy with a smaller file.
# (2026-11-10: a watcher that followed the image name outlived its game when the next one
# started within its 2 s poll, then copied the next game's freshly reset log over its own;
# and stopping a Monitor doesn't end this shell, so hitl_start.sh kills leftover watchers.)
cd /d/Recomps/lbp3-recompiled || exit 1
tag=${1:-t07}
log=analysis/remote_stderr.txt
out="analysis/${tag}_stderr.txt"
pid=""
for i in $(seq 1 60); do
  pid=$(tasklist //fi "imagename eq lbp3.exe" //fo csv //nh 2>/dev/null | head -1 | cut -d, -f2 | tr -d '"\r')
  case "$pid" in ''|*[!0-9]*) pid=""; sleep 1 ;; *) break ;; esac
done
[ -z "$pid" ] && { echo "no lbp3.exe to watch"; exit 1; }
echo "watching lbp3.exe pid $pid"
alive() { tasklist //fi "PID eq $pid" 2>/dev/null | grep -q lbp3.exe; }
size() { wc -c < "$1" 2>/dev/null || echo 0; }
kept=0
keep() {  # copy the log unless it shrank (a new launch resets it)
  s=$(size "$log")
  if [ "$s" -ge "$kept" ]; then cp "$log" "$out" && kept=$s; fi
}
last=""; since=$(date +%s); stalled=0; n=0; started=0; lost=0
while true; do
  if [ $lost -eq 0 ] && grep -q '^\[gpu\] Vulkan device lost' "$log" 2>/dev/null; then
    lost=1; echo "DEVICE LOST reported (after frame $last)"
  fi
  if ! alive; then
    keep
    echo "game exited (last: $last frames); log kept as $out ($kept bytes)"; exit 0
  fi
  keep
  cur=$(grep -o '^\[video\] [0-9]* frames flipped' "$log" 2>/dev/null | tail -1 | awk '{print $2}')
  now=$(date +%s)
  if [ -n "$cur" ] && [ "$cur" != "$last" ]; then
    [ $started -eq 0 ] && { echo "game is flipping frames"; started=1; }
    [ $stalled -eq 1 ] && echo "flips resumed at $cur"
    last=$cur; since=$now; stalled=0
  elif [ $started -eq 1 ] && [ $stalled -eq 0 ] && [ $((now - since)) -ge 12 ]; then
    n=$((n + 1)); stalled=1
    before=$(grep -c '^\[perf\] wrote' "$log")
    echo "STALL $n: no flips for 12 s after frame $last; sending F10"
    printf 'f10\n' > analysis/input_cmd.txt
    for i in $(seq 1 30); do
      [ "$(grep -c '^\[perf\] wrote' "$log")" -gt "$before" ] && break
      sleep 1
    done
    f=$(grep '^\[perf\] wrote' "$log" | tail -1 | sed 's/.*wrote analysis\\perf\\//' | tr -d '\r')
    if [ "$(grep -c '^\[perf\] wrote' "$log")" -gt "$before" ] && [ -n "$f" ]; then
      cp "analysis/perf/$f" "analysis/perf/${tag}_stall${n}.txt" && echo "F10 report $f saved as ${tag}_stall${n}.txt"
    else
      echo "F10 report did not appear within 30 s"
    fi
  fi
  sleep 2
done
