// Names the eboot's PLT thunks after the PS4 functions they import, so decompiled code reads
// sceImeDialogInit(...) instead of FUN_00e0faf0(...). Run once on the analysed project:
//   analyzeHeadless <dir> lbp3 -process eboot_etexec.elf -noanalysis -scriptPath <this dir>
//     -postScript LabelImports.java <plt_names.txt>
// plt_names.txt has one "<hex address> <name>" per line (tools/disasm.py's plt_names).
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;

import java.nio.file.Files;
import java.nio.file.Paths;

public class LabelImports extends GhidraScript {
    @Override
    public void run() throws Exception {
        int named = 0, created = 0, failed = 0;
        for (String line : Files.readAllLines(Paths.get(getScriptArgs()[0]))) {
            String[] p = line.trim().split("\\s+");
            if (p.length < 2) continue;
            Address a = toAddr(Long.parseUnsignedLong(p[0], 16));
            String name = p[1].replaceAll("[^A-Za-z0-9_]", "_");
            Function f = getFunctionAt(a);
            if (f == null) {
                disassemble(a);
                f = createFunction(a, name);
                if (f != null) created++;
            }
            try {
                if (f != null) {
                    f.setName(name, SourceType.IMPORTED);
                } else {
                    createLabel(a, name, true, SourceType.IMPORTED);
                }
                named++;
            } catch (Exception e) {
                failed++;
                println("could not name " + p[0] + " " + name + ": " + e.getMessage());
            }
        }
        println("named " + named + " import thunks (" + created + " new functions, " + failed + " failed)");
    }
}
