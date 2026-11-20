#!/bin/sh
# Milestone 2 integration, libSceSaveDataDialog by the level backup (export) and import, on a
# fresh launch (baseline savedata_thruster_on; the backups exist only in the test saves).
#   1  Home -> Create -> My Moon (the level is selected) -> square (More!) -> the level page ->
#      down, right x3 (Back up, checked by colour) -> Cross: the save list. Cross on
#      "[New saved data]": a new CUSA00473x00LEVEL... folder.
#   2  Back up again -> the list opens on the backup -> Cross -> "overwrite?" (No focused) ->
#      Circle: nothing written.
#   3  Circle back to the moon -> right (a crater of empty slots) -> Cross -> square (More!) ->
#      down (Import, checked by colour) -> Cross: the load list -> Circle: nothing loaded.
# With YES=1 the accepting answers instead: 2 moves the focus to Yes (d-pad left) and confirms
# (the backup is rewritten), 3 picks the backup with Cross (the level is imported into the empty
# slot: save files change; m2_<tag>_3_moon.png shows the slot).
# Pictures of each dialog over the game: m2_<tag>_*_dialog.png (capture_dialog.ps1).
# The dialogs are drawn over the game with ImGui (fix/dialog-overlay; "[dialog] savedata: on the
# overlay"), and the same presses answer them: Cross presses the focused button (a list's entry),
# Circle cancels, d-pad left focuses Yes. Pass LBP3_DIALOGS=window to run the native windows
# instead ("... on a window"). The last lines count which front end each dialog opened on.
#   t_m2v_backup.sh <tag> [NAME=value ...]
cd /d/Recomps/lbp3-recompiled || exit 1
tag=$1; shift
. /d/Recomps/lbp3-recompiled/.scratch/lbp3-port/tools/saves.sh; saves_keep; saves_baseline || exit 1
. /d/Recomps/lbp3-recompiled/.scratch/lbp3-port/tools/m2_lib.sh
log="analysis/m2_${tag}_out.txt"; : > "$log"
note() { echo "$*"; echo "$*" >> "$log"; }
S=user/savedata/CUSA00473
green() {  # green <shot> <x0> <y0> <x1> <y1>: is that button highlighted (green, not blue)?
  python -c "
import sys
from PIL import Image, ImageStat
im = Image.open(sys.argv[1]).convert('RGB')
r, g, b = ImageStat.Stat(im.crop(tuple(int(v) for v in sys.argv[2:6]))).mean
sys.exit(0 if g > b + 30 else 1)" "analysis/screenshots/m2_$1.png" "$2" "$3" "$4" "$5"
}
step() { n0=$(wc -l < analysis/remote_stderr.txt); cmdq "$1"; sleep "$2"; }
lines() { m2v_lines "$n0" | sed "s/^/$1: /" | tee -a "$log"; }
backup_highlighted() {  # make sure the level page's Back up button is the highlighted one
  shot "${tag}_$1_btn"
  green "${tag}_$1_btn" 725 308 830 345 && return 0
  cmdq 'left 250\nwait 800\n'; sleep 2; shot "${tag}_$1_btn"
  green "${tag}_$1_btn" 725 308 830 345 && return 0
  cmdq 'right 250\nwait 800\nright 250\nwait 800\n'; sleep 3; shot "${tag}_$1_btn"
  green "${tag}_$1_btn" 725 308 830 345
}
note "saves at start: $(saves_ls | tr '\n' ' ')"
launch "$@"; sleep 28; cmdq 'cross\nwait 2000\nsquare\nwait 2500\n'; sleep 6
note "display: $(grep -m1 -o 'display refresh [0-9.]* Hz' analysis/remote_stderr.txt)"
cmdq 'cross 300\nwait 3000\n'; sleep 4; shot "${tag}_create"
cmdq 'cross 300\nwait 3000\n'; sleep 8; shot "${tag}_moon"
cmdq 'square 300\nwait 1500\n'; sleep 3; shot "${tag}_page"
cmdq 'down 250\nwait 600\nright 250\nwait 600\nright 250\nwait 600\nright 250\nwait 1000\n'; sleep 4.5
# 1: export to a new backup
if backup_highlighted 1; then
  step 'cross 250\nwait 800\n' 4; dshot "${tag}_1_list_dialog"; lines "1 list"
  step 'cross 250\nwait 800\n' 8; lines "1 new"
  note "1: backup folders now: $(ls -d $S/CUSA00473x00LEVEL* 2>/dev/null | xargs -n1 basename | tr '\n' ' ')"
else note "1: Back up not highlighted; export skipped"; fi
bk=$(ls -d $S/CUSA00473x00LEVEL* 2>/dev/null | head -1)
# 2: export again onto it, answered No
if [ -n "$bk" ] && backup_highlighted 2; then
  b1=$(find "$bk" -printf '%P %s %T@\n' | sort; md5sum < "$bk/L0")
  step 'cross 250\nwait 800\n' 4; dshot "${tag}_2_list_dialog"; lines "2 list"
  step 'cross 250\nwait 800\n' 3; dshot "${tag}_2_overwrite_dialog"; lines "2 pick"
  if [ "${YES:-0}" = 1 ]; then
    step 'left 250\nwait 600\n' 2; dshot "${tag}_2_yes_focused"
    step 'cross 250\nwait 800\n' 10; lines "2 yes"; save_settle 5
    b2=$(find "$bk" -printf '%P %s %T@\n' | sort; md5sum < "$bk/L0")
    [ "$b1" != "$b2" ] && note "2: the backup was rewritten" || note "2: WARNING: the backup is unchanged after Yes"
  else
    step 'circle 250\nwait 800\n' 6; lines "2 no"
    b2=$(find "$bk" -printf '%P %s %T@\n' | sort; md5sum < "$bk/L0")
    [ "$b1" = "$b2" ] && note "2: the backup is unchanged" || note "2: WARNING: the backup changed"
  fi
else note "2: skipped (no backup, or Back up not highlighted)"; fi
# 3: import, cancelled
cmdq 'circle 300\nwait 1500\n'; sleep 3
cmdq 'right 300\nwait 1200\n'; sleep 2.5
cmdq 'cross 300\nwait 2000\n'; sleep 4
cmdq 'square 300\nwait 1500\n'; sleep 3
cmdq 'down 250\nwait 800\n'; sleep 2; shot "${tag}_3_btn"
if green "${tag}_3_btn" 580 238 715 278; then
  s1=$(saves_ls)
  step 'cross 250\nwait 800\n' 4; dshot "${tag}_3_list_dialog"; lines "3 list"
  if [ "${YES:-0}" = 1 ]; then
    step 'cross 250\nwait 800\n' 15; lines "3 import"; shot "${tag}_3_after"; save_settle 8
    note "3: save files after the import: $(saves_ls | tr '\n' ' ')"
    [ "$s1" != "$(saves_ls)" ] && note "3: save files changed (the import was saved)" || note "3: WARNING: no save file changed after the import"
    cmdq 'circle 300\nwait 1500\n'; sleep 3; shot "${tag}_3_moon"
  else
    step 'circle 250\nwait 800\n' 6; lines "3 cancel"; shot "${tag}_3_after"
    [ "$s1" = "$(saves_ls)" ] && note "3: no save file changed" || note "3: WARNING: save files changed"
  fi
else note "3: Import not highlighted; import skipped"; fi
note "saves at the end: $(saves_ls | tr '\n' ' ')"
cp analysis/remote_stderr.txt "analysis/m2_${tag}_stderr.txt"
taskkill //f //im lbp3.exe >/dev/null 2>&1; sleep 2
saves_return
grep -h "couldn't create\|refused\|the overlay didn't take\|Rejecting invalid S#\|stuck\|device lost" "analysis/m2_${tag}_stderr.txt" | sort | uniq -c | sed "s/^/$tag warnings: /" | tee -a "$log"
ov=$(grep -c '^\[dialog\] [a-z]*: on the overlay' "analysis/m2_${tag}_stderr.txt")
wn=$(grep -c '^\[dialog\] [a-z]*: on a window' "analysis/m2_${tag}_stderr.txt")
note "$tag front end: $ov dialog(s) on the overlay, $wn on a window"
case " $* " in
  *" LBP3_DIALOGS=window "*) [ "$ov" = 0 ] || note "$tag WARNING: LBP3_DIALOGS=window, yet $ov dialog(s) opened on the overlay" ;;
  *) [ "$wn" = 0 ] || note "$tag WARNING: $wn dialog(s) opened on a window although the game window was up" ;;
esac
