#!/bin/sh
# stop_cleanup.sh: after an interrupted test chain, put everything back the way the human left it.
#   stop_cleanup.sh [expected md5 of the clone's build/lbp3.exe]
# 1. Kills the chain: shells running a script from the planning repo's tools, the worktree tools
#    or a job tmp folder, remote_start.ps1, and tail -f on analysis files; then every lbp3.exe.
#    (A first version matched any command line with the clone's path and killed unrelated shells.)
# 2. If build/lbp3.exe was swapped (a build/lbp3.exe.*_bak exists): restores the backup whose md5
#    matches the expected value, deletes that backup once the md5 checks out.
# 3. If the human's saves are in the kept copy: human_restore (m2_lib.sh) puts them back and
#    clears the marker. Removes an empty kept-guests folder.
# 4. Prints the state: game, marker, kept copy, exe md5, branch.
# TaskStop on an agent or a background shell does not end its child scripts, and a force-kill
# skips their exit traps (2026-11-12, twice): this does by hand what the traps would have done.
cd /d/Recomps/lbp3-recompiled || exit 1
want=$1
powershell -NoProfile -Command "
\$me = $$
Get-CimInstance Win32_Process | Where-Object {
  \$_.Name -match '^(sh|bash|powershell|python|tail)\.exe$' -and
  \$_.CommandLine -match '(lbp3-port[\\\\/]tools|lbp3-wt[\\\\/]_tools|jobs[\\\\/][0-9a-f]+[\\\\/]tmp)[\\\\/][^ ]+\.(sh|ps1|py)|remote_start\.ps1|tail(\.exe)?\x22? .*-f .*analysis' -and
  \$_.CommandLine -notmatch 'stop_cleanup' -and \$_.CommandLine -notmatch '^\S*bash\.exe\x22? -c '
} | ForEach-Object { Stop-Process -Id \$_.ProcessId -Force -ErrorAction SilentlyContinue; 'killed ' + \$_.ProcessId + ' ' + \$_.Name }
" 2>/dev/null
taskkill //f //im lbp3.exe >/dev/null 2>&1 && echo "killed lbp3.exe"
sleep 1
for b in build/lbp3.exe.*_bak; do
  [ -f "$b" ] || continue
  m=$(md5sum < "$b" | cut -c1-32)
  if [ -n "$want" ] && [ "$m" = "$want" ]; then
    cp "$b" build/lbp3.exe && [ "$(md5sum < build/lbp3.exe | cut -c1-32)" = "$want" ] && rm -f "$b" &&
      echo "exe restored from $b (md5 ok)"
  else
    echo "exe backup $b has md5 $m (expected ${want:-?}): left alone"
  fi
done
if [ -d analysis/savedata_human_kept ]; then
  . /d/Recomps/lbp3-pc/.scratch/lbp3-port/tools/m2_lib.sh
  human_restore
fi
[ -d analysis/savedata_human_kept.guests ] && rmdir analysis/savedata_human_kept.guests 2>/dev/null &&
  echo "removed the empty kept-guests folder"
echo "-- state"
tasklist | grep -i lbp3.exe || echo "game: not running"
[ -f analysis/savedata_live_is_test ] && echo "MARKER STILL SET" || echo "marker: clear"
ls -d analysis/savedata_human_kept* 2>/dev/null | sed 's/^/LEFT OVER: /'
echo "exe md5: $(md5sum < build/lbp3.exe | cut -c1-32)${want:+ (expected $want)}"
echo "branch: $(git branch --show-current) $(git log --oneline -1 | cut -c1-60)"
