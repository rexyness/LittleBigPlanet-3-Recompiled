#!/bin/sh
# Milestone 2 probe for libSceSaveDataDialog / libSceMsgDialog: boot on a damaged save. Only the
# test copy in user/savedata is damaged (after saves_baseline); the baselines and the human's
# saves are never touched. Not a remote-input-only route: the damage is done to the file first.
#   t_m2_dialogs.sh <tag> big|little|play|nobig [NAME=value ...]
# big/little/play: the newest LBPxSAVE bigfart* / LBPxSAVExLITTLE littlefart* /
# LBPxSAVEx12x349 playfart* is cut to half its size and its second quarter is overwritten with
# 0x5a bytes; nobig: LBPxSAVE/00000001 is emptied. Then boot, screenshots every 6 s for a minute
# (m2_<tag>_bNN.png), a cross every 12 s to get through any prompt, and the milestone 2 [stub]
# lines (sceImeDialogGetStatus and sceCommonDialogInitialize left out).
cd /d/Recomps/lbp3-recompiled || exit 1
tag=$1; what=$2; shift 2
. /d/Recomps/lbp3-pc/.scratch/lbp3-port/tools/saves.sh; saves_keep; saves_baseline || exit 1
. /d/Recomps/lbp3-pc/.scratch/lbp3-port/tools/m2_lib.sh
S=user/savedata/CUSA00473
damage() {
  f=$(ls "$S/$1/00000001/$2"* 2>/dev/null | sort -V | tail -1)
  [ -n "$f" ] || { echo "$tag: no $2 file"; exit 1; }
  n=$(stat -c %s "$f"); h=$((n / 2)); q=$((n / 4))
  head -c "$h" "$f" > "$f.tmp" && mv "$f.tmp" "$f"
  python -c "
import sys
p, q = sys.argv[1], int(sys.argv[2])
b = bytearray(open(p, 'rb').read()); b[q:2 * q] = b'\x5a' * q; open(p, 'wb').write(b)" "$f" "$q"
  echo "$tag: $f cut from $n to $(stat -c %s "$f") bytes, bytes $q-$((2 * q)) set to 0x5a"
}
case "$what" in
  big) damage LBPxSAVE bigfart ;;
  little) damage LBPxSAVExLITTLE littlefart ;;
  play) damage LBPxSAVEx12x349 playfart ;;
  nobig) rm -f "$S"/LBPxSAVE/00000001/*; echo "$tag: LBPxSAVE/00000001 emptied" ;;
  *) echo "usage: t_m2_dialogs.sh <tag> big|little|play|nobig [NAME=value ...]"; exit 2 ;;
esac
echo "$tag: saves at boot: $(saves_ls | tr '\n' ' ')"
launch "$@"
i=0
while [ $i -lt 11 ]; do
  sleep 6; shot "${tag}_b$(printf %02d $i)"
  [ $((i % 2)) -eq 1 ] && [ $i -ge 3 ] && cmdq 'cross 250\n'
  i=$((i + 1))
done
stubs | grep -v 'sceImeDialogGetStatus\|sceCommonDialogInitialize' | sed "s/^/$tag: /"
grep -h 'savedata\]' analysis/remote_stderr.txt | grep -iv 'mount\|umount\|set param\|save icon' | sort | uniq -c | sed "s/^/$tag: /"
echo "$tag: saves at the end: $(saves_ls | tr '\n' ' ')"
cp analysis/remote_stderr.txt "analysis/m2_${tag}_stderr.txt"
taskkill //f //im lbp3.exe >/dev/null 2>&1; sleep 2
saves_return
