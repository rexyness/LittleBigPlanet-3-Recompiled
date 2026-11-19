#!/bin/sh
# Milestone 2 integration, boot check: the eight libraries are bound (not in stdout's "missing"
# list), none logs a [stub] line, libSceIme opens the keyboard once, and the game reaches the Pod
# and Home as before.
#   t_m2v_boot.sh <tag> [reference stdout] [NAME=value ...]
cd /d/Recomps/lbp3-recompiled || exit 1
tag=$1; ref=${2:-analysis/m2v_port_stdout_ref.txt}; shift 2
. /d/Recomps/lbp3-pc/.scratch/lbp3-port/tools/saves.sh; saves_keep; saves_baseline || exit 1
. /d/Recomps/lbp3-pc/.scratch/lbp3-port/tools/m2_lib.sh
launch "$@"; sleep 28
shot "${tag}_pod"
cmdq 'cross\nwait 2000\nsquare\nwait 2500\n'; sleep 6
shot "${tag}_home"
cp analysis/remote_stdout.txt "analysis/m2_${tag}_stdout.txt"
cp analysis/remote_stderr.txt "analysis/m2_${tag}_stderr.txt"
taskkill //f //im lbp3.exe >/dev/null 2>&1; sleep 2
saves_return
L='libSceIme |libSceImeDialog|libSceJpegDec|libSceJpegEnc|libSceMsgDialog|libSceSaveDataDialog|libSceCommonDialog|libSceGameCustomDataDialog'
echo "== imports line: reference / now"
grep -h '^imports:' "$ref" "analysis/m2_${tag}_stdout.txt"
grep -h '^missing' "$ref" "analysis/m2_${tag}_stdout.txt"
echo "== the eight libraries in the missing list: reference / now"
grep -E "^  ($L)" "$ref" | awk '{print "  ref: " $1, $2}'
grep -E "^  ($L)" "analysis/m2_${tag}_stdout.txt" | awk '{print "  NOW: " $1, $2}'
echo "== missing-list libraries that differ"
diff <(grep '^  ' "$ref" | awk '{print $1, $2}') <(grep '^  ' "analysis/m2_${tag}_stdout.txt" | awk '{print $1, $2}')
echo "== [stub] lines from the eight libraries (expect none)"
grep -E "^\[stub\] (libSceIme|libSceImeDialog|libSceJpegDec|libSceJpegEnc|libSceMsgDialog|libSceSaveDataDialog|libSceCommonDialog|libSceGameCustomDataDialog) ::" "analysis/m2_${tag}_stderr.txt" | sort | uniq -c
echo "== [ime] / [jpeg] / [dialog] / [modal] lines"
grep -E '^\[(ime|jpeg|dialog|modal)\]' "analysis/m2_${tag}_stderr.txt" | sort | uniq -c
echo "== GPU warnings"
grep -c 'Rejecting invalid S#\|stuck\|device lost' "analysis/m2_${tag}_stderr.txt"
