# m2_lib.sh: helpers for the milestone 2 game scripts (t17_*.sh, t_m2_*.sh), sourced after
# cd /d/Recomps/lbp3-recompiled. Saves are handled by saves.sh (saves_keep/saves_baseline).
#   cmdq 'cross 100\nwait 500\n'   queue remote input (waits until the game took the last batch)
#   shot <name>                    screenshot to analysis/screenshots/m2_<name>.png
#   launch [NAME=value ...]        kill any game, start a remote-controlled one
#   wait_log <regex> <seconds>     wait for a stderr line; returns 1 on timeout
#   stubs [regex]                  the [stub] lines of the milestone 2 libraries seen so far
#   saves_ls                       the save files with size and md5 (for the notes)
#   save_settle [seconds]          wait until no save file has changed for that long (default 8)
cmdq() {
  n=0; while [ -f analysis/input_cmd.txt ] && [ $n -lt 100 ]; do sleep 0.1; n=$((n + 1)); done
  # Written whole, then renamed into place: the game polls the file every 50 ms and once read a
  # half-written "pads 2" as "pads" (2026-11-12, t13_coop gsr_t13: player 2 never came back).
  printf "$1" > analysis/input_cmd.tmp && mv -f analysis/input_cmd.tmp analysis/input_cmd.txt
}
shot() { powershell -NoProfile -File tools/capture.ps1 -Out "analysis/screenshots/m2_$1.png" >/dev/null; }
launch() {
  taskkill //f //im lbp3.exe >/dev/null 2>&1; sleep 1; rm -f analysis/input_cmd.txt
  powershell -NoProfile -Command "Set-Location D:\\Recomps\\lbp3-recompiled; & .\\tools\\remote_start.ps1 $*" >/dev/null
}
wait_log() {
  n=0; while ! grep -Eq "$1" analysis/remote_stderr.txt 2>/dev/null; do
    [ $n -ge "$2" ] && return 1; sleep 1; n=$((n + 1)); done
}
M2_LIBS='libSceImeDialog|libSceJpegEnc|libSceJpegDec|libSceMsgDialog|libSceSaveDataDialog|libSceCommonDialog'
stubs() { grep -E "^\[stub\] (${1:-$M2_LIBS}) ::" analysis/remote_stderr.txt; }
saves_ls() { (cd user/savedata/CUSA00473 && find . -type f | sort | while read -r f; do
  printf '%s %s %s\n' "${f#./}" "$(stat -c %s "$f")" "$(md5sum < "$f" | cut -c1-8)"; done); }
save_settle() {
  q=${1:-8}; last=""; still=0
  while [ $still -lt "$q" ]; do
    now=$(find user/savedata/CUSA00473 -type f -printf '%P %s %T@\n' | sort)
    if [ "$now" = "$last" ]; then still=$((still + 1)); else still=0; last=$now; fi
    sleep 1
  done
}
# Interactive sessions (several tool calls, so no exit trap), with saves.sh's kept copy and marker:
#   human_keep        keep the human's saves (refuses if the live ones are marked as test saves)
#   use_baseline <d>  install test saves (needs the kept copy; sets the marker)
#   human_restore     put the kept copy back, clear the marker
# The rest of user/savedata (guests' profiles and anything else) is kept and put back whole, by
# saves.sh's others_* helpers.
. /d/Recomps/lbp3-recompiled/.scratch/lbp3-port/tools/saves.sh
human_keep() {
  taskkill //f //im lbp3.exe >/dev/null 2>&1; sleep 1
  if [ -d analysis/savedata_human_kept ]; then echo "saves: reusing the kept copy"
  elif [ -f analysis/savedata_live_is_test ]; then echo "saves: live saves are test saves and nothing is kept; restore from analysis/savedata_backups first"; return 1
  else cp -r user/savedata/CUSA00473 analysis/savedata_human_kept && others_keep; fi
}
use_baseline() {
  [ -d analysis/savedata_human_kept ] || { echo "use_baseline: run human_keep first"; return 1; }
  taskkill //f //im lbp3.exe >/dev/null 2>&1; sleep 1; : > analysis/savedata_live_is_test
  others_clear
  rm -rf user/savedata/CUSA00473 && cp -r "$1" user/savedata/CUSA00473
}
human_restore() {
  [ -d analysis/savedata_human_kept ] || { echo "human_restore: no kept copy"; return 1; }
  taskkill //f //im lbp3.exe >/dev/null 2>&1; sleep 1
  others_restore || echo "saves: the other profiles could not all be put back (analysis/savedata_human_kept.others kept)"
  rm -rf user/savedata/CUSA00473 && cp -r analysis/savedata_human_kept user/savedata/CUSA00473 &&
    rm -rf analysis/savedata_human_kept && rm -f analysis/savedata_live_is_test && echo "saves: the human's are back"
}
# Picture of the game with its dialog windows on top (PrintWindow of each window, composed at their
# screen positions; never shows other applications). Never grab the screen itself: it would show
# whatever covers the game, the human's own windows included.
dshot() { powershell -NoProfile -ExecutionPolicy Bypass -File D:/Recomps/lbp3-recompiled/.scratch/lbp3-port/tools/capture_dialog.ps1 -Out "analysis/screenshots/m2_$1.png"; }
# The [ime]/[jpeg]/[dialog]/[modal] lines logged after stderr line $1.
m2v_lines() { tail -n +"$(($1 + 1))" analysis/remote_stderr.txt | grep -E '^\[(ime|jpeg|dialog|modal)\]|^\[stub\] libSce(Ime|Jpeg|MsgDialog|SaveDataDialog|CommonDialog|GameCustomData)'; }
