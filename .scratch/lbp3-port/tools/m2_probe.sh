#!/bin/sh
# m2_probe.sh <name> <wait s> '<input lines>': one step of a hand-driven route search. Sends the
# input, waits, takes m2_<name>.png and prints the new stderr lines of the milestone 2 libraries:
# [stub] lines (stub builds; sceImeDialogGetStatus and sceCommonDialogInitialize are left out,
# every run calls them) and [ime]/[jpeg]/[dialog]/[modal] lines (builds with the libraries).
cd /d/Recomps/lbp3-recompiled || exit 1
. /d/Recomps/lbp3-pc/.scratch/lbp3-port/tools/m2_lib.sh
n0=$(wc -l < analysis/remote_stderr.txt)
cmdq "$3"; sleep "$2"
shot "$1"
tail -n +"$((n0 + 1))" analysis/remote_stderr.txt | grep -E "^\[stub\] ($M2_LIBS) ::|^\[(ime|jpeg|dialog|modal)\]" | grep -v 'sceImeDialogGetStatus\|sceCommonDialogInitialize' | sed "s/^/[$1] /"
echo "[$1] stderr lines $n0 -> $(wc -l < analysis/remote_stderr.txt)"
