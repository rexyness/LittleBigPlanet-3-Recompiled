#!/bin/sh
# Milestone 2 integration, photo (libSceJpegEnc) on a build with the real library:
#   1  Edit Now, Popit -> Stickers & Decorations -> Take A Photo -> USE! -> Cross (a self-timer
#      may count down first). Expect "[jpeg] encoded 640x360 BGRA, ratio 70 (quality 73) -> N bytes"
#      and a "[video] ... 3 read back" status line (the capture targets written back for the CPU).
#      Screenshots: the scene in the photo frame (frame), My Pictures with the new tile
#      (pictures), then USE! on the photo: the picture sticker on the cursor (sticker).
#   2  (unless PHASE2=0) relaunch on the same saves, Edit Now, Stickers & Decorations, R1 x3
#      (My Pictures): the photo is still there. Any "[jpeg] decoded" line is reported.
# Checks (PASS/FAIL lines at the end): the encoder line, the capture readback, and photo_check.py on
# the test copy of the saves (a JPEG of at least 10000 bytes that isn't black; the black frame of
# the readback bug was 4291 bytes).
#   t_m2v_photo.sh <tag> [NAME=value ...]
cd /d/Recomps/lbp3-recompiled || exit 1
tag=$1; shift
. /d/Recomps/lbp3-pc/.scratch/lbp3-port/tools/saves.sh; saves_keep; saves_baseline || exit 1
. /d/Recomps/lbp3-pc/.scratch/lbp3-port/tools/m2_lib.sh
edit_now() { launch "$@"; sleep 28; cmdq 'cross\nwait 2000\nsquare\nwait 2500\n'; sleep 6; cmdq 'square 200\n'; sleep 25; }
echo "$tag: saves before: $(saves_ls | tr '\n' ' ')"
edit_now "$@"; shot "${tag}_level"
n0=$(wc -l < analysis/remote_stderr.txt)
cmdq 'square 250\nwait 1500\nright 250\nwait 700\ncross 250\nwait 1500\nright 250\nwait 800\n'; sleep 6; shot "${tag}_tool"
cmdq 'cross 250\nwait 1500\n'; sleep 3; shot "${tag}_frame"
cmdq 'cross 250\nwait 1500\n'; sleep 2; shot "${tag}_flash"
sleep 5; shot "${tag}_pictures"
cmdq 'cross 250\nwait 1500\n'; sleep 3; shot "${tag}_sticker"
save_settle 5
m2v_lines "$n0" | sed "s/^/$tag 1: /"
echo "$tag: saves after the photo: $(saves_ls | tr '\n' ' ')"
cp analysis/remote_stderr.txt "analysis/m2_${tag}_1_stderr.txt"
if [ "${PHASE2:-1}" = 1 ]; then
  taskkill //f //im lbp3.exe >/dev/null 2>&1; sleep 2
  edit_now "$@"
  n0=$(wc -l < analysis/remote_stderr.txt)
  cmdq 'square 250\nwait 1500\nright 250\nwait 700\ncross 250\nwait 1500\n'; sleep 5
  cmdq 'r1 250\nwait 1000\nr1 250\nwait 1000\nr1 250\nwait 1200\n'; sleep 5; shot "${tag}_2_pictures"
  m2v_lines "$n0" | sed "s/^/$tag 2: /"
  cp analysis/remote_stderr.txt "analysis/m2_${tag}_2_stderr.txt"
fi
taskkill //f //im lbp3.exe >/dev/null 2>&1; sleep 2
rm -rf "analysis/m2_${tag}_testsaves"; cp -r user/savedata/CUSA00473 "analysis/m2_${tag}_testsaves"  # the test copy, for a look at the photo
saves_return
grep -h 'Rejecting invalid S#\|stuck\|device lost\|\[jpeg\] decoded\|capture job code differs' analysis/m2_${tag}_?_stderr.txt | sort | uniq -c | sed "s/^/$tag warnings: /"
v=FAIL; grep -q '^\[jpeg\] encoded 640x360 BGRA, ratio 70 (quality 73)' "analysis/m2_${tag}_1_stderr.txt" && v=PASS; echo "$tag check encoder line: $v"
v=FAIL; grep -q ' 3 read back' "analysis/m2_${tag}_1_stderr.txt" && v=PASS; echo "$tag check capture readback: $v"
python /d/Recomps/lbp3-pc/.scratch/lbp3-port/tools/photo_check.py "analysis/m2_${tag}_testsaves" "analysis/screenshots/m2_${tag}_photo" | sed "s/^/$tag /"
