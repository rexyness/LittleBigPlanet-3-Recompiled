#!/bin/sh
# Milestone 2 route to libSceJpegEnc (and a look for libSceJpegDec), from the baseline level in
# Create mode:
#   1  square (Popit) -> right (Stickers & Decorations) -> cross -> right (Functions: "Take A
#      Photo") -> cross (USE!) -> cross (Take Photo). The game asks sceJpegEncQueryMemorySize, then
#      writes the profile (LBPxSAVE/bigfart*) and shows My Pictures with a black "A Photo" tile.
#   2  (unless PHASE2=0) quit, relaunch on the same saves, Edit Now, open Stickers & Decorations
#      again and page through it with R1 (Stickers, Decorations, PlayStation Camera, My Pictures
#      with the black photo tile, Community Photos): any libSceJpegDec call would show here (none
#      on the stub build, 2026-11-10).
#   t_m2_photo.sh <tag> [NAME=value ...]
cd /d/Recomps/lbp3-recompiled || exit 1
tag=$1; shift
. /d/Recomps/lbp3-pc/.scratch/lbp3-port/tools/saves.sh; saves_keep; saves_baseline || exit 1
. /d/Recomps/lbp3-pc/.scratch/lbp3-port/tools/m2_lib.sh
edit_now() {
  launch "$@"; sleep 28
  cmdq 'cross\nwait 2000\nsquare\nwait 2500\n'; sleep 6
  cmdq 'square 200\n'; sleep 25
}
m2() { tail -n +"$(($1 + 1))" analysis/remote_stderr.txt | grep -E "^\[stub\] ($M2_LIBS) ::" | grep -v sceImeDialogGetStatus | sed "s/^/$tag $2: /"; }
echo "$tag: saves before: $(saves_ls | tr '\n' ' ')"
edit_now "$@"; shot "${tag}_level"
n0=$(wc -l < analysis/remote_stderr.txt)
cmdq 'square 250\nwait 1500\nright 250\nwait 700\ncross 250\nwait 1500\nright 250\nwait 800\n'
sleep 6; shot "${tag}_tool"
cmdq 'cross 250\nwait 1500\n'; sleep 3; shot "${tag}_frame"
cmdq 'cross 250\nwait 1500\n'; sleep 2; shot "${tag}_flash"
sleep 4; shot "${tag}_pictures"
save_settle 5
m2 "$n0" 1
echo "$tag: saves after the photo: $(saves_ls | tr '\n' ' ')"
cp analysis/remote_stderr.txt "analysis/m2_${tag}_1_stderr.txt"
if [ "${PHASE2:-1}" = 1 ]; then
  taskkill //f //im lbp3.exe >/dev/null 2>&1; sleep 2
  edit_now "$@"
  n0=$(wc -l < analysis/remote_stderr.txt)
  cmdq 'square 250\nwait 1500\nright 250\nwait 700\ncross 250\nwait 1500\n'; sleep 5; shot "${tag}_2_stickers"
  i=1; while [ $i -le 4 ]; do cmdq 'r1 250\nwait 1200\n'; sleep 2; shot "${tag}_2_page$i"; i=$((i + 1)); done
  m2 "$n0" 2
  cp analysis/remote_stderr.txt "analysis/m2_${tag}_2_stderr.txt"
fi
taskkill //f //im lbp3.exe >/dev/null 2>&1; sleep 2
saves_return
