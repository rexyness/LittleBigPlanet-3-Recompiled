#!/bin/sh
# Gap 17 (a): an edited level survives Save Changes, a quit and a relaunch; an unsaved edit doesn't.
# Three launches on one save folder, restored from the baseline only before the first:
#   A  Edit Now, move the Thruster next to the entrance, quit (kill) without saving.
#   B  Edit Now (the Thruster must be back where it was), move it again, close the Popit,
#      Change to Play Mode -> Save Changes, wait until the save files stop changing, quit (kill).
#   C  Edit Now: the Thruster must be next to the entrance. Then Change to Play Mode (Discard).
# Screenshots m2_<tag>_<step>.png. A's and B's start views must match (the unsaved move is gone).
# C's start view comes from another camera than the baseline's (the same in runs e1 and e3; why is
# not known), so judge the move by the Thruster's distance from the entrance in entrance diameters
# (3.7 in the baseline, 2.8 just before the save, 2.9 after the reload), and by B's play view
# right after the save (B_saved). C's play view is optional: its Discard press once left the game
# in Create (e4).
# Save files are listed in analysis/m2_<tag>_saves.txt after each step. Taking Save Changes with
# the Popit menu still open (e3, a lost press) left a white screen for 25 s or more, with 17
# "Rejecting invalid S#" warnings, though the save went through; so the presses here are 250 ms.
#   t17_edit.sh <tag> [NAME=value ...]
cd /d/Recomps/lbp3-recompiled || exit 1
tag=$1; shift
. /d/Recomps/lbp3-recompiled/.scratch/lbp3-port/tools/saves.sh; saves_keep; saves_baseline || exit 1
. /d/Recomps/lbp3-recompiled/.scratch/lbp3-port/tools/m2_lib.sh
log="analysis/m2_${tag}_saves.txt"; : > "$log"
note() { echo "$*"; echo "$*" >> "$log"; }
listing() { note "-- saves $1 ($(date +%H:%M:%S))"; saves_ls >> "$log"; }
open_level() {  # Pod -> Home (opens on Create) -> Edit Now; screenshots home and level
  launch "$@"; sleep 28
  cmdq 'cross\nwait 2000\nsquare\nwait 2500\n'; sleep 6
  shot "${tag}_${S}_home"
  cmdq 'square 100\n'; sleep 25
  shot "${tag}_${S}_level"
}
move_thruster() {  # Popit -> Popit Cursor -> onto the Thruster -> grab, drag left 1.5 s, drop
  cmdq 'square 100\nwait 1500\ncross 100\nwait 800\nlstick 0.8 0.5 700\nwait 500\nlstick 0.8 -0.6 700\nwait 500\nlstick 0.5 0.9 600\nwait 700\nlstick 0.1 0.9 350\nwait 700\nlstick 0.4 0.8 200\nwait 600\nlstick 0.3 0.9 250\nwait 800\n'
  sleep 11; shot "${tag}_${S}_onthruster"
  cmdq 'cross 100\nwait 1200\nlstick -1 0 1500\nwait 800\ncross 100\nwait 1000\n'; sleep 6
  shot "${tag}_${S}_moved"
  # Back to the Popit menu (circle) and close it (square): the camera returns to the Sackboy.
  cmdq 'circle 250\nwait 1000\nsquare 250\nwait 1500\n'; sleep 6
  shot "${tag}_${S}_edited"
}
listing "baseline"
S=A; open_level "$@"
move_thruster
listing "A: after the unsaved move"
cp analysis/remote_stderr.txt "analysis/m2_${tag}_A_stderr.txt"
taskkill //f //im lbp3.exe >/dev/null 2>&1; sleep 2
listing "A: after the kill (no save)"

S=B; open_level "$@"
move_thruster
cmdq 'options 250\nwait 1500\ncross 250\nwait 1500\n'; sleep 3.5
shot "${tag}_B_dialog"
# Save Changes. A 100 ms press was once lost on this dialog (frames of up to 176 ms around it),
# so the press is 300 ms and is repeated (up to twice) while the level save (LBPxSAVE/bigfart*) is
# unchanged; LBPxSAVExLITTLE is rewritten without any save, so it can't tell.
before=$(find user/savedata/CUSA00473/LBPxSAVE -type f -printf '%P %s %T@\n' | sort)
try=1
while :; do
  cmdq 'cross 300\n'; sleep 10
  [ "$(find user/savedata/CUSA00473/LBPxSAVE -type f -printf '%P %s %T@\n' | sort)" != "$before" ] && break
  [ $try -ge 3 ] && { note "B: WARNING: LBPxSAVE unchanged after 3 presses of Save Changes"; break; }
  note "B: LBPxSAVE unchanged 10 s after Save Changes press $try; pressing again"; try=$((try + 1))
done
shot "${tag}_B_saving"
save_settle 15
shot "${tag}_B_saved"
listing "B: after Save Changes (files quiet for 15 s)"
cp analysis/remote_stderr.txt "analysis/m2_${tag}_B_stderr.txt"
taskkill //f //im lbp3.exe >/dev/null 2>&1; sleep 2
listing "B: after the kill"

S=C; open_level "$@"
# The game asks to save or discard even with no change; Discard Changes (right) reaches play mode
# without writing the level again.
cmdq 'options 250\nwait 1500\ncross 250\nwait 1500\n'; sleep 3.5
shot "${tag}_C_dialog"
cmdq 'right 300\nwait 800\ncross 300\n'; sleep 20
shot "${tag}_C_play"
listing "C: in play mode"
cp analysis/remote_stderr.txt "analysis/m2_${tag}_C_stderr.txt"
taskkill //f //im lbp3.exe >/dev/null 2>&1; sleep 2
listing "C: after the kill"
saves_return
# The Thruster's spot: x 69-86 %, y 40-68 % of the window in the baseline start view.
python - "$tag" <<'EOF' | tee "analysis/m2_${tag}_cmp.txt"
import sys
from PIL import Image, ImageChops, ImageStat
t = sys.argv[1]
def crop(s, box=(0.69, 0.40, 0.86, 0.68)):
    im = Image.open(f"analysis/screenshots/m2_{t}_{s}.png").convert("RGB")
    w, h = im.size
    return im.crop((int(w * box[0]), int(h * box[1]), int(w * box[2]), int(h * box[3])))
d = lambda x, y: sum(ImageStat.Stat(ImageChops.difference(x, y)).mean) / 3
print(f"Thruster spot, mean abs difference (0-255): A_level-B_level {d(crop('A_level'), crop('B_level')):.1f}, "
      f"A_level-C_level {d(crop('A_level'), crop('C_level')):.1f}, B_edited-C_level {d(crop('B_edited'), crop('C_level')):.1f}")
full = (0, 0.1, 1, 0.88)
print(f"Whole view: B_edited-C_level {d(crop('B_edited', full), crop('C_level', full)):.1f}, "
      f"A_level-C_level {d(crop('A_level', full), crop('C_level', full)):.1f}, B_saved-C_play {d(crop('B_saved', full), crop('C_play', full)):.1f}")
EOF
grep -h '^\[stub\] libSce\(ImeDialog\|Jpeg\|MsgDialog\|SaveDataDialog\)' analysis/m2_${tag}_?_stderr.txt | sort | uniq -c
