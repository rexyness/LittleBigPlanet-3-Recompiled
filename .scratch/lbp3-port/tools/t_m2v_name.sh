#!/bin/sh
# Milestone 2 integration, text entry by the level name (Create pause menu -> Edit Details ->
# name), on a build with the real libSceImeDialog. Two launches on one save folder (baseline
# savedata_thruster_on restored once):
#   A  set the name with "ime <NAME>" queued before the Cross that opens the field; screenshot
#      Edit Details; options (close). The game saves the level by itself after the confirm
#      (LBPxSAVE/bigfart*); wait until the save files are quiet; kill.
#   B  relaunch: Home (the Recently Edited card), Edit Now, Edit Details: the name is kept.
#      Then "ime-cancel" + Cross on the field: the name stays. Then Cross on the field with
#      nothing queued: the dialog over the game (dshot, capture_dialog.ps1), then ime-cancel.
#   t_m2v_name.sh <tag> [NAME=value ...]      (NEWNAME="..." overrides the name)
cd /d/Recomps/lbp3-recompiled || exit 1
tag=$1; shift
NAME=${NEWNAME:-M2 Test Level}
. /d/Recomps/lbp3-pc/.scratch/lbp3-port/tools/saves.sh; saves_keep; saves_baseline || exit 1
. /d/Recomps/lbp3-pc/.scratch/lbp3-port/tools/m2_lib.sh
log="analysis/m2_${tag}_out.txt"; : > "$log"
note() { echo "$*"; echo "$*" >> "$log"; }
to_name_field() {  # pause menu -> Edit Details -> the name field highlighted
  cmdq 'options 250\nwait 1500\nup 250\nwait 600\nup 250\nwait 600\nup 250\nwait 800\ncross 250\nwait 1500\nright 250\nwait 800\n'; sleep 7
}
note "saves at start: $(saves_ls | tr '\n' ' ')"
# --- A
launch "$@"; sleep 28; cmdq 'cross\nwait 2000\nsquare\nwait 2500\n'; sleep 6; shot "${tag}_A_home"
cmdq 'square 200\n'; sleep 25; shot "${tag}_A_level"
to_name_field; shot "${tag}_A_field"
n0=$(wc -l < analysis/remote_stderr.txt)
cmdq "ime $NAME\ncross 250\nwait 500\n"; sleep 4
shot "${tag}_A_named"
m2v_lines "$n0" | sed 's/^/A: /' | tee -a "$log"
# Confirming the name saves the level by itself (LBPxSAVE/bigfart* is rewritten right after the
# confirm, seen in run m2v_name), so there is no Save Changes step: wait for the files, then quit.
cmdq 'options 250\nwait 1500\n'; sleep 3; shot "${tag}_A_closed"
save_settle 10
tail -n +"$((n0 + 1))" analysis/remote_stderr.txt | grep -q 'bigfart' && note "A: the level save (bigfart) was written after the confirm" || note "A: WARNING: no bigfart write after the confirm"
note "saves after A: $(saves_ls | tr '\n' ' ')"
cp analysis/remote_stderr.txt "analysis/m2_${tag}_A_stderr.txt"
taskkill //f //im lbp3.exe >/dev/null 2>&1; sleep 2
# --- B
launch "$@"; sleep 28; cmdq 'cross\nwait 2000\nsquare\nwait 2500\n'; sleep 6; shot "${tag}_B_home"
cmdq 'square 200\n'; sleep 25
to_name_field; shot "${tag}_B_field"
n0=$(wc -l < analysis/remote_stderr.txt)
cmdq 'ime-cancel\ncross 250\nwait 500\n'; sleep 4
shot "${tag}_B_cancelled"
m2v_lines "$n0" | sed 's/^/B cancel: /' | tee -a "$log"
n0=$(wc -l < analysis/remote_stderr.txt)
cmdq 'cross 250\nwait 500\n'; sleep 3
dshot "${tag}_B_dialog"; shot "${tag}_B_dialog_window"
cmdq 'ime-cancel\n'; sleep 3
shot "${tag}_B_after"
m2v_lines "$n0" | sed 's/^/B open+cancel: /' | tee -a "$log"
cmdq 'down 250\nwait 800\n'; sleep 2; shot "${tag}_B_input"
note "saves after B: $(saves_ls | tr '\n' ' ')"
cp analysis/remote_stderr.txt "analysis/m2_${tag}_B_stderr.txt"
taskkill //f //im lbp3.exe >/dev/null 2>&1; sleep 2
saves_return
grep -h '\[stub\] libSce\(Ime\|Jpeg\|MsgDialog\|SaveDataDialog\|CommonDialog\)\|Rejecting invalid S#\|stuck\|device lost' "analysis/m2_${tag}_A_stderr.txt" "analysis/m2_${tag}_B_stderr.txt" | sort | uniq -c | sed 's/^/warnings: /' | tee -a "$log"
