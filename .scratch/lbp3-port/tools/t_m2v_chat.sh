#!/bin/sh
# Milestone 2 integration, Text Chat (the route that soft-locked on the stub build):
# Edit Now -> Popit -> left (Personal) -> down (Text Chat), "ime hello" queued before the Cross.
# Screenshots every second after the confirm (a speech bubble may show briefly). Then a second
# Text Chat with nothing queued: screen captures with the dialog over the game, then ime-cancel.
# Then input checks: Popit navigation, pause menu (up moves the highlight), Sackboy moves.
#   t_m2v_chat.sh <tag> [NAME=value ...]      (CHAT="..." overrides the text)
cd /d/Recomps/lbp3-recompiled || exit 1
tag=$1; shift
TEXT=${CHAT:-hello}
. /d/Recomps/lbp3-pc/.scratch/lbp3-port/tools/saves.sh; saves_keep; saves_baseline || exit 1
. /d/Recomps/lbp3-pc/.scratch/lbp3-port/tools/m2_lib.sh
launch "$@"; sleep 28; cmdq 'cross\nwait 2000\nsquare\nwait 2500\n'; sleep 6
cmdq 'square 200\n'; sleep 25; shot "${tag}_level"
cmdq 'square 250\nwait 1500\nleft 250\nwait 600\ndown 250\nwait 800\n'; sleep 4.5; shot "${tag}_textchat"
n0=$(wc -l < analysis/remote_stderr.txt)
cmdq "ime $TEXT\ncross 250\nwait 500\n"
i=1; while [ $i -le 6 ]; do sleep 1; shot "${tag}_after$i"; i=$((i + 1)); done
m2v_lines "$n0" | sed "s/^/$tag chat: /"
# Second Text Chat with nothing queued: is the Popit back on its menu, and the dialog over the game
n0=$(wc -l < analysis/remote_stderr.txt)
cmdq 'square 250\nwait 1500\n'; sleep 2.5; shot "${tag}_popit2"
cmdq 'left 250\nwait 600\ndown 250\nwait 800\ncross 250\nwait 500\n'; sleep 4
dshot "${tag}_dialog"
cmdq 'ime-cancel\n'; sleep 3; shot "${tag}_cancelled"
m2v_lines "$n0" | sed "s/^/$tag second: /"
# Input after the dialogs
cmdq 'right 250\nwait 800\n'; sleep 2; shot "${tag}_popit_right"
cmdq 'circle 250\nwait 800\nsquare 250\nwait 1000\n'; sleep 3; shot "${tag}_closed"
cmdq 'options 250\nwait 1500\nup 250\nwait 800\n'; sleep 3; shot "${tag}_pause_up"
cmdq 'options 250\nwait 1200\nlstick 1 0 1200\nwait 800\n'; sleep 4; shot "${tag}_moved"
cp analysis/remote_stderr.txt "analysis/m2_${tag}_stderr.txt"
taskkill //f //im lbp3.exe >/dev/null 2>&1; sleep 2
saves_return
grep -h 'Rejecting invalid S#\|stuck\|device lost' "analysis/m2_${tag}_stderr.txt" | sort | uniq -c | sed "s/^/$tag warnings: /"
