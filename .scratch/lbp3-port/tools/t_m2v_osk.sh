#!/bin/sh
# Milestone 2, the on-screen keyboard (fix/ime-osk) in the game: the level name typed with the
# pad only, on a build whose libSceImeDialog draws the keyboard over the game window. Route as
# t_m2v_name.sh: Create pause menu -> Edit Details -> the name field. One launch on the baseline
# save (analysis/savedata_thruster_on, restored; the human's saves are kept and put back).
#   1  Cross on the field: the keyboard comes up (the log says "on the on-screen keyboard");
#      screenshot. Square clears the prefill, one press per byte of it plus two (assumes the
#      text cursor starts at the end, as on a PS4). The name "osk ok" is typed with d-pad moves
#      and Cross, ASSUMING THE SELECTION STARTS ON 'q' (row 1, col 0 of the Latin-lower page,
#      shadPS4's kLatinLowerKeys: row 0 digits, row 1 qwertyuiop, row 2 asdfghjkl', row 3
#      zxcvbnm,.?), and Triangle for the space; R2 confirms. Pass: [ime] dialog confirmed: "osk ok".
#   2  Cross on the field again: the keyboard's prefill is the new name; Circle cancels
#      ([ime] dialog cancelled). Cross a third time: the name is unchanged and the menu is still
#      on the field (the Circle that cancelled didn't reach the game); Circle again.
# Every press is held 100 ms with 80 ms between presses, below any key-repeat delay.
#
#   t_m2v_osk.sh <tag> [NAME=value ...]      (NAME=value: environment for the game)
#   OSK_EXE=<lbp3.exe>   the build to run; default the clone's build/lbp3.exe. Another
#                        worktree's build runs in the clone's folder (dump, saves, analysis/),
#                        with that build's own SDL3.dll beside it, e.g.
#                        OSK_EXE=D:/Recomps/lbp3-wt/osk/build/lbp3.exe t_m2v_osk.sh osk1
cd /d/Recomps/lbp3-recompiled || exit 1
tag=$1; shift
EXE=${OSK_EXE:-build/lbp3.exe}
[ -f "$EXE" ] || { echo "no game build at $EXE"; exit 1; }
NAME="osk ok"
. /d/Recomps/lbp3-recompiled/.scratch/lbp3-port/tools/saves.sh; saves_keep; saves_baseline || exit 1
. /d/Recomps/lbp3-recompiled/.scratch/lbp3-port/tools/m2_lib.sh
log="analysis/m2_${tag}_out.txt"; : > "$log"
note() { echo "$*"; echo "$*" >> "$log"; }
fails=0
fail() { note "FAIL: $*"; fails=$((fails + 1)); }

# As tools/remote_start.ps1, but with any lbp3.exe (paths in mixed form: no backslashes).
launch_osk() {
  taskkill //f //im lbp3.exe >/dev/null 2>&1; sleep 1; rm -f analysis/input_cmd.txt
  exe_m=$(cygpath -m "$(realpath "$EXE")"); root_m=$(cygpath -m "$PWD")
  disp=$(head -n 1 analysis/test_display.txt 2>/dev/null | tr -d '\r ')
  env LBP3_REMOTE_INPUT=1 LBP3_OSK_PLAIN="${LBP3_OSK_PLAIN:-1}" SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT="${SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT:-0x0000/0x0000}" LBP3_DISPLAY="${LBP3_DISPLAY:-$disp}" "$@" powershell -NoProfile -Command \
    "Start-Process -FilePath '$exe_m' -ArgumentList '--run','--skip-intro' -WorkingDirectory '$root_m' -RedirectStandardOutput '$root_m/analysis/remote_stdout.txt' -RedirectStandardError '$root_m/analysis/remote_stderr.txt'" >/dev/null
}
# wait_after <line> <regex> <seconds>: a stderr line matching <regex> after line <line>.
wait_after() {
  n=0; while ! tail -n +"$(($1 + 1))" analysis/remote_stderr.txt | grep -Eq "$2"; do
    [ $n -ge "$3" ] && return 1; sleep 1; n=$((n + 1)); done
}
# press <button> [count]: queue presses into $seq; go sends them.
seq=""
press() { k=${2:-1}; while [ "$k" -gt 0 ]; do seq="${seq}$1 100\nwait 80\n"; k=$((k - 1)); done; }
go() { cmdq "$seq"; seq=""; }
to_name_field() {  # pause menu -> Edit Details -> the name field highlighted
  cmdq 'options 250\nwait 1500\nup 250\nwait 600\nup 250\nwait 600\nup 250\nwait 800\ncross 250\nwait 1500\nright 250\nwait 800\n'; sleep 7
}
# open_field <line>: Cross on the name field; waits for the dialog; prints its prefill.
open_field() {
  cmdq 'cross 250\nwait 500\n'
  wait_after "$1" '^\[ime\] dialog opened' 10 || return 1
  tail -n +"$(($1 + 1))" analysis/remote_stderr.txt | grep -m1 '^\[ime\] dialog opened' |
    sed -n 's/.*, text "\(.*\)" (type [0-9]*, options .*/\1/p'
}

note "build: $EXE"
note "saves at start: $(saves_ls | tr '\n' ' ')"
launch_osk "$@"; sleep 28
grep -m1 'LBP3_DISPLAY' analysis/remote_stderr.txt | sed 's/^/display: /' | tee -a "$log"
cmdq 'cross\nwait 2000\nsquare\nwait 2500\n'; sleep 6; shot "${tag}_home"
cmdq 'square 200\n'; sleep 25; shot "${tag}_level"
to_name_field; shot "${tag}_field"

# --- 1: type the name with the pad
n0=$(wc -l < analysis/remote_stderr.txt)
prefill=$(open_field "$n0") || fail "1: no text-entry dialog opened"
sleep 1
tail -n +"$((n0 + 1))" analysis/remote_stderr.txt | grep -q 'on the on-screen keyboard' ||
  fail "1: the dialog didn't open on the on-screen keyboard"
note "1: prefill \"$prefill\""
shot "${tag}_1_keyboard"
press square $(( $(printf '%s' "$prefill" | wc -c) + 2 )); go
# o (row 1, col 8) from q (1, 0)
press right 8; press cross
# s (2, 1): down to (2, 8), left 7
press down; press left 7; press cross
# k (2, 7): right 6
press right 6; press cross
press triangle
# o (1, 8): up to (1, 7), right 1
press up; press right; press cross
# k (2, 7): down to (2, 8), left 1
press down; press left; press cross
go
sleep 8; shot "${tag}_1_typed"
cmdq 'r2 150\nwait 500\n'
if wait_after "$n0" "^\[ime\] dialog confirmed: \"$NAME\"" 10; then note "1: PASS: confirmed \"$NAME\""
else fail "1: no [ime] dialog confirmed: \"$NAME\""; fi
sleep 2; shot "${tag}_1_named"
tail -n +"$((n0 + 1))" analysis/remote_stderr.txt | grep -q 'bigfart' && note "1: the level save (bigfart) was written after the confirm"

# --- 2: Circle cancels and keeps the name
n0=$(wc -l < analysis/remote_stderr.txt)
prefill=$(open_field "$n0") || fail "2: no text-entry dialog opened"
[ "$prefill" = "$NAME" ] && note "2: the field kept the name" || fail "2: prefill \"$prefill\", expected \"$NAME\""
sleep 1; shot "${tag}_2_keyboard"
cmdq 'circle 150\nwait 500\n'
if wait_after "$n0" '^\[ime\] dialog cancelled' 10; then note "2: PASS: Circle cancelled"
else fail "2: no [ime] dialog cancelled after Circle"; fi
sleep 2; shot "${tag}_2_cancelled"
n0=$(wc -l < analysis/remote_stderr.txt)
prefill=$(open_field "$n0") || fail "2: Cross on the field opened nothing (did Circle back out of the menu?)"
[ "$prefill" = "$NAME" ] && note "2: PASS: name unchanged after the cancel, menu still on the field" ||
  fail "2: prefill \"$prefill\" after the cancel, expected \"$NAME\""
cmdq 'circle 150\nwait 500\n'; wait_after "$n0" '^\[ime\] dialog cancelled' 10 || fail "2: the last Circle didn't cancel"
sleep 2
m2v_lines 0 | grep -E '^\[(ime|modal)\]' | sed 's/^/log: /' >> "$log"
cmdq 'options 250\nwait 1500\n'; sleep 3; shot "${tag}_closed"
save_settle 10
note "saves after: $(saves_ls | tr '\n' ' ')"
cp analysis/remote_stderr.txt "analysis/m2_${tag}_stderr.txt"
taskkill //f //im lbp3.exe >/dev/null 2>&1; sleep 2
saves_return
grep -h 'Rejecting invalid S#\|stuck\|device lost' "analysis/m2_${tag}_stderr.txt" | sort | uniq -c | sed 's/^/warnings: /' | tee -a "$log"
[ "$fails" -eq 0 ] && note "t_m2v_osk: PASS" || note "t_m2v_osk: $fails failure(s)"
[ "$fails" -eq 0 ]
