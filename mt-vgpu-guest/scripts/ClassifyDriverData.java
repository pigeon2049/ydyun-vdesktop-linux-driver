// Correct individually audited strings misidentified as callback candidates.
// @category MTvGPU
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.data.ArrayDataType;
import ghidra.program.model.data.ByteDataType;

public class ClassifyDriverData extends GhidraScript {
    @Override public void run() throws Exception {
        for (String arg : getScriptArgs()) {
            String[] parts = arg.split(":");
            Address a = toAddr(parts[0]);
            int size = Integer.parseInt(parts[1]);
            if (size < 1 || size > 32 || getByte(a.add(size-1)) != 0)
                throw new IllegalArgumentException("Invalid audited string span: " + arg);
            Function f = getFunctionAt(a);
            if (f == null || !f.getName().startsWith("recovered_"))
                throw new IllegalStateException("Not an experimental recovered entry: " + a);
            currentProgram.getFunctionManager().removeFunction(a);
            clearListing(a, a.add(size-1));
            createData(a, new ArrayDataType(ByteDataType.dataType, size, 1));
            createLabel(a, "verified_string_data_" + a.toString(), true);
            println("CLASSIFIED DATA " + arg);
        }
    }
}
