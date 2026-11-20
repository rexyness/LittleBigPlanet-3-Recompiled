# cells.sh <tag> <pod seconds> "<settings>" [<tag> <pod seconds> "<settings>" ...]: several cells of the
# set-conditions matrix, one after another (cell.sh), each summary kept in ../../runs/06/<tag>.txt with its
# marks beside it. Stops at the first cell that can't launch (another game running, the lock held).
# Settings for one instance, or cell.sh's CELL_ variables, go in front as environment: they then apply to every
# cell of the call.
cd "$(dirname "$0")"
out=../../runs/06; mkdir -p $out
while [ $# -ge 3 ]; do
  tag=$1; pod=$2; set3=$3; shift 3
  bash cell.sh $tag $pod $set3 > $out/$tag.txt 2>&1
  cp "${TMPDIR:-/tmp}/rig3/cell_$tag.marks" $out/$tag.marks 2>/dev/null
  grep -q "launch failed" $out/$tag.txt && { echo "$tag: launch failed, stopping"; tail -5 $out/$tag.txt; exit 1; }
  grep '^ROW\|^CELL' $out/$tag.txt
done
