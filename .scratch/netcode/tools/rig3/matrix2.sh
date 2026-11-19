# matrix2.sh [part]: the set-conditions cells beyond the delay and loss grid, in parts so that one call stays
# well under an hour. Each cell's summary lands in ../../runs/06/<tag>.txt (cells.sh).
#   burst   loss in bursts at 450 ms and 60 ms RTT, gap-free delivery on (b...) and off (nb...)
#   far     RTT past what prediction hides (700 and 1000 ms), and 700 ms with 1% loss
#   freeze  the guest's frame thread stopped for 1 s and 3.5 s, twice each, at 60 and 450 ms RTT, with the
#           default catch-up window and with 10 frames (set on both games)
#   odd     a 10 s one-way hold during the join at 450 ms; a guest suspended as the join's block begins; a
#           12 s freeze of the guest's frame thread with the kick limit at 30 s on both; a join into a level
cd "$(dirname "$0")"
D60=LBP3_P2P_TEST_DELAY=30; D450=LBP3_P2P_TEST_DELAY=225
case "$1" in
burst)
  bash cells.sh b450_b4 120 "$D450 LBP3_P2P_TEST_LOSS=101,4" b450_b10 120 "$D450 LBP3_P2P_TEST_LOSS=301,10" \
    nb450_b4 120 "$D450 LBP3_P2P_TEST_LOSS=101,4 LBP3_P2P_DATA3=0" nb450_b10 120 "$D450 LBP3_P2P_TEST_LOSS=301,10 LBP3_P2P_DATA3=0" \
    nb60_b10 120 "$D60 LBP3_P2P_TEST_LOSS=301,10 LBP3_P2P_DATA3=0" b450_b30 120 "$D450 LBP3_P2P_TEST_LOSS=601,30" ;;
far)
  bash cells.sh c700_l0 120 "LBP3_P2P_TEST_DELAY=350" c1000_l0 120 "LBP3_P2P_TEST_DELAY=500" \
    c700_l1 120 "LBP3_P2P_TEST_DELAY=350 LBP3_P2P_TEST_LOSS=101" ;;
freeze)
  export CELL_FREEZE="1000 3500 1000 3500"
  bash cells.sh f60 120 "$D60" f450 120 "$D450" f60_c10 120 "$D60 LBP3_TEST_CATCHUP_FRAMES=10" \
    f450_c10 120 "$D450 LBP3_TEST_CATCHUP_FRAMES=10" ;;
odd)
  RIG_A_ENV="LBP3_P2P_TEST_HOLD=10,7" bash cells.sh h450 90 "$D450" || exit 1
  CELL_SUSPEND_AT_BLOCK=15 bash cells.sh hung60 60 "$D60 LBP3_SYNC_PROBE=2" || exit 1
  CELL_B_FREEZE_AT=30:12000 bash cells.sh thaw60 90 "$D60 LBP3_TEST_KICK_SECONDS=30" || exit 1
  CELL_LEVEL_FIRST=1 bash cells.sh lv60 90 "$D60" lv450 90 "$D450" ;;
*) echo "usage: matrix2.sh burst|far|freeze|odd"; exit 2 ;;
esac
