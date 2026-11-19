#!/bin/sh
# Milestone 2 route to libSceImeDialog (text entry), from the baseline level in Create mode.
#   t_m2_ime.sh <tag> name|chat [NAME=value ...]
# name: options (Create pause menu) -> up x3 (Edit Details) -> cross -> right (the level name,
#       "SET THE NAME!") -> cross. Then down, circle (ignored while the dialog is "open"), and
#       options, which closes the page; the game calls sceImeDialogAbort and Create goes on.
# chat: square (Popit) -> left (Personal) -> down (Text Chat) -> cross. Then right, square (the
#       Popit stays on Text Chat), options (the pause menu opens) and up (ignored).
# Prints the milestone 2 [stub] lines logged after the route started (sceImeDialogGetStatus is
# polled every frame from boot, so it is left out); screenshots m2_<tag>_*.png.
cd /d/Recomps/lbp3-recompiled || exit 1
tag=$1; route=$2; shift 2
. /d/Recomps/lbp3-pc/.scratch/lbp3-port/tools/saves.sh; saves_keep; saves_baseline || exit 1
. /d/Recomps/lbp3-pc/.scratch/lbp3-port/tools/m2_lib.sh
launch "$@"; sleep 28
cmdq 'cross\nwait 2000\nsquare\nwait 2500\n'; sleep 6
cmdq 'square 200\n'; sleep 25            # Home opens on Create: Edit Now
shot "${tag}_level"
n0=$(wc -l < analysis/remote_stderr.txt)
case "$route" in
  name)
    cmdq 'options 250\nwait 1500\nup 250\nwait 600\nup 250\nwait 600\nup 250\nwait 800\ncross 250\nwait 1500\nright 250\nwait 800\n'
    sleep 7; shot "${tag}_field"
    cmdq 'cross 250\nwait 1500\n'; sleep 3; shot "${tag}_entered"
    cmdq 'down 250\nwait 800\ncircle 250\nwait 1200\n'; sleep 3; shot "${tag}_input"
    cmdq 'options 250\nwait 1500\n'; sleep 3; shot "${tag}_options"
    cmdq 'lstick 1 0 1200\nwait 800\n'; sleep 3; shot "${tag}_moved" ;;
  chat)
    cmdq 'square 250\nwait 1500\nleft 250\nwait 600\ndown 250\nwait 800\n'
    sleep 4.5; shot "${tag}_field"
    cmdq 'cross 250\nwait 1500\n'; sleep 3; shot "${tag}_entered"
    cmdq 'right 250\nwait 800\nsquare 250\nwait 1200\n'; sleep 3; shot "${tag}_input"
    cmdq 'options 250\nwait 1500\n'; sleep 3; shot "${tag}_options"
    cmdq 'up 250\nwait 800\n'; sleep 2; shot "${tag}_moved" ;;
  *) echo "usage: t_m2_ime.sh <tag> name|chat [NAME=value ...]"; exit 2 ;;
esac
tail -n +"$((n0 + 1))" analysis/remote_stderr.txt | grep -E "^\[stub\] ($M2_LIBS) ::" | grep -v sceImeDialogGetStatus | sed "s/^/$tag: /"
cp analysis/remote_stderr.txt "analysis/m2_${tag}_stderr.txt"
taskkill //f //im lbp3.exe >/dev/null 2>&1; sleep 2
saves_return
