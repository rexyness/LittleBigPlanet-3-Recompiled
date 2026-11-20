#!/bin/sh
# Ticket 13, local co-op in the game with virtual pads (LBP3_VIRTUAL_PADS), on the clone's build.
# Each case starts from the baseline save (analysis/savedata_thruster_on); the human's saves are
# kept and put back on exit (saves.sh).
#   A  one player: boot, Pod -> Home -> Play -> the Introduction, 40 s in the level.
#   B  two players (LBP3_VIRTUAL_PADS=2): user 2 logs in at boot; the same route; in the level
#      player 2 moves; then remote "pads 1" (player 2's pad gone) and "pads 2" (back) while the
#      game runs.
# PASS when: B logs user 2's login at boot, then a logout and a login; A mounts saves for user 1
# only; in B player 2 gets its own profile (the game mounts and creates LBPxSAVE and
# LBPxSAVExLITTLE for user 2: on a PS4 every user has their own saves), and those land in
# user\savedata\user2, never in player 1's folder: B leaves the same save files as A in player
# 1's folder (names; contents differ); neither run has a GPU warning; B still flips frames after
# the logout and login. (saves.sh clears and restores the user2..user4 folders around a run.)
#   t13_coop.sh <tag> [NAME=value ...]      (NAME=value: environment for the game, both cases)
cd /d/Recomps/lbp3-recompiled || exit 1
tag=$1; shift
. /d/Recomps/lbp3-recompiled/.scratch/lbp3-port/tools/saves.sh; saves_keep
. /d/Recomps/lbp3-recompiled/.scratch/lbp3-port/tools/m2_lib.sh
log="analysis/m2_${tag}_out.txt"; : > "$log"
note() { echo "$*"; echo "$*" >> "$log"; }
fails=0
fail() { note "FAIL: $*"; fails=$((fails + 1)); }
pass() { note "PASS: $*"; }

to_intro() {  # from the Pod with the boot's network message up, into the Introduction
  cmdq 'cross 200\nwait 1500\nsquare\nwait 2500\n'; sleep 6
  cmdq 'up 100\nwait 700\n'; sleep 1.5
  cmdq 'cross\nwait 3000\ncross\nwait 6500\ncross\nwait 4500\nleft 100\nwait 1500\ncross\nwait 1000\n'; sleep 45
}
save_names() { (cd user/savedata/CUSA00473 && find . -type f | sed 's#^\./##' | sed 's/[0-9][0-9]*$/N/' | sort -u); }
gpu_warnings() { grep -c 'Rejecting invalid S#\|stuck\|device lost' analysis/remote_stderr.txt; }

# ---- A: one player ----
saves_baseline || exit 1
launch "$@"; sleep 30
note "display: $(grep -m1 -o 'LBP3_DISPLAY=[^:]*: window on "[^"]*" ([0-9.]* Hz), one of [0-9] displays' analysis/remote_stderr.txt)"
to_intro; shot "${tag}_A_level"
taskkill //f //im lbp3.exe >/dev/null 2>&1; sleep 2
cp analysis/remote_stderr.txt "analysis/m2_${tag}_A_stderr.txt"
a_names=$(save_names)
a_users=$(grep -o '^\[savedata\] mount .* user [0-9]*' "analysis/m2_${tag}_A_stderr.txt" | grep -o 'user [0-9]*$' | sort | uniq -c | tr '\n' ' ')
note "A: save mounts by user: $a_users"
note "A: GPU warnings: $(gpu_warnings)"

# ---- B: two players ----
saves_baseline || exit 1
launch LBP3_VIRTUAL_PADS=2 "$@"; sleep 30
grep -q '^\[user\] user 2 logged in' analysis/remote_stderr.txt && pass "B: user 2 logged in at boot" || fail "B: no login of user 2 at boot"
shot "${tag}_B_pod"
to_intro; shot "${tag}_B_level"
cmdq 'p2 lstick 1 0 1500\nwait 300\np2 cross 200\nwait 1000\n'; sleep 4; shot "${tag}_B_level_p2"
n0=$(grep -c '' analysis/remote_stderr.txt)
cmdq 'pads 1\n'; sleep 5; shot "${tag}_B_p2_gone"
cmdq 'pads 2\n'; sleep 6; shot "${tag}_B_p2_back"
after=$(tail -n +"$((n0 + 1))" analysis/remote_stderr.txt)
echo "$after" | grep -q '^\[user\] user 2 logged out' && pass "B: user 2 logged out when the pad went" || fail "B: no logout of user 2"
echo "$after" | grep -q '^\[user\] user 2 logged in' && pass "B: user 2 logged in again" || fail "B: no second login of user 2"
f0=$(grep -c 'frames flipped' analysis/remote_stderr.txt); sleep 8; f1=$(grep -c 'frames flipped' analysis/remote_stderr.txt)
[ "$f1" -gt "$f0" ] && pass "B: still flipping frames after the logout and login ($f0 -> $f1 status lines)" || fail "B: no frames after the logout and login"
taskkill //f //im lbp3.exe >/dev/null 2>&1; sleep 2
cp analysis/remote_stderr.txt "analysis/m2_${tag}_B_stderr.txt"
b_names=$(save_names)
b_users=$(grep -o '^\[savedata\] mount .* user [0-9]*' "analysis/m2_${tag}_B_stderr.txt" | grep -o 'user [0-9]*$' | sort | uniq -c | tr '\n' ' ')
note "B: save mounts by user: $b_users"
note "B: GPU warnings: $(gpu_warnings)"

# ---- checks across A and B ----
echo "$a_users" | grep -q 'user [2-9]' && fail "A (one player) mounted saves for another user" || pass "A mounted saves for user 1 only"
echo "$b_users" | grep -q 'user 2' && pass "B: player 2 mounted its own saves" || fail "B: no save mount for player 2"
g2=""; [ -d user/savedata/user2/CUSA00473 ] && g2=$(cd user/savedata/user2/CUSA00473 && find . -type f | sed 's#^\./##' | sed 's/[0-9][0-9]*$/N/' | sort -u | tr '\n' ' ')
[ -n "$g2" ] && pass "player 2's profile is in user/savedata/user2 ($g2)" || fail "no player 2 saves in user/savedata/user2"
[ "$a_names" = "$b_names" ] && pass "B left the same save files as A ($(echo "$a_names" | tr '\n' ' '))" || {
  fail "B's save files differ from A's"; note "A: $(echo "$a_names" | tr '\n' ' ')"; note "B: $(echo "$b_names" | tr '\n' ' ')"; }
[ "$(grep -c 'Rejecting invalid S#\|stuck\|device lost' "analysis/m2_${tag}_A_stderr.txt")" = 0 ] &&
  [ "$(grep -c 'Rejecting invalid S#\|stuck\|device lost' "analysis/m2_${tag}_B_stderr.txt")" = 0 ] && pass "no GPU warnings" || fail "GPU warnings"
note "t13_coop: $([ $fails = 0 ] && echo PASS || echo "FAIL ($fails)")"
