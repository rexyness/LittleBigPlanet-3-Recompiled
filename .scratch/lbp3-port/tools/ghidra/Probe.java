// Probe.java: batch static queries on the eboot project (read-only).
//   -postScript Probe.java <out file> <commands file>
// Commands, one per line:
//   xref NAME            references to every symbol named NAME (call sites with their function)
//   xrefaddr HEX         references to an address
//   dec HEX              decompile the function containing HEX
//   scalars HEX,HEX,...  instructions with any of these scalar operands (one pass)
//   strings REGEX        defined strings matching REGEX (case-insensitive) and their references
//   bytes HEXBYTES       occurrences of a byte pattern in initialized memory, with references to each
//   dis HEX N            disassemble N instructions from HEX
//   constrefs HEXBYTES   data occurrences of a byte pattern that code reads, grouped by reading function
//   incafter HEX         "MOV reg,[x+HEX]" followed by INC/ADD on [reg...] (finds counter increments)
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.symbol.*;

import java.io.PrintWriter;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.*;
import java.util.regex.Pattern;

public class Probe extends GhidraScript {
    PrintWriter out;
    DecompInterface decomp;

    String fn(Address a) {
        Function f = getFunctionContaining(a);
        return f == null ? "?" : f.getName() + "@" + f.getEntryPoint();
    }

    void refsTo(Address a, String indent, int cap) {
        int n = 0;
        for (Reference r : getReferencesTo(a)) {
            if (n++ >= cap) { out.println(indent + "... more"); break; }
            out.println(indent + r.getFromAddress() + " " + r.getReferenceType() + " in " + fn(r.getFromAddress()));
        }
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        out = new PrintWriter(args[0], "UTF-8");
        decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        try {
            for (String line : Files.readAllLines(Paths.get(args[1]))) {
                line = line.trim();
                if (line.isEmpty() || line.startsWith("#")) continue;
                out.println("==== " + line);
                String[] p = line.split("\\s+", 2);
                try {
                    switch (p[0]) {
                        case "xref": xref(p[1]); break;
                        case "xrefaddr": refsTo(toAddr(Long.parseUnsignedLong(p[1].replaceFirst("^0[xX]", ""), 16)), "  ", 400); break;
                        case "dec": dec(p[1]); break;
                        case "scalars": scalars(p[1]); break;
                        case "strings": strings(p[1]); break;
                        case "bytes": bytes(p[1]); break;
                        case "dis": dis(p[1]); break;
                        case "incafter": incafter(p[1]); break;
                        case "constrefs": constrefs(p[1]); break;
                        default: out.println("  unknown command");
                    }
                } catch (Exception e) {
                    out.println("  error: " + e);
                }
                out.flush();
            }
        } finally {
            decomp.dispose();
            out.close();
        }
    }

    void xref(String name) {
        SymbolTable st = currentProgram.getSymbolTable();
        int found = 0;
        for (Symbol s : st.getSymbols(name)) {
            found++;
            out.println("  symbol " + s.getName(true) + " at " + s.getAddress());
            refsTo(s.getAddress(), "    ", 400);
            Function f = getFunctionAt(s.getAddress());
            if (f != null && f.isThunk()) out.println("    (thunk)");
        }
        if (found == 0) {
            // substring match on function names
            int n = 0;
            for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
                if (f.getName().contains(name)) {
                    out.println("  function " + f.getName() + " at " + f.getEntryPoint());
                    refsTo(f.getEntryPoint(), "    ", 200);
                    if (++n > 20) break;
                }
            }
            if (n == 0) out.println("  no symbol");
        }
    }

    void dec(String hex) throws Exception {
        Address a = toAddr(Long.parseUnsignedLong(hex.replaceFirst("^0[xX]", ""), 16));
        Function f = getFunctionContaining(a);
        if (f == null) { disassemble(a); f = createFunction(a, null); }
        if (f == null) { out.println("  no function"); return; }
        DecompileResults r = decomp.decompileFunction(f, 180, monitor);
        out.println("  ---- " + f.getName() + " at " + f.getEntryPoint());
        out.println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : "  decompile failed: " + r.getErrorMessage());
    }

    void dis(String args) {
        String[] p = args.split("\\s+");
        Address a = toAddr(Long.parseUnsignedLong(p[0].replaceFirst("^0[xX]", ""), 16));
        int n = Integer.parseInt(p[1]);
        Instruction ins = getInstructionAt(a);
        if (ins == null) ins = getInstructionAfter(a);
        for (int i = 0; i < n && ins != null; i++, ins = ins.getNext())
            out.println("  " + ins.getAddress() + "  " + ins);
    }

    void scalars(String list) {
        Map<Long, Integer> want = new HashMap<>();
        for (String s : list.split(",")) want.put(Long.parseUnsignedLong(s.trim().replaceFirst("^0[xX]", ""), 16), 0);
        InstructionIterator it = currentProgram.getListing().getInstructions(true);
        while (it.hasNext()) {
            Instruction ins = it.next();
            int nops = ins.getNumOperands();
            for (int i = 0; i < nops; i++) {
                for (Object o : ins.getOpObjects(i)) {
                    if (!(o instanceof Scalar)) continue;
                    long v = ((Scalar) o).getUnsignedValue();
                    long v32 = v & 0xffffffffL;
                    Long key = want.containsKey(v) ? Long.valueOf(v) : (want.containsKey(v32) ? Long.valueOf(v32) : null);
                    if (key == null) continue;
                    int c = want.get(key);
                    want.put(key, c + 1);
                    if (c < 400) out.println("  0x" + Long.toHexString(key) + "  " + ins.getAddress() + "  " + ins + "  in " + fn(ins.getAddress()));
                }
            }
        }
        for (Map.Entry<Long, Integer> e : want.entrySet()) out.println("  total 0x" + Long.toHexString(e.getKey()) + ": " + e.getValue());
    }

    void strings(String regex) {
        Pattern pat = Pattern.compile(regex, Pattern.CASE_INSENSITIVE);
        int n = 0;
        DataIterator it = currentProgram.getListing().getDefinedData(true);
        while (it.hasNext()) {
            Data d = it.next();
            if (!d.hasStringValue()) continue;
            Object v = d.getValue();
            if (v == null) continue;
            String s = v.toString();
            if (!pat.matcher(s).find()) continue;
            if (n++ >= 300) { out.println("  ... more"); break; }
            out.println("  " + d.getAddress() + "  \"" + s.replace("\n", "\\n") + "\"");
            refsTo(d.getAddress(), "      ", 8);
        }
        out.println("  total shown " + n);
    }

    void bytes(String hex) throws Exception {
        hex = hex.replaceAll("\\s", "");
        byte[] pat = new byte[hex.length() / 2];
        for (int i = 0; i < pat.length; i++) pat[i] = (byte) Integer.parseInt(hex.substring(2 * i, 2 * i + 2), 16);
        Memory mem = currentProgram.getMemory();
        int n = 0;
        for (MemoryBlock b : mem.getBlocks()) {
            if (!b.isInitialized()) continue;
            Address a = b.getStart();
            while (a != null && a.compareTo(b.getEnd()) <= 0) {
                Address hit = mem.findBytes(a, b.getEnd(), pat, null, true, monitor);
                if (hit == null) break;
                if (n++ < 200) {
                    out.println("  " + hit + " (" + b.getName() + ")");
                    refsTo(hit, "      ", 12);
                }
                a = hit.add(1);
            }
        }
        out.println("  total " + n);
    }

    // incafter HEX: loads "MOV reg,[x+HEX]" followed within 14 instructions by INC/ADD on [reg] or [reg+n]
    void incafter(String hex) {
        long want = Long.parseUnsignedLong(hex.replaceFirst("^0[xX]", ""), 16);
        InstructionIterator it = currentProgram.getListing().getInstructions(true);
        int n = 0;
        while (it.hasNext()) {
            Instruction ins = it.next();
            if (!ins.getMnemonicString().startsWith("MOV")) continue;
            if (ins.getNumOperands() != 2) continue;
            boolean hit = false;
            for (Object o : ins.getOpObjects(1)) if (o instanceof Scalar && ((Scalar) o).getUnsignedValue() == want) hit = true;
            if (!hit || !ins.getDefaultOperandRepresentation(1).contains("[")) continue;
            String reg = ins.getDefaultOperandRepresentation(0);
            Instruction nx = ins.getNext();
            for (int i = 0; i < 14 && nx != null; i++, nx = nx.getNext()) {
                String m = nx.getMnemonicString();
                String op0 = nx.getNumOperands() > 0 ? nx.getDefaultOperandRepresentation(0) : "";
                if ((m.startsWith("INC") || m.startsWith("ADD")) && op0.contains("[" + reg)) {
                    out.println("  " + ins.getAddress() + " " + ins + "  ->  " + nx.getAddress() + " " + nx + "  in " + fn(ins.getAddress()));
                    n++;
                }
                if (nx.getDefaultOperandRepresentation(0).equals(reg) && !m.startsWith("CMP") && !m.startsWith("TEST")) break;
            }
        }
        out.println("  total " + n);
    }

    // constrefs HEXBYTES: data occurrences of the pattern that code reads; prints one line per reading function
    void constrefs(String hex) throws Exception {
        hex = hex.replaceAll("\s", "");
        byte[] pat = new byte[hex.length() / 2];
        for (int i = 0; i < pat.length; i++) pat[i] = (byte) Integer.parseInt(hex.substring(2 * i, 2 * i + 2), 16);
        Memory mem = currentProgram.getMemory();
        TreeMap<String, Integer> fns = new TreeMap<>();
        int sites = 0;
        for (MemoryBlock b : mem.getBlocks()) {
            if (!b.isInitialized()) continue;
            Address a = b.getStart();
            while (a != null && a.compareTo(b.getEnd()) <= 0) {
                Address hit = mem.findBytes(a, b.getEnd(), pat, null, true, monitor);
                if (hit == null) break;
                for (Reference r : getReferencesTo(hit)) {
                    if (!r.getReferenceType().isRead()) continue;
                    sites++;
                    fns.merge(fn(r.getFromAddress()), 1, Integer::sum);
                }
                a = hit.add(1);
            }
        }
        out.println("  read sites " + sites + " in " + fns.size() + " functions");
        for (Map.Entry<String, Integer> e : fns.entrySet()) out.println("    " + e.getKey() + " x" + e.getValue());
    }
}
