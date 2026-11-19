#!/bin/sh
# ghidra_probe.sh <commands file> <out file>: runs ghidra/Probe.java (xref, xrefaddr, dec, dis, scalars, strings, bytes, constrefs, incafter; see its header) on the eboot project under the shared decomp lock.
G=/d/Recomps/lbp3-recompiled/analysis/ghidra
S="$(cd "$(dirname "$0")" && pwd -W)/ghidra"
LOCK=$G/decomp.lock
n=0
until mkdir "$LOCK" 2>/dev/null; do n=$((n + 1)); [ $n -gt 1200 ] && { echo "lock busy" >&2; exit 1; }; sleep 1; done
trap 'rmdir "$LOCK"' EXIT
JAVA_HOME='D:\RE\jdk-25' GHIDRA_HEADLESS_MAXMEM=8G \
  cmd //c "D:\RE\ghidra_12.2_PUBLIC\support\analyzeHeadless.bat" 'D:\Recomps\lbp3-recompiled\analysis\ghidra' lbp3 \
  -process eboot_etexec.elf -noanalysis -readOnly -scriptPath "$S" \
  -postScript Probe.java "$(cygpath -w "$2")" "$(cygpath -w "$1")" > "$2.log" 2>&1 < /dev/null
grep -E "ERROR|Exception" "$2.log" | head -5
wc -l "$2"
