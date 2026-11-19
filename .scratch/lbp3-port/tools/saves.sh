# saves.sh: sourced by the test scripts after they cd to the clone.
#   . /d/Recomps/lbp3-recompiled/.scratch/lbp3-port/tools/saves.sh; saves_keep; saves_baseline
# The tests start from a fixed baseline, analysis/savedata_thruster_on (the human's test level
# with one Thruster firing, as of 2026-11-10 08:20). The human's own saves have moved on since,
# so a run first keeps the current saves aside (analysis/savedata_human_kept) and puts them back
# when the script exits, by any path.
#
# The marker analysis/savedata_live_is_test says "the live saves are test saves; the human's
# are in the kept copy". Installing test saves sets it (saves_mark_test), putting the human's
# back clears it. While it is set, live saves are never kept as the human's: on 2026-11-11 a
# chain stopped halfway left orphaned scripts that kept the test baseline as the "human" copy,
# which the next run would have put back over the human's saves.
#   - marker set, kept copy present: an interrupted run; reuse the kept copy.
#   - marker set, no kept copy: refuse (restore the human's saves from analysis/savedata_backups
#     by hand, then delete the marker).
#   - no marker, kept copy present: killed between keeping and installing; reuse it.
#   - no marker, no kept copy: the live saves are the human's; keep them.
# A script called by another one that already kept the saves (SAVES_NESTED=1) does nothing.
# Everything else in user/savedata (the local co-op guests' profiles user2..user4, ticket 13, or
# anything else) is kept as a whole in $KEPT.others and put back exactly: a folder a test created
# is removed, and a test starts without any of them. (Until 2026-11-13 only user2..user4 were kept,
# in $KEPT.guests; a kept copy in that layout is taken over.) Timestamps are kept (cp -p), so a
# folder put back doesn't look new. With no kept copy nothing is removed: those may be the human's.
KEPT=analysis/savedata_human_kept
TESTMARK=analysis/savedata_live_is_test
others_list() { ls -A user/savedata 2>/dev/null | grep -vx "CUSA00473"; }
others_keep() {
  [ -d "$KEPT.others" ] && return 0
  [ -d "$KEPT.guests" ] && { mv "$KEPT.guests" "$KEPT.others"; return; }
  mkdir -p "$KEPT.others" || return 1
  for e in $(others_list); do cp -rp "user/savedata/$e" "$KEPT.others/" || return 1; done
}
# Clears only what is kept (a reused kept copy from an interrupted run may lack it: keep first).
others_clear() { others_keep || return 1; for e in $(others_list); do rm -rf "user/savedata/$e"; done; }
others_drop() { for e in $(others_list); do rm -rf "user/savedata/$e"; done; }
others_restore() {
  [ -d "$KEPT.guests" ] && [ ! -d "$KEPT.others" ] && mv "$KEPT.guests" "$KEPT.others"
  [ -d "$KEPT.others" ] || return 0
  others_drop
  for e in $(ls -A "$KEPT.others"); do cp -rp "$KEPT.others/$e" user/savedata/ || return 1; done
  rm -rf "$KEPT.others"
}
# The older names.
guests_keep() { others_keep; }
guests_clear() { others_clear; }
saves_keep() {
  [ "$SAVES_NESTED" = 1 ] && return 0
  # Never copy the saves from under a running game, which may be writing them.
  taskkill //f //im lbp3.exe >/dev/null 2>&1 && sleep 1
  if [ -d "$KEPT" ]; then
    echo "saves: reusing the kept copy from an interrupted run ($KEPT)"
  elif [ -f "$TESTMARK" ]; then
    echo "saves: the live saves are test saves ($TESTMARK) and there is no kept copy of the human's."
    echo "saves: restore them from analysis/savedata_backups, delete $TESTMARK, and run again."
    exit 1
  else
    cp -r user/savedata/CUSA00473 "$KEPT" || { echo "saves: could not keep the current saves"; exit 1; }
    others_keep || { echo "saves: could not keep the other profiles in user/savedata"; exit 1; }
  fi
  export SAVES_NESTED=1
  trap saves_return EXIT
}
# Call before (or right after) putting any test saves in user/savedata.
saves_mark_test() { [ -d "$KEPT" ] || { echo "saves: no kept copy; refusing to install test saves"; return 1; }; : > "$TESTMARK"; }
# SAVES_BASELINE=<dir> starts from another saved copy, e.g. analysis/savedata_water_level (the
# human's level with 3 Thrusters and a water emitter, copied 2026-11-10 after their session).
saves_baseline() {
  saves_mark_test || return 1
  others_clear
  rm -rf user/savedata/CUSA00473 && cp -r "${SAVES_BASELINE:-analysis/savedata_thruster_on}" user/savedata/CUSA00473
}
saves_return() {
  [ -d "$KEPT" ] || return 0
  taskkill //f //im lbp3.exe >/dev/null 2>&1; sleep 1
  others_restore || echo "saves: the other profiles could not all be put back ($KEPT.others kept)"
  rm -rf user/savedata/CUSA00473 && cp -r "$KEPT" user/savedata/CUSA00473 && rm -rf "$KEPT" && rm -f "$TESTMARK"
}
