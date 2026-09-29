// Recover explicitly audited callback entry points omitted by auto-analysis.
// @category MTvGPU
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.*;
import com.google.gson.Gson;
import com.google.gson.GsonBuilder;
import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.util.*;

public class RecoverDriverCallbacks extends GhidraScript {
    @Override public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) throw new IllegalArgumentException("output-dir entry-address...");
        Path out = Path.of(args[0]);
        Files.createDirectories(out);
        Gson gson = new GsonBuilder().setPrettyPrinting().create();
        DecompInterface dc = new DecompInterface();
        dc.openProgram(currentProgram);
        List<Map<String,Object>> results = new ArrayList<>();
        try {
            for (int i=1; i<args.length; i++) {
                monitor.checkCancelled();
                Address a = toAddr(args[i]);
                Map<String,Object> row = new LinkedHashMap<>();
                row.put("address", a.toString());
                Function f = getFunctionAt(a);
                row.put("was_defined", f != null);
                if (f == null) {
                    if (currentProgram.getListing().getDefinedDataContaining(a) != null) {
                        row.put("status", "refused_defined_data");
                        results.add(row); continue;
                    }
                    Function containing = getFunctionContaining(a);
                    // Audited callback table 141106500 references 140024ee4.
                    // The old analysis incorrectly falls through INT 29h
                    // (Windows fast fail) at 140024ee2 into this leaf function.
                    if (currentProgram.getName().equals("mtkm64.sys") &&
                        containing != null && args[i].equals("140024ee4") &&
                        containing.getEntryPoint().equals(toAddr("140024e48")) &&
                        containing.getBody().getMaxAddress().equals(toAddr("140024f12")) &&
                        (getByte(a.subtract(2)) & 255) == 0xcd &&
                        (getByte(a.subtract(1)) & 255) == 0x29) {
                        Instruction fail = getInstructionAt(a.subtract(2));
                        fail.setFlowOverride(FlowOverride.RETURN);
                        AddressSet body = new AddressSet(containing.getBody());
                        body.delete(a, containing.getBody().getMaxAddress());
                        containing.setBody(body);
                        row.put("split_after_verified_fastfail", "140024ee2");
                        containing = null;
                    }
                    if (containing != null) {
                        row.put("status", "refused_inside_existing_function");
                        row.put("containing", containing.getEntryPoint().toString());
                        results.add(row); continue;
                    }
                    if (!currentProgram.getMemory().getExecuteSet().contains(a)) {
                        row.put("status", "refused_non_executable");
                        results.add(row); continue;
                    }
                    disassemble(a);
                    f = createFunction(a, "recovered_" + a.toString());
                }
                if (f == null) {
                    row.put("status", "function_creation_failed");
                } else {
                    dc.flushCache();
                    DecompileResults r = dc.decompileFunction(f, 60, monitor);
                    row.put("name", f.getName());
                    row.put("body_bytes", f.getBody().getNumAddresses());
                    row.put("message", r.getErrorMessage());
                    if (r.decompileCompleted() && r.getDecompiledFunction() != null) {
                        String filename = a.toString() + ".c";
                        Files.writeString(out.resolve(filename), r.getDecompiledFunction().getC(), StandardCharsets.UTF_8);
                        row.put("file", filename);
                        row.put("status", "decompiled");
                    } else row.put("status", "decompilation_failed");
                    List<String> targets = new ArrayList<>();
                    for (Function t : f.getCalledFunctions(monitor)) targets.add(t.getEntryPoint().toString());
                    row.put("calls", targets);
                }
                results.add(row);
                println(gson.toJson(row));
            }
        } finally { dc.dispose(); }
        Files.writeString(out.resolve("functions.json"), gson.toJson(results) + "\n", StandardCharsets.UTF_8);
    }
}
