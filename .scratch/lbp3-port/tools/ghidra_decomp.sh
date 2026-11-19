#!/bin/sh
# Decompiles the eboot functions containing the given addresses (module offsets, as in
# eboot.bin+0x... crash reports) with Ghidra, and prints the C.
#   ghidra_decomp.sh 0x559060 0x559800 > out.c
# Uses the analysed project in the clone's analysis/ghidra (lbp3.gpr, program eboot_etexec.elf:
# analysis/eboot.elf with e_type set to ET_EXEC so addresses stay module offsets; import thunks
# named by ghidra/LabelImports.java). Ghidra 12.2 and a portable JDK 25 are in D:\RE.
# Only one Ghidra at a time can open the project, so callers wait on a lock directory.
# Each call takes about half a minute to start.
[ $# -gt 0 ] || { echo "usage: ghidra_decomp.sh <hex address>..." >&2; exit 2; }
G=/d/Recomps/lbp3-recompiled/analysis/ghidra
S="$(cd "$(dirname "$0")" && pwd -W)/ghidra"
LOCK=$G/decomp.lock
n=0
until mkdir "$LOCK" 2>/dev/null; do
  n=$((n + 1)); [ $n -gt 600 ] && { echo "ghidra_decomp: lock busy for 10 minutes ($LOCK)" >&2; exit 1; }
  sleep 1
done
trap 'rmdir "$LOCK"' EXIT
out=$(mktemp -u "$G/decomp_XXXXXX.c")
log=${out%.c}.log
JAVA_HOME='D:\RE\jdk-25' GHIDRA_HEADLESS_MAXMEM=8G \
  cmd //c "D:\\RE\\ghidra_12.2_PUBLIC\\support\\analyzeHeadless.bat" 'D:\Recomps\lbp3-recompiled\analysis\ghidra' lbp3 \
  -process eboot_etexec.elf -noanalysis -readOnly -scriptPath "$S" \
  -postScript Decompile.java "$(cygpath -w "$out")" "$@" > "$log" 2>&1 < /dev/null
if [ -s "$out" ]; then cat "$out"; rm -f "$out" "$log"; else echo "ghidra_decomp: no output; log: $log" >&2; exit 1; fi
