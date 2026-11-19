// Writes Ghidra's C for the functions containing the given eboot addresses (module offsets,
// eboot.bin+0x...) to a file. Read-only; run through tools/ghidra_decomp.sh.
//   -postScript Decompile.java <out file> <hex address> [<hex address> ...]
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

import java.io.PrintWriter;

public class Decompile extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        try (PrintWriter out = new PrintWriter(args[0], "UTF-8")) {
            for (int i = 1; i < args.length; i++) {
                Address a = toAddr(Long.parseUnsignedLong(args[i].replaceFirst("^0[xX]", ""), 16));
                Function f = getFunctionContaining(a);
                if (f == null) {
                    disassemble(a);
                    f = createFunction(a, null);  // in memory only: the project is opened read-only
                }
                if (f == null) {
                    out.println("// " + args[i] + ": no function there");
                    continue;
                }
                DecompileResults r = decomp.decompileFunction(f, 180, monitor);
                out.println("// ---- " + f.getName() + " at 0x" + f.getEntryPoint() + " (asked for " + args[i] + ")");
                out.println(r.decompileCompleted() ? r.getDecompiledFunction().getC()
                                                   : "// decompile failed: " + r.getErrorMessage());
            }
        } finally {
            decomp.dispose();
        }
    }
}
